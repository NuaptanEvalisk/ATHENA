/******************************************************************************
* MODULE     : QTMMaterialCitationDialog.cpp
* DESCRIPTION: Materials citation inserter
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
******************************************************************************/

#include "QTMMaterialCitationDialog.hpp"

#include "ATHENA/Data/materials_engine.hpp"
#include "ATHENA/Data/materials_document.hpp"
#include "ATHENA/Data/vault.hpp"
#include "QTMVaultLinkFocus.hpp"
#include "scheme.hpp"
#include "new_buffer.hpp"
#include "qt_utilities.hpp"
#include "actor_transport.hpp"
#include "buffer_actor.hpp"
#include "guile_tm.hpp"
#include "object.hpp"
#include "scheme_execution_context.hpp"

#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QThread>
#include <QPushButton>
#include <QTableWidget>
#include <QTextDocument>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>

#include <algorithm>

namespace {

QString qstr (const std::string& value) {
  return QString::fromUtf8 (value.data (), (qsizetype) value.size ());
}

std::string stdstr (const QString& value) {
  QByteArray bytes= value.toUtf8 ();
  return std::string (bytes.constData (), (size_t) bytes.size ());
}

std::string plain_html (const std::string& html) {
  QTextDocument document;
  document.setHtml (qstr (html));
  return stdstr (document.toPlainText ());
}

class CitationDialog: public QDialog {
public:
  CitationDialog (QWidget* parent, bool with_locator,
                   const std::vector<inherited_material>& inherited)
    : QDialog (parent), allow_empty_selection (!with_locator) {
    setWindowTitle (with_locator ? "Insert Material citation"
                                 : "Select referenced Materials");
    resize (820, 560);
    QVBoxLayout* outer= new QVBoxLayout (this);
    QHBoxLayout* search_row= new QHBoxLayout;
    QLabel* search_label= new QLabel ("&Search:", this);
    search= new QLineEdit (this);
    search->setObjectName ("materialCitationSearch");
    search->setPlaceholderText ("Search title, creator, identifier, or tag");
    search_label->setBuddy (search);
    search_row->addWidget (search_label);
    search_row->addWidget (search, 1);
    outer->addLayout (search_row);
    table= new QTableWidget (0, 4, this);
    table->setObjectName ("materialCitationResults");
    table->setHorizontalHeaderLabels ({"Type", "Creator", "Title", "Date"});
    table->setSelectionBehavior (QAbstractItemView::SelectRows);
    table->setSelectionMode (QAbstractItemView::ExtendedSelection);
    table->setEditTriggers (QAbstractItemView::NoEditTriggers);
    table->verticalHeader ()->hide ();
    table->horizontalHeader ()->setSectionResizeMode (0, QHeaderView::Interactive);
    table->horizontalHeader ()->setSectionResizeMode (1, QHeaderView::Interactive);
    table->horizontalHeader ()->setSectionResizeMode (2, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (3, QHeaderView::Interactive);
    const int em= table->fontMetrics ().horizontalAdvance ("M");
    table->setColumnWidth (0, 8 * em);
    table->setColumnWidth (1, 16 * em);
    table->setColumnWidth (3, 12 * em);
    outer->addWidget (table, 1);
    if (!inherited.empty ()) {
      outer->addWidget (new QLabel ("Inherited from namespaces", this));
      auto* inherited_table= new QTableWidget (int (inherited.size ()), 2, this);
      inherited_table->setObjectName ("inheritedMaterialReferences");
      inherited_table->setHorizontalHeaderLabels ({"Material", "Namespace"});
      inherited_table->setEditTriggers (QAbstractItemView::NoEditTriggers);
      inherited_table->setSelectionMode (QAbstractItemView::NoSelection);
      inherited_table->verticalHeader ()->hide ();
      inherited_table->horizontalHeader ()->setSectionResizeMode (QHeaderView::Stretch);
      inherited_table->setMaximumHeight (160);
      MaterialsStore* store= vault_get_materials_store ();
      for (size_t i=0; i<inherited.size (); ++i) {
        const auto& item= inherited[i];
        std::string error;
        auto record= store ? store->get (item.uuid, error) : std::optional<MaterialRecord> ();
        QString title= record ? qstr (record->field ("title")) : qstr (item.uuid);
        auto* material= new QTableWidgetItem (title);
        material->setToolTip (qstr (item.uuid));
        inherited_table->setItem (int (i), 0, material);
        inherited_table->setItem (int (i), 1, new QTableWidgetItem (
          to_qstring (string (item.namespace_name.data (), item.namespace_name.size ()))));
      }
      outer->addWidget (inherited_table);
    }
    locator_type= new QComboBox (this);
    locator_value= new QLineEdit (this);
    if (with_locator) {
      QFormLayout* locator_form= new QFormLayout;
      locator_type->addItems ({"", "page", "chapter", "section", "paragraph",
                               "figure", "table", "volume", "issue", "other"});
      locator_value->setPlaceholderText ("For example: 42-45");
      locator_form->addRow ("Locator &type:", locator_type);
      locator_form->addRow ("&Locator:", locator_value);
      outer->addLayout (locator_form);
    }
    else {
      locator_type->hide ();
      locator_value->hide ();
    }
    QLabel* hint= new QLabel (
      with_locator
        ? "Select one or more Materials. A locator applies to the selected citation cluster."
        : "Optionally select Materials to include in addition to those cited "
          "by this document. Space or Ctrl+click toggles a row.", this);
    hint->setWordWrap (true);
    outer->addWidget (hint);
    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    accept_button= buttons->button (QDialogButtonBox::Ok);
    accept_button->setText (
      with_locator ? "Insert" : "Select");
    accept_button->setDefault (true);
    if (!with_locator) {
      QPushButton* clear_button= buttons->addButton (
        "&Clear selection", QDialogButtonBox::ResetRole);
      connect (clear_button, &QPushButton::clicked, table,
               &QTableWidget::clearSelection);
    }
    outer->addWidget (buttons);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    connect (table, &QTableWidget::cellDoubleClicked, this,
             [this] { accept (); });
    connect (search, &QLineEdit::textChanged, this, [this] { reload (); });
    connect (table, &QTableWidget::itemSelectionChanged, this,
             [this] { updateAcceptState (); });
    search->installEventFilter (this);
    table->installEventFilter (this);
    setTabOrder (search, table);
    if (with_locator) {
      setTabOrder (table, locator_type);
      setTabOrder (locator_type, locator_value);
      setTabOrder (locator_value, accept_button);
    }
    else setTabOrder (table, accept_button);
    reload ();
    search->setFocus (Qt::OtherFocusReason);
  }

  void reload () {
    MaterialsStore* store= vault_get_materials_store ();
    if (store == nullptr) return;
    std::string error;
    std::vector<MaterialSearchHit> hits= search->text ().trimmed ().isEmpty ()
      ? store->list (100000, 0, error)
      : store->search (stdstr (search->text ().trimmed ()), 100000, error);
    table->setRowCount (0);
    for (const MaterialSearchHit& hit: hits) {
      int row= table->rowCount ();
      table->insertRow (row);
      QStringList values= {qstr (hit.item_type), qstr (hit.creators),
                           qstr (hit.title), qstr (hit.issued)};
      for (int column=0; column<4; ++column) {
        QTableWidgetItem* item= new QTableWidgetItem (values[column]);
        item->setToolTip (values[column]);
        item->setData (Qt::UserRole, qstr (hit.uuid));
        table->setItem (row, column, item);
      }
    }
    if (!allow_empty_selection && table->rowCount () > 0) {
      table->setCurrentCell (0, 0);
      table->selectRow (0);
    }
    updateAcceptState ();
    if (!error.empty ()) QMessageBox::warning (this, "Materials", qstr (error));
  }

  std::vector<std::string> selected_uuids () const {
    std::vector<std::pair<int,std::string>> rows;
    for (QTableWidgetItem* item: table->selectedItems ())
      if (item->column () == 0)
        rows.push_back ({item->row (), stdstr (item->data (Qt::UserRole).toString ())});
    std::sort (rows.begin (), rows.end ());
    std::vector<std::string> result;
    for (const auto& row: rows) result.push_back (row.second);
    return result;
  }

  std::string locatorType () const { return stdstr (locator_type->currentText ()); }
  std::string locatorValue () const { return stdstr (locator_value->text ().trimmed ()); }

protected:
  bool eventFilter (QObject* watched, QEvent* event) override {
    if ((watched == search || watched == table) &&
        event->type () == QEvent::KeyPress) {
      QKeyEvent* key= static_cast<QKeyEvent*> (event);
      if (watched == search &&
          (key->key () == Qt::Key_Up || key->key () == Qt::Key_Down)) {
        moveSelection (key->key () == Qt::Key_Up ? -1 : 1);
        return true;
      }
      if (watched == search &&
          (key->key () == Qt::Key_PageUp ||
           key->key () == Qt::Key_PageDown)) {
        moveSelectionPage (key->key () == Qt::Key_PageUp ? -1 : 1);
        return true;
      }
      if (key->key () == Qt::Key_Return || key->key () == Qt::Key_Enter) {
        if (allow_empty_selection || !selected_uuids ().empty ()) accept ();
        return true;
      }
      if (watched == table && key->key () == Qt::Key_Space) {
        toggleCurrentRow ();
        return true;
      }
      if (key->key () == Qt::Key_Escape) {
        reject ();
        return true;
      }
    }
    return QDialog::eventFilter (watched, event);
  }

private:
  void moveSelection (int delta) {
    int count= table->rowCount ();
    if (count <= 0) return;
    int row= table->currentRow ();
    if (row < 0) row= delta > 0 ? -1 : 0;
    row= std::clamp (row + delta, 0, count - 1);
    table->setCurrentCell (row, 0);
    table->selectRow (row);
    table->scrollToItem (table->item (row, 0));
  }

  void moveSelectionPage (int direction) {
    int count= table->rowCount ();
    if (count <= 0) return;
    int row= table->currentRow ();
    if (row < 0) row= direction > 0 ? 0 : count - 1;
    int row_height= table->rowHeight (row);
    if (row_height <= 0) row_height= table->fontMetrics ().height ();
    int rows= std::max (1, table->viewport ()->height () /
                           std::max (1, row_height) - 1);
    row= std::clamp (row + direction * rows, 0, count - 1);
    table->setCurrentCell (row, 0);
    table->selectRow (row);
    table->scrollToItem (table->item (row, 0),
                         QAbstractItemView::PositionAtCenter);
  }

  void updateAcceptState () {
    accept_button->setEnabled (allow_empty_selection ||
                               !selected_uuids ().empty ());
  }

  void toggleCurrentRow () {
    int row= table->currentRow ();
    if (row < 0 || row >= table->rowCount ()) return;
    bool selected= table->item (row, 0)->isSelected ();
    for (int column=0; column<table->columnCount (); ++column)
      table->item (row, column)->setSelected (!selected);
    updateAcceptState ();
  }

  bool allow_empty_selection;
  QLineEdit* search;
  QTableWidget* table;
  QComboBox* locator_type;
  QLineEdit* locator_value;
  QPushButton* accept_button;
};

tree
choose_reference_uuids (bool with_locator, std::string* locator_type,
                        std::string* locator_value, bool document_context= false,
                        url origin= url_none ()) {
  QWidget* parent= QApplication::activeWindow ();
  if (!vault_active ()) {
    QMessageBox::warning (parent, "Materials",
                          "Load a vault before selecting Materials.");
    return UNINIT;
  }
  if (vault_get_materials_store () == nullptr) {
    QMessageBox::critical (
      parent, "Materials",
      "The active vault's Materials database is not available. Reload the "
      "vault and check the Materials database path in Vaultfile.json.");
    return UNINIT;
  }
  TeXmacsFocusSnapshot focus= capture_texmacs_focus_snapshot ();
  std::string error;
  auto inherited= document_context ? athena_materials_inherited (
    vault_capture_context (), is_none (origin) ? get_current_buffer_safe () : origin, error) : std::vector<inherited_material> {};
  if (!error.empty ()) { QMessageBox::warning (parent, "Materials", qstr (error)); return UNINIT; }
  CitationDialog dialog (parent, with_locator, inherited);
  if (dialog.exec () != QDialog::Accepted) {
    restore_texmacs_focus_snapshot_later (focus);
    return UNINIT;
  }
  std::vector<std::string> uuids= dialog.selected_uuids ();
  if (uuids.empty () && with_locator) {
    restore_texmacs_focus_snapshot_later (focus);
    return UNINIT;
  }
  tree result (TUPLE);
  for (const std::string& uuid: uuids) result << tree (uuid.c_str ());
  if (locator_type != nullptr) *locator_type= dialog.locatorType ();
  if (locator_value != nullptr) *locator_value= dialog.locatorValue ();
  restore_texmacs_focus_snapshot_later (focus);
  return result;
}

std::string
material_uri (const std::string& uuid, const std::string& locator_type,
              const std::string& locator_value) {
  QUrl uri;
  uri.setScheme ("tmfs");
  uri.setHost ("material");
  uri.setPath ("/" + qstr (uuid));
  QUrlQuery query;
  if (!locator_type.empty ()) query.addQueryItem ("locator", qstr (locator_type));
  if (!locator_value.empty ()) query.addQueryItem ("value", qstr (locator_value));
  uri.setQuery (query);
  return stdstr (uri.toString (QUrl::FullyEncoded));
}

} // namespace

tree
qtm_material_choose_citation (const std::string& csl_style) {
  std::string locator_type;
  std::string locator_value;
  tree chosen= choose_reference_uuids (true, &locator_type, &locator_value);
  if (!is_func (chosen, TUPLE) || N(chosen) == 0) return UNINIT;
  MaterialsStore* store= vault_get_materials_store ();
  std::vector<MaterialRecord> records;
  MaterialCitationCluster cluster;
  std::string error;
  for (int i=0; i<N(chosen); ++i) {
    std::string uuid (as_charp (chosen[i]->label), N(chosen[i]->label));
    std::optional<MaterialRecord> record= store->get (uuid, error);
    if (!record) {
      QMessageBox::warning (QApplication::activeWindow (), "Insert citation", qstr (error));
      return UNINIT;
    }
    records.push_back (*record);
    cluster.items.push_back ({record->uuid, locator_type, locator_value, false});
  }
  MaterialRenderedDocument rendered;
  if (!athena_materials_render (
        records, {cluster}, {},
        csl_style.empty () ? "springer-mathphys" : csl_style,
        rendered, error) ||
      rendered.citation_html.empty ()) {
    QMessageBox::warning (QApplication::activeWindow (), "Insert citation", qstr (error));
    return UNINIT;
  }
  tree items (TUPLE);
  for (const MaterialCitationItem& item: cluster.items)
    items << compound ("material-cite-item", tree (item.uuid.c_str ()),
                       tree (item.locator_type.c_str ()),
                       tree (item.locator_value.c_str ()));
  tree result (TUPLE);
  result << items << tree (plain_html (rendered.citation_html[0]).c_str ());
  result << tree (cluster.items.size () == 1
    ? material_uri (cluster.items[0].uuid, locator_type, locator_value).c_str ()
    : "");
  return result;
}

tree
qtm_material_choose_references (bool document_context) {
  return choose_reference_uuids (false, nullptr, nullptr, document_context);
}

namespace {
struct MaterialChooserRequest {
  athena_actor_id actor;
  athena_view_id view;
  SchemeCapabilitySet capabilities;
  athena_scheme_handle_id completion= ATHENA_NO_SCHEME_HANDLE;
  vault_context_handle vault;
  std::string style, origin;
  bool citation;
  ~MaterialChooserRequest () { scheme_command_handle_release (completion); }
};

void choose_async (bool citation, string style, object completion) {
  auto* app= QCoreApplication::instance ();
  if (!app) return;
  const auto* context= current_scheme_execution_context ();
  if (!context || context->actor_id == ATHENA_NO_ACTOR || context->view_id == ATHENA_NO_VIEW) {
    if (QThread::currentThread () == app->thread ())
      (void) call (completion, object (citation ?
        qtm_material_choose_citation (std::string (style.data (), N(style))) : qtm_material_choose_references ()));
    return;
  }
  auto request= std::make_shared<MaterialChooserRequest> ();
  request->actor= context->actor_id;
  request->view= context->view_id;
  request->capabilities= context->capabilities;
  request->vault= vault_capture_context ();
  request->citation= citation;
  request->style.assign (style.data (), N(style));
  string origin= as_string (get_current_buffer_safe ());
  request->origin.assign (origin.data (), N(origin));
  request->completion= scheme_command_handle_acquire (object_to_tmscm (completion));
  QMetaObject::invokeMethod (app, [request] {
    tree result= UNINIT;
    if (vault_context_is_current (request->vault)) {
      result= request->citation ? qtm_material_choose_citation (request->style) :
        choose_reference_uuids (false, nullptr, nullptr, true,
          url (string (request->origin.data (), request->origin.size ())));
      if (!vault_context_is_current (request->vault)) result= UNINIT;
    }
    auto result_id= actor_tree_registry::instance ().store (std::move (result));
    auto ticket= buffer_actor::submit_to (request->actor,
      actor_command_kind::invoke_scheme_handle_tree, request->view, result_id,
      ATHENA_NO_BLOB, request->capabilities, request->completion);
    if (ticket) request->completion= ATHENA_NO_SCHEME_HANDLE;
    else actor_tree_registry::instance ().discard (result_id);
  }, Qt::QueuedConnection);
}
}

void qtm_material_choose_citation_async (string style, object completion) {
  choose_async (true, std::move (style), std::move (completion));
}
void qtm_material_choose_references_async (object completion) {
  choose_async (false, "", std::move (completion));
}
