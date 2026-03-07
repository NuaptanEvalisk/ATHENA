/******************************************************************************
* MODULE     : QTMVaultChooser.hpp
* DESCRIPTION: Qt vault chooser for Wikilinks
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTCHOOSER_HPP
#define QTMVAULTCHOOSER_HPP

#include <QDialog>
#include <QLineEdit>
#include <QListWidget>
#include <QVBoxLayout>
#include <QLabel>
#include "tree.hpp"
#include "url.hpp"
#include "vault.hpp"

class QTMVaultChooser : public QDialog {
  Q_OBJECT

public:
  enum State { SELECT_FILE, SELECT_ANCHOR, SELECT_DISPLAY_TEXT,
               SELECT_ANCHOR_BEGIN, SELECT_ANCHOR_END };

  QTMVaultChooser (QWidget* parent = nullptr, bool transcludeMode = false);
  ~QTMVaultChooser ();

  tree getResult ();

private slots:
  void onTextChanged (const QString& text);
  void onReturnPressed ();
  void onItemDoubleClicked (QListWidgetItem* item);

protected:
  virtual void keyPressEvent (QKeyEvent* event) override;

private:
  void updateList ();
  void filterFiles (const QString& hint);
  void filterAnchors (const QString& hint);
  bool fuzzyMatch (const QString& str, const QString& hint);

  QVBoxLayout* layout;
  QLabel*      prompt;
  QLineEdit*   searchEdit;
  QListWidget* resultList;

  State        state;
  array<url>   allFiles;
  strings      allAnchors;
  
  // Results
  QString      selectedRelPath;
  QString      selectedAnchor;
  QString      selectedAnchorBegin;
  QString      selectedAnchorEnd;
  QString      fileHint;
  QString      anchorHint;
  QString      displayText;
  
  bool         transcludeMode;
  bool         selectionChangedByArrows;
  bool         resultAccepted;
};

// Glue function
tree vault_choose_link (bool transcludeMode = false);

#endif // QTMVAULTCHOOSER_HPP
