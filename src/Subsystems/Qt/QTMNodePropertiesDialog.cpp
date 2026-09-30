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
#include "buffer_actor.hpp"
#include "buffer_state.hpp"
#include "editor.hpp"
#include "new_buffer.hpp"
#include "tm_window.hpp"
#include "qt_widget.hpp"
#include "qt_utilities.hpp"
#include "convert.hpp"
#include "native_interfaces.hpp"

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
  QLineEdit* line;
  QToolButton* edit;
public:
  explicit RichField (tree value, QWidget* parent= nullptr):
    QWidget (parent), original (copy (value)) {
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
      tree (WITH, "preamble", "true", tree (DOCUMENT, initial)), style, name);
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
  node_property_submit submit;
  QComboBox *kind= nullptr, *variant= nullptr;
  RichField* name= nullptr;
  QLineEdit *year= nullptr, *target= nullptr;
  QCheckBox* numbered= nullptr;
  QListWidget* authors= nullptr;
  std::vector<tree> authorValues;
  QLabel* error;
  QDialogButtonBox* buttons;
  bool applying= false;
public:
  NodeDialog (std::string snapshot, bool canonical, bool writable,
              node_property_submit commit, QWidget* parent):
    QDialog (parent), header (xml::read_xml_v2 (snapshot, xml::xml_kind::fragment)),
    submit (std::move (commit)) {
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
    name= new RichField (field<node::rich_text> (header, "name")->content, fields);
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
      for (const auto& value: *values) authorValues.push_back (copy (std::get<node::rich_text> (value.data).content));
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
    auto* text= new RichField (index < 0 ? tree ("") : authorValues[index], &dialog);
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
      p["name"]= node::property (node::rich_text {name->value ()});
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
      submit (xml::write_xml_v2 (changed, xml::xml_kind::fragment), [guard] (std::string message) {
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
        tree scope= context->editor->the_buffer ();
        auto where= context->actor->current_state ()->interop_nodes ().locate (scope, request.lease);
        auto prepared= model::prepare_property_replacement (scope, where,
          xml::read_xml_v2 (request.snapshot, xml::xml_kind::fragment),
          xml::read_xml_v2 (desired, xml::xml_kind::fragment));
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

QDialog* make_node_properties_dialog (std::string snapshot, bool canonical, bool writable,
                                      node_property_submit submit, QWidget* parent) {
  return new NodeDialog (std::move (snapshot), canonical, writable, std::move (submit), parent);
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
      xml::write_xml_v2 (model::property_header (source), xml::xml_kind::fragment)};
    bool canonical= en::is_canonical (source) && model::validate_node_properties (source).empty ();
    bool writable= !context->actor->current_state ()->read_only;
    qt_post_to_main_thread ([request= std::move (request), canonical, writable] {
      try {
        auto* dialog= make_node_properties_dialog (request.snapshot, canonical, writable,
          [request] (std::string desired, node_property_completion complete) {
            commit (request, std::move (desired), std::move (complete));
          }, QApplication::activeWindow ());
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
