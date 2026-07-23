/******************************************************************************
* MODULE     : QTMVaultArtifactPage.hpp
* DESCRIPTION: Shared artifact selector for vault link inserters
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTARTIFACTPAGE_HPP
#define QTMVAULTARTIFACTPAGE_HPP

#include "QTMVaultPreviewWidget.hpp"
#include "ATHENA/Data/artifacts.hpp"

#include <QWizardPage>
#include <functional>
#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QWidget;

enum class QTMVaultArtifactUsage {
  Wikilink,
  Transclusion
};

struct QTMVaultArtifactSelection {
  QString relative_path;
  QString upper_anchor;
  QString lower_anchor;
  QString display_text;
};

class QTMVaultArtifactPage : public QWizardPage {
public:
  using SelectionHandler=
    std::function<void (const QTMVaultArtifactSelection&)>;

  QTMVaultArtifactPage (QTMVaultArtifactUsage usage,
                        const char* casePreference,
                        const char* fuzzyPreference,
                        QWidget* parent= nullptr);

  int nextId () const override;
  void initializePage () override;
  bool validatePage () override;
  void showEvent (QShowEvent* event) override;

  void setSelectionHandler (SelectionHandler handler);

private:
  void loadRecords ();
  void runSearch ();
  void updatePreview ();
  bool resolveSelection (QTMVaultArtifactSelection& selection);

  QTMVaultArtifactUsage usage;
  const char* casePreference;
  const char* fuzzyPreference;
  SelectionHandler selectionHandler;
  QLineEdit* queryEdit;
  QCheckBox* caseInsensitiveCheck;
  QCheckBox* fuzzyCheck;
  QPushButton* searchButton;
  QLabel* statusLabel;
  QListWidget* resultList;
  QLabel* previewTitle;
  QWidget* previewHost;
  WikilinkPreview preview;
  std::vector<AthenaArtifactRecord> records;
  bool recordsLoaded;
  bool selectionAccepted;
};

#endif // QTMVAULTARTIFACTPAGE_HPP
