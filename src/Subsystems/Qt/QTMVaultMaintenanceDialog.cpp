/******************************************************************************
* MODULE     : QTMVaultMaintenanceDialog.cpp
* DESCRIPTION: Configured vault maintenance pass selection
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
*******************************************************************************/

#include "QTMVaultMaintenanceDialog.hpp"

#include "ATHENA/Data/vault_maintenance.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

#include <algorithm>
#include <string>
#include <vector>

namespace {

class VaultMaintenanceSetupDialog: public QDialog {
public:
  explicit VaultMaintenanceSetupDialog (
    const std::vector<VaultMaintenancePlanEntry>& plan, QWidget* parent)
    : QDialog (parent), entries (plan) {
    setWindowTitle ("Maintenance Setup");
    setModal (true);
    setMinimumWidth (720);
    setMaximumWidth (720);

    QVBoxLayout* layout= new QVBoxLayout (this);
    QLabel* explanation= new QLabel (
      "Select the maintenance passes to run. Passes execute in the order "
      "shown.", this);
    explanation->setWordWrap (true);
    layout->addWidget (explanation);

    table= new QTableWidget (int (entries.size ()), 3, this);
    table->setHorizontalHeaderLabels ({"Order", "Maintenance pass", "Run"});
    table->verticalHeader ()->hide ();
    table->setSelectionMode (QAbstractItemView::NoSelection);
    table->setEditTriggers (QAbstractItemView::NoEditTriggers);
    table->setAlternatingRowColors (true);
    table->setShowGrid (true);
    table->verticalHeader ()->setSectionResizeMode (QHeaderView::Fixed);
    table->horizontalHeader ()->setSectionResizeMode (
      0, QHeaderView::ResizeToContents);
    table->horizontalHeader ()->setSectionResizeMode (
      1, QHeaderView::Stretch);
    table->horizontalHeader ()->setSectionResizeMode (
      2, QHeaderView::ResizeToContents);
    table->horizontalHeader ()->setStretchLastSection (false);

    checks.reserve (entries.size ());
    for (size_t i=0; i<entries.size (); ++i) {
      QTableWidgetItem* order= new QTableWidgetItem (
        QString::number (int (i + 1)));
      order->setTextAlignment (Qt::AlignCenter);
      table->setItem (int (i), 0, order);
      table->setItem (
        int (i), 1,
        new QTableWidgetItem (
          QString::fromStdString (entries[i].description)));

      QWidget* cell= new QWidget (table);
      QHBoxLayout* cellLayout= new QHBoxLayout (cell);
      cellLayout->setContentsMargins (0, 0, 0, 0);
      cellLayout->setAlignment (Qt::AlignCenter);
      QCheckBox* check= new QCheckBox (cell);
      check->setChecked (entries[i].selected_by_default);
      check->setAccessibleName (
        "Run " + QString::fromStdString (entries[i].description));
      cellLayout->addWidget (check);
      table->setCellWidget (int (i), 2, cell);
      checks.push_back (check);
    }
    int rowHeight= std::max (
      table->fontMetrics ().height () + 8,
      checks.empty () ? 0 : checks.front ()->sizeHint ().height () + 4);
    table->verticalHeader ()->setMinimumSectionSize (rowHeight);
    table->verticalHeader ()->setDefaultSectionSize (rowHeight);
    int rowsHeight= table->horizontalHeader ()->height () +
                    table->frameWidth () * 2 +
                    table->rowCount () * rowHeight;
    table->setMinimumHeight (std::min (rowsHeight, 460));
    table->setMaximumHeight (460);
    layout->addWidget (table);

    QDialogButtonBox* buttons= new QDialogButtonBox (
      QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    buttons->button (QDialogButtonBox::Ok)->setText ("Run maintenance");
    buttons->button (QDialogButtonBox::Ok)->setDefault (true);
    connect (buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect (buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget (buttons);
  }

  std::vector<std::string> skipped_passes () const {
    std::vector<std::string> skipped;
    for (size_t i=0; i<entries.size (); ++i)
      if (!checks[i]->isChecked ()) skipped.push_back (entries[i].id);
    return skipped;
  }

  std::vector<std::string> enabled_passes () const {
    std::vector<std::string> enabled;
    for (size_t i=0; i<entries.size (); ++i)
      if (checks[i]->isChecked ()) enabled.push_back (entries[i].id);
    return enabled;
  }

private:
  std::vector<VaultMaintenancePlanEntry> entries;
  std::vector<QCheckBox*> checks;
  QTableWidget* table;
};

} // namespace

tree
qtm_vault_maintenance_setup (string vault_root) {
  std::vector<VaultMaintenancePlanEntry> plan;
  std::string error;
  if (!vault_maintenance_plan (vault_root, plan, error)) {
    QMessageBox::critical (
      QApplication::activeWindow (), "Maintenance Setup",
      QString::fromStdString (
        error.empty () ? "Could not create the maintenance plan." : error));
    return UNINIT;
  }

  VaultMaintenanceSetupDialog dialog (
    plan, QApplication::activeWindow ());
  if (dialog.exec () != QDialog::Accepted) return UNINIT;

  tree result (TUPLE);
  result << tree ("accepted");
  tree skipped (TUPLE), enabled (TUPLE);
  for (const std::string& id: dialog.skipped_passes ())
    skipped << tree (id.c_str ());
  for (const std::string& id: dialog.enabled_passes ())
    enabled << tree (id.c_str ());
  result << skipped << enabled;
  return result;
}
