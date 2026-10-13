/******************************************************************************
* MODULE     : pdf_merge.cpp
* DESCRIPTION: Merge native PDF exports while preserving link annotations
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "pdf_merge.hpp"
#include "PDFWriter/PDFWriter.h"
#include "PDFWriter/PDFDocumentCopyingContext.h"
#include "PDFWriter/DocumentContext.h"
#include "PDFWriter/DocumentContextExtenderAdapter.h"
#include "PDFWriter/DictionaryContext.h"
#include "PDFWriter/ObjectsContext.h"
#include "PDFWriter/IndirectObjectsReferenceRegistry.h"
#include "PDFWriter/PDFParser.h"
#include "PDFWriter/PDFArray.h"
#include "PDFWriter/PDFDictionary.h"
#include "PDFWriter/PDFName.h"
#include "PDFWriter/PDFLiteralString.h"
#include "PDFWriter/PDFHexString.h"
#include "PDFWriter/PDFObjectCast.h"
#include <cmath>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>

namespace athena::pdf {
namespace {
void require (bool ok, const char* message) {
  if (!ok) throw std::runtime_error (message);
}
void check_cancelled (const std::function<bool()>& cancelled) {
  if (cancelled && cancelled ()) throw std::runtime_error ("PDF export cancelled");
}
std::string text (PDFObject* object) {
  if (!object) return {};
  switch (object->GetType ()) {
  case PDFObject::ePDFObjectName:
    return static_cast<PDFName*> (object)->GetValue ();
  case PDFObject::ePDFObjectLiteralString:
    return static_cast<PDFLiteralString*> (object)->GetValue ();
  case PDFObject::ePDFObjectHexString:
    return static_cast<PDFHexString*> (object)->GetValue ();
  default: return {};
  }
}
struct member_pages {
  ObjectIDTypeToObjectIDTypeMap mapping;
  std::vector<ObjectIDType> output, annotations;
};
struct output_destination {
  ObjectIDType page;
  double x, y;
};

// Pages are written before the global link scope is known. Only their annotation
// arrays are deferred; content streams and resources follow Hummus's normal copy.
class annotation_arrays final: public DocumentContextExtenderAdapter {
  PDFHummus::DocumentContext& document;
public:
  ObjectIDType next= 0;
  explicit annotation_arrays (PDFHummus::DocumentContext& doc): document (doc) {
    document.AddDocumentContextExtender (this);
  }
  ~annotation_arrays () { document.RemoveDocumentContextExtender (this); }
  PDFHummus::EStatusCode OnPageWrite (
    PDFPage*, DictionaryContext* page, ObjectsContext*,
    PDFHummus::DocumentContext*) override {
    if (!next || page->WriteKey ("Annots") != PDFHummus::eSuccess)
      return PDFHummus::eFailure;
    page->WriteNewObjectReferenceValue (next);
    next= 0;
    return PDFHummus::eSuccess;
  }
};

void copy_value (PDFDocumentCopyingContext& copier, PDFObject* value,
                 std::set<ObjectIDType>& pending) {
  auto copied= copier.CopyDirectObjectWithDeepCopy (value);
  require (copied.first == PDFHummus::eSuccess, "Could not copy PDF annotation value");
  pending.insert (copied.second.begin (), copied.second.end ());
}

// Native exports use catalog /Dests. Resolve member-local names before their
// catalog is discarded; explicit destination arrays only need page-ID remapping.
PDFObject* local_destination (PDFParser& parser, PDFObject* value) {
  if (!value) return nullptr;
  if (value->GetType () == PDFObject::ePDFObjectArray) {
    value->AddRef ();
    return value;
  }
  const auto name= text (value);
  require (!name.empty (), "Invalid PDF link destination");
  PDFObjectCastPtr<PDFDictionary> catalog (
    parser.QueryDictionaryObject (parser.GetTrailer (), "Root"));
  require (!!catalog, "Missing PDF catalog");
  PDFObjectCastPtr<PDFDictionary> destinations (
    parser.QueryDictionaryObject (catalog.GetPtr (), "Dests"));
  require (!!destinations, "Missing native PDF destination dictionary");
  RefCountPtr<PDFObject> found (
    parser.QueryDictionaryObject (destinations.GetPtr (), name));
  require (!!found, "Unresolved member-local PDF destination");
  if (found->GetType () == PDFObject::ePDFObjectDictionary)
    return parser.QueryDictionaryObject (static_cast<PDFDictionary*> (found.GetPtr ()), "D");
  found->AddRef ();
  return found.GetPtr ();
}

ObjectIDType copy_annotation (
  PDFDocumentCopyingContext& copier, ObjectsContext& objects, PDFDictionary* annotation,
  const std::map<std::string,output_destination>& destinations,
  const std::function<std::string(const std::string&)>& resolve_link) {
  PDFParser& parser= *copier.GetSourceDocumentParser ();
  RefCountPtr<PDFObject> subtype (parser.QueryDictionaryObject (annotation, "Subtype"));
  PDFObjectCastPtr<PDFDictionary> action (parser.QueryDictionaryObject (annotation, "A"));
  RefCountPtr<PDFObject> action_type (
    !!action ? parser.QueryDictionaryObject (action.GetPtr (), "S") : nullptr);
  const output_destination* target= nullptr;
  if (text (subtype.GetPtr ()) == "Link" && text (action_type.GetPtr ()) == "URI") {
    RefCountPtr<PDFObject> uri (parser.QueryDictionaryObject (action.GetPtr (), "URI"));
    const auto original= text (uri.GetPtr ());
    const auto resolved= resolve_link ? resolve_link (original) : original;
    if (resolved.empty ()) return 0;
    if (resolved != original) {
      require (resolved.front () == '#', "PDF merge resolver must return an internal destination");
      auto found= destinations.find (resolved.substr (1));
      if (found == destinations.end ()) return 0;
      target= &found->second;
    }
  }
  RefCountPtr<PDFObject> direct (parser.QueryDictionaryObject (annotation, "Dest"));
  RefCountPtr<PDFObject> local (target || !direct ? nullptr : local_destination (parser, direct.GetPtr ()));

  ObjectIDType id= objects.StartNewIndirectObject ();
  DictionaryContext* dictionary= objects.StartDictionary ();
  std::set<ObjectIDType> pending;
  auto entries= annotation->GetIterator ();
  while (entries.MoveNext ()) {
    const auto key= entries.GetKey ()->GetValue ();
    if (target && (key == "A" || key == "Dest")) continue;
    require (dictionary->WriteKey (key) == PDFHummus::eSuccess, "Could not write PDF annotation key");
    copy_value (copier, key == "Dest" && !!local ? local.GetPtr () : entries.GetValue (), pending);
  }
  if (target) {
    require (dictionary->WriteKey ("Dest") == PDFHummus::eSuccess, "Could not write PDF link target");
    objects.StartArray ();
    objects.WriteNewIndirectObjectReference (target->page);
    objects.WriteName ("XYZ");
    objects.WriteDouble (target->x);
    objects.WriteDouble (target->y);
    objects.WriteNull ();
    objects.EndArray ();
  }
  require (objects.EndDictionary (dictionary) == PDFHummus::eSuccess, "Could not finish PDF annotation");
  objects.EndIndirectObject ();
  require (copier.CopyNewObjectsForDirectObject (
    ObjectIDTypeList (pending.begin (), pending.end ())) == PDFHummus::eSuccess,
    "Could not copy PDF annotation dependencies");
  return id;
}
} // namespace

void merge_documents (
  const std::vector<merge_member>& members, const std::string& output,
  const std::function<std::string(const std::string&)>& resolve_link,
  const std::function<bool()>& cancelled) {
  PDFWriter writer;
  require (writer.StartPDF (output, ePDFVersion17) == PDFHummus::eSuccess,
           "Could not create compound PDF");
  auto& objects= writer.GetObjectsContext ();
  std::vector<member_pages> pages (members.size ());
  std::map<std::string,output_destination> destinations;
  {
    annotation_arrays arrays (writer.GetDocumentContext ());
    for (std::size_t i= 0; i < members.size (); ++i) {
      check_cancelled (cancelled);
      std::unique_ptr<PDFDocumentCopyingContext> copier (
        writer.CreatePDFCopyingContext (members[i].filename));
      require (copier != nullptr, "Could not open member PDF");
      auto* parser= copier->GetSourceDocumentParser ();
      for (unsigned long p= 0; p < parser->GetPagesCount (); ++p) {
        check_cancelled (cancelled);
        arrays.next= objects.GetInDirectObjectsRegistry ().AllocateNewObjectID ();
        pages[i].annotations.push_back (arrays.next);
        auto appended= copier->AppendPDFPageFromPDF (p);
        require (appended.first == PDFHummus::eSuccess, "Could not append member PDF page");
        pages[i].output.push_back (appended.second);
        pages[i].mapping.emplace (parser->GetPageObjectID (p), appended.second);
      }
      for (const auto& dest: members[i].destinations) {
        require (dest.page < pages[i].output.size () && std::isfinite (dest.x) &&
                 std::isfinite (dest.y), "Invalid member PDF destination");
        destinations.emplace (dest.label,
          output_destination {pages[i].output[dest.page], dest.x, dest.y});
      }
    }
  }
  // Reopen one member at a time; page mappings prevent annotation references
  // from importing orphan copies of source page trees. First duplicate label wins.
  for (std::size_t i= 0; i < members.size (); ++i) {
    check_cancelled (cancelled);
    std::unique_ptr<PDFDocumentCopyingContext> copier (
      writer.CreatePDFCopyingContext (members[i].filename));
    require (copier != nullptr, "Could not reopen member PDF");
    copier->ReplaceSourceObjects (pages[i].mapping);
    auto* parser= copier->GetSourceDocumentParser ();
    require (parser->GetPagesCount () == pages[i].output.size (), "Member PDF changed during merge");
    for (unsigned long p= 0; p < parser->GetPagesCount (); ++p) {
      check_cancelled (cancelled);
      RefCountPtr<PDFDictionary> page (parser->ParsePage (p));
      require (!!page, "Could not read member PDF page");
      PDFObjectCastPtr<PDFArray> annotations (parser->QueryDictionaryObject (page.GetPtr (), "Annots"));
      std::vector<ObjectIDType> retained;
      if (!!annotations) for (unsigned long a= 0; a < annotations->GetLength (); ++a) {
        check_cancelled (cancelled);
        PDFObjectCastPtr<PDFDictionary> annotation (parser->QueryArrayObject (annotations.GetPtr (), a));
        require (!!annotation, "Invalid member PDF annotation");
        auto id= copy_annotation (*copier, objects, annotation.GetPtr (), destinations, resolve_link);
        if (id) retained.push_back (id);
      }
      objects.StartNewIndirectObject (pages[i].annotations[p]);
      objects.StartArray ();
      for (auto id: retained) objects.WriteNewIndirectObjectReference (id);
      objects.EndArray ();
      objects.EndIndirectObject ();
    }
  }
  check_cancelled (cancelled);
  require (writer.EndPDF () == PDFHummus::eSuccess, "Could not finish compound PDF");
}
} // namespace athena::pdf
