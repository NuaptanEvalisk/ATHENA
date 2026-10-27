/******************************************************************************
* MODULE     : QTMNodePropertiesDialog.cpp
* DESCRIPTION: Lossless enunciation fields and lease-checked native property edits
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#include "QTMNodePropertiesDialog.hpp"
#include "ATHENA/Data/document_node_model.hpp"
#include "ATHENA/Data/enunciation_model.hpp"
#include "Data/Convert/Xml/athena_document_xml.hpp"
#include "Data/Convert/Xml/document_file_codec.hpp"
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "tm_window.hpp"
#include "qt_widget.hpp"
#include "qt_utilities.hpp"
#include "convert.hpp"
#include "native_interfaces.hpp"
#include "Typeset/Env/local_environment.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <stdexcept>

namespace {
namespace node= athena::node;
namespace model= athena::document_node;
namespace en= athena::enunciation;
namespace xml= athena::document;

QString qs (const std::string& s) { return QString::fromUtf8 (s.data (), s.size ()); }
std::string utf8 (const QString& s) { return s.toUtf8 ().toStdString (); }
QString description (const tree& t) {
  return to_qstring (is_atomic (t) ? t->label : tree_to_scheme (t));
}
template<class T> const T* field (const tree& header, const char* key) {
  const auto* meta= node::get (header);
  if (!meta) return nullptr;
  auto found= meta->properties.find (key);
  return found == meta->properties.end () ? nullptr : std::get_if<T> (&found->second.data);
}
QToolButton* tool (const char* icon, const char* label, QWidget* parent) {
  auto* button= new QToolButton (parent);
  button->setIcon (QIcon::fromTheme (icon));
  button->setToolTip (label);
  button->setAccessibleName (label);
  return button;
}

// Native structural editing in source mode, never evaluation of stored metadata.
// The input buffer is private and the original field is unchanged on cancel.
class RichField: public QWidget {
  tree original;
  std::shared_ptr<const athena::contracts::detached_environment> environment;
  xml::xml_format_version source_version;
  QLineEdit* line;
  QToolButton* edit;
public:
  explicit RichField (tree value, xml::xml_format_version version, QWidget* parent,
                     std::shared_ptr<const athena::contracts::detached_environment> context):
    QWidget (parent), original (copy (value)), environment (std::move (context)),
    source_version (version) {
    auto* layout= new QHBoxLayout (this);
    layout->setContentsMargins (0, 0, 0, 0);
    line= new QLineEdit (this);
    edit= tool ("document-edit", "Edit structured text", this);
    layout->addWidget (line, 1);
    layout->addWidget (edit);
    refresh ();
    connect (edit, &QToolButton::clicked, this, [this] {
      try { editStructure (); }
      catch (const std::exception& failure) {
        QMessageBox::warning (this, "Structured text", QString::fromUtf8 (failure.what ()));
      }
      catch (const string& failure) {
        QMessageBox::warning (this, "Structured text", to_qstring (failure));
      }
    });
  }
  tree value () const {
    if (!is_atomic (original) || line->text () == description (original)) return copy (original);
    tree changed (from_qstring (line->text ()));
    node::copy_metadata (original, changed);
    return changed;
  }
private:
  void refresh () {
    line->setReadOnly (!is_atomic (original));
    line->setText (description (original));
  }
  void editStructure () {
    tree initial= value ();
    widget input;
    QDialog dialog (this);
    dialog.setWindowTitle ("Structured text");
    dialog.resize (720, 280);
    auto* layout= new QVBoxLayout (&dialog);
    url name (string ("tmfs://aux/node-property-") * from_qstring (qs (node::new_id ())));
    tree style= compound ("style", tuple ("generic", "gui-base"));
    input= texmacs_input_widget (
      tree (WITH, "preamble", "true", tree (DOCUMENT, initial)), style, name,
      source_version, environment);
    struct CloseInput {
      widget& input;
      ~CloseInput () { if (!is_nil (input)) send_destroy (input); }
    } closeInput {input};
    const tree seeded= get_buffer_body (name);
    auto* editor= concrete (input)->as_qwidget (&dialog);
    editor->setMinimumHeight (160);
    layout->addWidget (editor, 1);
    auto* buttons= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec () == QDialog::Accepted) {
      tree body= get_buffer_body (name);
      if (is_func (body, DOCUMENT) && body != seeded) {
        original= copy (N(body) == 1 ? body[0] : body);
        refresh ();
      }
    }
  }
};

class RichNames: public QWidget {
  QVBoxLayout* rows;
  std::shared_ptr<const athena::contracts::detached_environment> environment;
  xml::xml_format_version version;
  std::vector<RichField*> fields;
  node::property original;
  std::vector<tree> initial;
public:
  RichNames (const tree& source, xml::xml_format_version format, QWidget* parent,
             std::shared_ptr<const athena::contracts::detached_environment> context):
    QWidget (parent), environment (std::move (context)), version (format) {
    rows= new QVBoxLayout (this);
    rows->setContentsMargins (0, 0, 0, 0);
    auto* add= tool ("list-add", "Add name", this);
    rows->addWidget (add, 0, Qt::AlignLeft);
    auto values= en::names (source);
    if (values.empty ()) values.emplace_back ("");
    initial= values;
    original= node::get (source)->properties.at ("name");
    for (const auto& value: values) append (value);
    connect (add, &QToolButton::clicked, this, [this] { append (tree ("")); });
  }
  node::property value () const {
    node::property::list result;
    for (auto* field: fields) result.emplace_back (node::rich_text {field->value ()});
    bool unchanged= fields.size () == initial.size ();
    for (std::size_t i=0; unchanged && i<fields.size (); ++i)
      unchanged= fields[i]->value () == initial[i];
    if (unchanged) return original;
    return node::property (std::move (result));
  }
private:
  void append (tree value) {
    auto* row= new QWidget (this);
    auto* layout= new QHBoxLayout (row);
    layout->setContentsMargins (0, 0, 0, 0);
    auto* field= new RichField (value, version, row, environment);
    auto* remove= tool ("list-remove", "Remove name", row);
    layout->addWidget (field, 1); layout->addWidget (remove);
    fields.push_back (field);
    rows->insertWidget (rows->count ()-1, row);
    connect (remove, &QToolButton::clicked, this, [this, row, field] {
      if (fields.size () == 1) return;
      fields.erase (std::find (fields.begin (), fields.end (), field));
      delete row;
    });
  }
};

void inspect (QTreeWidgetItem* item, const node::property& p) {
  std::visit ([&] (const auto& v) {
    using T= std::decay_t<decltype (v)>;
    QString type, value;
    if constexpr (std::is_same_v<T, std::string>) { type= "Text"; value= qs (v); }
    else if constexpr (std::is_same_v<T, bool>) { type= "Boolean"; value= v ? "true" : "false"; }
    else if constexpr (std::is_same_v<T, std::int64_t>) { type= "Integer"; value= QString::number (qlonglong (v)); }
    else if constexpr (std::is_same_v<T, double>) { type= "Real"; value= QString::number (v, 'g', 17); }
    else if constexpr (std::is_same_v<T, node::reference>) { type= "Node reference"; value= qs (v.id); }
    else if constexpr (std::is_same_v<T, node::rich_text>) { type= "Structured text"; value= description (v.content); }
    else if constexpr (std::is_same_v<T, node::property::list>) {
      type= "List";
      for (std::size_t i= 0; i < v.size (); ++i) {
        auto* child= new QTreeWidgetItem (item, {QString::number (i + 1)});
        inspect (child, v[i]);
      }
    }
    else {
      type= "Dictionary";
      for (const auto& entry: v) {
        auto* child= new QTreeWidgetItem (item, {qs (entry.first)});
        inspect (child, entry.second);
      }
    }
    item->setText (1, type);
    item->setText (2, value);
    item->setToolTip (2, value);
  }, p.data);
}

class NodeDialog: public QDialog {
  tree header;
  std::shared_ptr<const athena::contracts::detached_environment> environment;
  xml::xml_format_version source_version;
  node_property_submit submit;
  QComboBox *kind= nullptr, *variant= nullptr;
  RichNames* name= nullptr;
  QLineEdit *year= nullptr, *target= nullptr;
  QCheckBox* numbered= nullptr;
  QListWidget* authors= nullptr;
  std::vector<tree> authorValues;
  QLabel* error;
  QDialogButtonBox* buttons;
  bool applying= false;
public:
  NodeDialog (std::string snapshot, bool canonical, bool writable,
              node_property_submit commit, QWidget* parent,
              std::shared_ptr<const athena::contracts::detached_environment> context):
    QDialog (parent), environment (std::move (context)), submit (std::move (commit)) {
    auto decoded= decode_node_property_snapshot (snapshot);
    header= std::move (decoded.header);
    source_version= decoded.source_version;
    if (canonical && (!field<std::string> (header, "kind") ||
        !en::valid_names (header) || !field<bool> (header, "numbered")))
      throw std::invalid_argument ("Invalid editable node properties");
    setObjectName ("node-properties-dialog");
    setWindowTitle (canonical ? "Enunciation properties" : "Node properties");
    resize (760, canonical ? 700 : 400);
    auto* layout= new QVBoxLayout (this);
    auto* scroll= new QScrollArea (this);
    scroll->setWidgetResizable (true);
    auto* body= new QWidget (scroll);
    auto* content= new QVBoxLayout (body);
    auto* identity= new QFormLayout;
    identity->addRow ("Node:", new QLabel (is_atomic (header) ? "Text" : to_qstring (as_string (L(header))), body));
    auto* id= new QLineEdit (qs (node::id (header)), body);
    id->setObjectName ("node-uuid");
    id->setReadOnly (true);
    id->setPlaceholderText ("Not assigned");
    identity->addRow ("UUID:", id);
    content->addLayout (identity);
    if (canonical) buildFields (content, body, writable);
    auto* details= new QTreeWidget (body);
    details->setObjectName ("node-property-inspector");
    details->setHeaderLabels ({"Property", "Type", "Value"});
    details->setRootIsDecorated (true);
    details->setMinimumHeight (150);
    details->header ()->setSectionResizeMode (2, QHeaderView::Stretch);
    if (const auto* metadata= node::get (header))
      for (const auto& entry: metadata->properties) {
        auto* item= new QTreeWidgetItem (details, {qs (entry.first)});
        inspect (item, entry.second);
      }
    details->setColumnWidth (0, 180);
    details->setColumnWidth (1, 140);
    content->addWidget (details, 1);
    scroll->setWidget (body);
    layout->addWidget (scroll, 1);
    error= new QLabel (this);
    error->setObjectName ("node-properties-error");
    error->setTextFormat (Qt::PlainText);
    error->setWordWrap (true);
    layout->addWidget (error);
    buttons= new QDialogButtonBox (canonical && writable ?
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel : QDialogButtonBox::Close, this);
    layout->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (buttons, &QDialogButtonBox::accepted, this, [this] { apply (); });
  }
protected:
  void reject () override { if (!applying) QDialog::reject (); }
private:
  void buildFields (QVBoxLayout* layout, QWidget* parent, bool writable) {
    auto* fields= new QWidget (parent);
    fields->setEnabled (writable);
    auto* form= new QFormLayout (fields);
    form->setContentsMargins (0, 0, 0, 0);
    kind= new QComboBox (fields);
    kind->setObjectName ("enunciation-kind");
    kind->setEditable (true);
    for (const auto& definition: en::standard_registry ().kinds ())
      kind->addItem (qs (definition.first));
    kind->setCurrentText (qs (*field<std::string> (header, "kind")));
    form->addRow ("Kind:", kind);
    variant= new QComboBox (fields);
    variant->setObjectName ("enunciation-variant");
    variant->setEditable (true);
    variant->addItem ("");
    if (const auto* v= field<std::string> (header, "variant")) variant->setCurrentText (qs (*v));
    const auto updateVariants= [this] {
      const QString selected= variant->currentText ();
      QSignalBlocker blocker (variant);
      variant->clear (); variant->addItem ("");
      if (const auto* definition= en::standard_registry ().kind (utf8 (kind->currentText ())))
        for (const auto& entry: definition->variants) variant->addItem (qs (entry.first));
      variant->setCurrentText (selected);
    };
    updateVariants ();
    connect (kind, &QComboBox::currentTextChanged, this, updateVariants);
    form->addRow ("Variant:", variant);
    name= new RichNames (header, source_version, fields, environment);
    name->setObjectName ("enunciation-name");
    form->addRow ("Name:", name);
    numbered= new QCheckBox ("Numbered", fields);
    numbered->setObjectName ("enunciation-numbered");
    numbered->setChecked (*field<bool> (header, "numbered"));
    form->addRow ("", numbered);
    year= new QLineEdit (fields);
    year->setObjectName ("enunciation-year");
    if (const auto* v= field<std::string> (header, "year")) year->setText (qs (*v));
    form->addRow ("Year:", year);
    target= new QLineEdit (fields);
    target->setObjectName ("enunciation-target");
    if (const auto* v= field<node::reference> (header, "target")) target->setText (qs (v->id));
    form->addRow ("Target UUID:", target);
    auto* authorBox= new QWidget (fields);
    auto* authorLayout= new QVBoxLayout (authorBox);
    authorLayout->setContentsMargins (0, 0, 0, 0);
    authors= new QListWidget (authorBox);
    authors->setObjectName ("enunciation-attribution");
    authors->setMaximumHeight (100);
    if (const auto* values= field<node::property::list> (header, "attribution"))
      for (const auto& value: *values) authorValues.push_back (std::get<node::rich_text> (value.data).content);
    refreshAuthors ();
    authorLayout->addWidget (authors);
    auto* controls= new QHBoxLayout;
    auto* add= tool ("list-add", "Add attribution", authorBox);
    auto* edit= tool ("document-edit", "Edit attribution", authorBox);
    auto* remove= tool ("list-remove", "Remove attribution", authorBox);
    auto* up= tool ("go-up", "Move attribution up", authorBox);
    auto* down= tool ("go-down", "Move attribution down", authorBox);
    for (auto* button: {add, edit, remove, up, down}) controls->addWidget (button);
    controls->addStretch ();
    authorLayout->addLayout (controls);
    connect (add, &QToolButton::clicked, this, [this] { editAuthor (-1); });
    connect (edit, &QToolButton::clicked, this, [this] { if (authors->currentRow () >= 0) editAuthor (authors->currentRow ()); });
    connect (authors, &QListWidget::itemDoubleClicked, this, [this] { editAuthor (authors->currentRow ()); });
    connect (remove, &QToolButton::clicked, this, [this] {
      int i= authors->currentRow ();
      if (i >= 0) { authorValues.erase (authorValues.begin () + i); refreshAuthors (); }
    });
    for (auto pair: {std::make_pair (up, -1), std::make_pair (down, 1)})
      connect (pair.first, &QToolButton::clicked, this, [this, delta= pair.second] {
        int i= authors->currentRow (), j= i + delta;
        if (i >= 0 && j >= 0 && j < int (authorValues.size ())) {
          std::swap (authorValues[i], authorValues[j]); refreshAuthors (); authors->setCurrentRow (j);
        }
      });
    form->addRow ("Attribution:", authorBox);
    layout->addWidget (fields);
  }
  void refreshAuthors () {
    authors->clear ();
    for (const auto& value: authorValues) authors->addItem (description (value));
  }
  void editAuthor (int index) {
    QDialog dialog (this);
    dialog.setWindowTitle ("Attribution");
    dialog.resize (600, 120);
    auto* layout= new QVBoxLayout (&dialog);
    auto* text= new RichField (index < 0 ? tree ("") : authorValues[index],
                              source_version, &dialog, environment);
    layout->addWidget (text);
    auto* actions= new QDialogButtonBox (QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
    layout->addWidget (actions);
    connect (actions, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect (actions, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    if (dialog.exec () == QDialog::Accepted) {
      if (index < 0) { index= int (authorValues.size ()); authorValues.push_back (text->value ()); }
      else authorValues[index]= text->value ();
      refreshAuthors (); authors->setCurrentRow (index);
    }
  }
  void apply () {
    try {
      node::metadata metadata= *node::get (header);
      auto& p= metadata.properties;
      p["kind"]= node::property (utf8 (kind->currentText ()));
      p["name"]= name->value ();
      p["numbered"]= node::property (numbered->isChecked ());
      const auto optionalText= [&] (const char* key, const QString& value) {
        const auto* old= field<std::string> (header, key);
        if (value == (old ? qs (*old) : QString ())) return;
        if (value.isEmpty ()) p.erase (key);
        else p[key]= node::property (utf8 (value));
      };
      optionalText ("year", year->text ());
      optionalText ("variant", variant->currentText ());
      if (target->text ().isEmpty ()) p.erase ("target");
      else p["target"]= node::property (node::reference {utf8 (target->text ())});
      node::property::list names;
      for (const auto& author: authorValues) names.emplace_back (node::rich_text {copy (author)});
      if (!names.empty () || p.count ("attribution")) p["attribution"]= node::property (std::move (names));
      tree changed= model::property_header (header);
      node::set (changed, metadata);
      buttons->setEnabled (false);
      applying= true;
      error->clear ();
      QPointer<NodeDialog> guard (this);
      submit (encode_node_property_snapshot (changed, source_version), [guard] (std::string message) {
        if (!guard) return;
        guard->applying= false;
        guard->buttons->setEnabled (true);
        if (message.empty ()) guard->accept ();
        else guard->error->setText (qs (message));
      });
    }
    catch (const std::exception& failure) {
      applying= false;
      buttons->setEnabled (true); error->setText (QString::fromUtf8 (failure.what ()));
    }
  }
};

struct PropertyRequest {
  athena_actor_id actor;
  athena_view_id view;
  athena::interop::document_node lease;
  std::string snapshot;
};

void commit (PropertyRequest request, std::string desired, node_property_completion complete) {
  const auto actor= request.actor;
  const auto view= request.view;
  auto finish= [complete] (std::string error) {
    qt_post_to_main_thread ([complete, error= std::move (error)] { complete (error); });
  };
  auto continuation= actor_continuation_registry::instance ().store (
    [request= std::move (request), desired= std::move (desired), finish] {
      std::string error;
      try {
        const auto* context= current_scheme_execution_context ();
        if (!context || !context->editor || !context->actor)
          throw std::runtime_error ("The document editor has closed");
        if (context->actor->current_state ()->read_only)
          throw std::runtime_error ("The document is read-only");
        const auto expected= decode_node_property_snapshot (request.snapshot);
        const auto replacement= decode_node_property_snapshot (desired);
        if (expected.source_version != context->actor->current_state ()->storage_version ||
            replacement.source_version != expected.source_version)
          throw std::runtime_error ("The document storage version has changed");
        tree scope= context->editor->the_buffer ();
        auto where= context->actor->current_state ()->interop_nodes ().locate (scope, request.lease);
        auto prepared= model::prepare_property_replacement (scope, where,
          expected.header, replacement.header);
        if (!prepared.ok ()) error= prepared.diagnostics.front ().detail;
        else if (prepared.change) {
          context->editor->before_menu_action ();
          try { ::apply (scope, *prepared.change); }
          catch (...) { context->editor->cancel_menu_action (); throw; }
          context->editor->after_menu_action ();
        }
      }
      catch (const std::exception& failure) { error= failure.what (); }
      catch (const string& failure) { error.assign (failure.data (), N(failure)); }
      catch (...) { error= "Could not update node properties"; }
      finish (std::move (error));
    });
  if (!buffer_actor::submit_to (actor, actor_command_kind::run_native_continuation,
        view, ATHENA_NO_BLOB, ATHENA_NO_BLOB, SCHEME_CAPABILITY_BUFFER, continuation)) {
    actor_continuation_registry::instance ().discard (continuation);
    finish ("The document no longer accepts edits");
  }
}
} // namespace

std::string encode_node_property_snapshot (const tree& source, xml::xml_format_version version) {
  const string tag= is_atomic (source) ? string ("") : as_string (L(source));
  if (!is_atomic (source) && (L(source) <= TMSTRING || L(source) == UNKNOWN ||
      !existing_tree_label (tag) || as_tree_label (tag) != L(source)))
    throw std::invalid_argument ("Unregistered property snapshot node type");
  const char* stored;
  switch (version) {
  case xml::xml_format_version::v1: stored= "1"; break;
  case xml::xml_format_version::v2: stored= "2"; break;
  case xml::xml_format_version::v3: stored= "3"; break;
  default: throw std::invalid_argument ("Unsupported property source version");
  }
  tree carrier (TUPLE, "node-property-header", stored, tree (tag));
  node::copy_metadata (source, carrier);
  return xml::encode_native_xml_bytes (carrier,
    version == xml::xml_format_version::v3 ? version : xml::xml_format_version::v2,
    xml::xml_kind::fragment);
}

node_property_snapshot decode_node_property_snapshot (const std::string& bytes) {
  const auto decoded= xml::decode_native_xml_bytes (bytes, xml::xml_kind::fragment);
  const tree& carrier= decoded.document;
  if (!is_func (carrier, TUPLE, 3) || carrier[0] != "node-property-header" ||
      !is_atomic (carrier[1]) || !is_atomic (carrier[2]))
    throw std::invalid_argument ("Invalid node property snapshot");
  xml::xml_format_version version;
  if (carrier[1] == "1") version= xml::xml_format_version::v1;
  else if (carrier[1] == "2") version= xml::xml_format_version::v2;
  else if (carrier[1] == "3") version= xml::xml_format_version::v3;
  else throw std::invalid_argument ("Unsupported property source version");
  const auto transport= version == xml::xml_format_version::v3 ? version : xml::xml_format_version::v2;
  if (decoded.version != transport)
    throw std::invalid_argument ("Property snapshot version mismatch");
  for (int i=0; i<N(carrier); ++i)
    if (node::get (carrier[i]))
      throw std::invalid_argument ("Property snapshot descriptor has metadata");
  tree header ("");
  if (carrier[2] != "") {
    const tree_label label= as_tree_label (carrier[2]->label);
    if (label <= TMSTRING || label == UNKNOWN)
      throw std::invalid_argument ("Unregistered property snapshot node type");
    header= tree (label, 0);
  }
  node::copy_metadata (carrier, header);
  return {header, version};
}

QDialog* make_node_properties_dialog (std::string snapshot, bool canonical, bool writable,
                                      node_property_submit submit, QWidget* parent,
                                      std::shared_ptr<const athena::contracts::detached_environment> environment) {
  return new NodeDialog (std::move (snapshot), canonical, writable, std::move (submit),
                        parent, std::move (environment));
}

bool node_properties_show (tree source) {
  const auto* context= current_scheme_execution_context ();
  if (!context || !context->actor || !context->editor ||
      !context->has (SCHEME_CAPABILITY_BUFFER)) return false;
  try {
    path ip= obtain_ip (source);
    if (!ip_attached (ip)) return false;
    path root= context->editor->the_buffer_path (), absolute= reverse (ip);
    if (!(root <= absolute)) return false;
    if (!en::is_enunciation (source) &&
        en::standard_registry ().body_index (source) >= 0 &&
        context->actor->current_state ()->storage_version != xml::xml_format_version::v3 &&
        !context->actor->current_state ()->read_only) {
      en::conversion_options options;
      options.numbering_preferences["number solutions"]= get_preference ("number solutions") == "on";
      auto converted= en::convert_detached_source (source, options);
      if (!converted.diagnostics.empty ())
        throw std::runtime_error (converted.diagnostics.front ().detail);
      source= tree_set_diff (source, converted.source);
    }
    tree scope= context->editor->the_buffer ();
    path relative= absolute / root;
    if (!has_subtree (scope, relative) || !strong_equal (source, subtree (scope, relative))) return false;
    model::source_path where;
    for (path p= relative; !is_nil (p); p= p->next) where.push_back (p->item);
    PropertyRequest request {context->actor_id, context->view_id,
      context->actor->current_state ()->interop_nodes ().track (scope, where),
      encode_node_property_snapshot (source, context->actor->current_state ()->storage_version)};
    bool canonical= en::is_canonical (source) && model::validate_node_properties (source).empty ();
    bool writable= !context->actor->current_state ()->read_only;
    const auto prepared= context->editor->cursor_context_at (absolute * 0, {});
    if (writable && canonical && !prepared) {
      context->editor->set_message ("Node properties", "Source context is not ready", true);
      return false;
    }
    auto environment= prepared ? prepared->detach () : nullptr;
    qt_post_to_main_thread ([request= std::move (request), canonical, writable, environment] {
      try {
        auto* dialog= make_node_properties_dialog (request.snapshot, canonical, writable,
          [request] (std::string desired, node_property_completion complete) {
            commit (request, std::move (desired), std::move (complete));
          }, QApplication::activeWindow (), environment);
        dialog->setAttribute (Qt::WA_DeleteOnClose);
        dialog->setWindowModality (Qt::WindowModal);
        dialog->show ();
      }
      catch (const std::exception& failure) {
        QMessageBox::warning (QApplication::activeWindow (), "Node properties",
                              QString::fromUtf8 (failure.what ()));
      }
    });
    return true;
  }
  catch (const std::exception& failure) {
    context->editor->set_message ("Node properties", tree (string (failure.what ())), true);
    return false;
  }
}
