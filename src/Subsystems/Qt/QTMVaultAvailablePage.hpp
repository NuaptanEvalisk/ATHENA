/******************************************************************************
* MODULE     : QTMVaultAvailablePage.hpp
* DESCRIPTION: Wikilink targets from the current document and its transclusions
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "QTMVaultAvailableEnunciations.hpp"
#include "QTMVaultArtifactPage.hpp"
#include "ATHENA/Data/vault.hpp"
#include <QWizardPage>

class QComboBox;
class QProgressBar;
class QTimer;

class QTMVaultAvailablePage: public QWizardPage {
public:
  explicit QTMVaultAvailablePage (QWidget* parent= nullptr);
  ~QTMVaultAvailablePage () override;
  int nextId () const override { return -1; }
  void initializePage () override;
  void cleanupPage () override;
  bool isComplete () const override;
  bool validatePage () override;
  void setSelectionHandler (QTMVaultArtifactPage::SelectionHandler handler);
private:
  void filter ();
  void showPreview ();
  struct Task;
  std::shared_ptr<Task> task;
  const url source;
  const QString relative;
  const vault_context_handle context;
  QTMVaultArtifactPage::SelectionHandler selectionHandler;
  std::vector<AvailableEnunciation> entries;
  QLineEdit* query;
  QLineEdit* display;
  QComboBox* kind;
  QCheckBox* caseInsensitive;
  QCheckBox* fuzzy;
  QListWidget* list;
  QLabel* status;
  QLabel* previewTitle;
  QWidget* previewHost;
  QProgressBar* progress;
  QPushButton* stop;
  QTimer* timer;
  WikilinkPreview preview;
};
