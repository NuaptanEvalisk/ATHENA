/******************************************************************************
* MODULE     : QTMNodePropertiesDialog.hpp
* DESCRIPTION: Native node property inspector and actor-owned edit handoff
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include <functional>
#include <string>
#include "Data/Convert/Xml/athena_document_xml.hpp"

class QDialog;
class QWidget;

using node_property_completion= std::function<void (std::string)>;
using node_property_submit=
  std::function<void (std::string, node_property_completion)>;

struct node_property_snapshot {
  tree header;
  athena::document::xml_format_version source_version;
};

// A body-free typed metadata carrier, not an invalid zero-arity native node.
// The source version is explicit; v1 sources use v2 transport for metadata.
std::string encode_node_property_snapshot (
  const tree& source, athena::document::xml_format_version source_version);
node_property_snapshot decode_node_property_snapshot (const std::string&);

// UI-thread factory. The snapshot is a self-describing native XML carrier.
// Completion is delivered on the UI thread: empty means success, else error.
QDialog* make_node_properties_dialog (
  std::string snapshot, bool canonical, bool writable,
  node_property_submit submit, QWidget* parent= nullptr);

// Called from the owning BufferActor, with the actual attached focus node.
bool node_properties_show (tree source);
