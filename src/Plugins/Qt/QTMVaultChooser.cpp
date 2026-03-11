/******************************************************************************
* MODULE     : QTMVaultChooser.cpp
* DESCRIPTION: Qt vault chooser for Wikilinks
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#include "QTMVaultChooser.hpp"
#include "vault.hpp"
#include "qt_utilities.hpp"
#include "qt_gui.hpp"
#include <QKeyEvent>
#include <QApplication>
#include <algorithm>

QTMVaultChooser::QTMVaultChooser (QWidget* parent, bool transcludeMode2)
  : QDialog (parent), state (SELECT_FILE), transcludeMode (transcludeMode2),
    selectionChangedByArrows (false), resultAccepted (false)
{
  setWindowTitle ("Wikilink Chooser");
  resize (600, 400);

  layout = new QVBoxLayout (this);
  prompt = new QLabel ("Search File:", this);
  layout->addWidget (prompt);

  searchEdit = new QLineEdit (this);
  layout->addWidget (searchEdit);

  resultList = new QListWidget (this);
  layout->addWidget (resultList);

  allFiles = vault_get_all_files ();
  
  // Sort files by mtime initially?
  // User asked for "recently visited first manner, if possible"
  // For now let's just use what vault_get_all_files returns.
  
  updateList ();

  connect (searchEdit, SIGNAL (textChanged (const QString&)), this, SLOT (onTextChanged (const QString&)));
  connect (searchEdit, SIGNAL (returnPressed ()), this, SLOT (onReturnPressed ()));
  connect (resultList, SIGNAL (itemDoubleClicked (QListWidgetItem*)), this, SLOT (onItemDoubleClicked (QListWidgetItem*)));
  
  searchEdit->setFocus ();
}

QTMVaultChooser::~QTMVaultChooser () {}

void
QTMVaultChooser::keyPressEvent (QKeyEvent* event) {
  if (event->key () == Qt::Key_Up || event->key () == Qt::Key_Down) {
    selectionChangedByArrows = true;
    QApplication::sendEvent (resultList, event);
    return;
  }
  QDialog::keyPressEvent (event);
}

bool
QTMVaultChooser::fuzzyMatch (const QString& str, const QString& hint) {
  if (hint.isEmpty()) return true;
  int hintIdx = 0;
  QString lStr = str.toLower();
  QString lHint = hint.toLower();
  for (int i = 0; i < lStr.length() && hintIdx < lHint.length(); ++i) {
    if (lStr[i] == lHint[hintIdx]) {
      hintIdx++;
    }
  }
  return hintIdx == lHint.length();
}

void
QTMVaultChooser::updateList () {
  resultList->clear ();
  QString hint = searchEdit->text ();
  
  if (state == SELECT_FILE) {
    filterFiles (hint);
  } else if (state == SELECT_ANCHOR || state == SELECT_ANCHOR_BEGIN || state == SELECT_ANCHOR_END) {
    filterAnchors (hint);
  }
  
  if (resultList->count () > 0 && state == SELECT_FILE) {
    resultList->setCurrentRow (0);
  } else {
    resultList->setCurrentItem (nullptr);
  }
}

void
QTMVaultChooser::filterFiles (const QString& hint) {
  url root = vault_get_root ();
  for (int i = 0; i < N(allFiles); i++) {
    url rel = delta (root * url (""), allFiles[i]);
    string s = as_unix_string (rel);
    string suf = suffix (rel);
    if (suf == "tm" && N(s) > 3) s = s (0, N(s) - 3);
    else if (suf == "ath" && N(s) > 4) s = s (0, N(s) - 4);
    QString relNoExt = to_qstring (s);
    if (fuzzyMatch (relNoExt, hint)) {
      resultList->addItem (to_qstring (as_unix_string (rel)));
    }
  }
}

void
QTMVaultChooser::filterAnchors (const QString& hint) {
  for (int i = 0; i < N(allAnchors); i++) {
    QString a = to_qstring (allAnchors[i]);
    if (fuzzyMatch (a, hint)) {
      resultList->addItem (a);
    }
  }
}

void
QTMVaultChooser::onTextChanged (const QString& text) {
  updateList ();
}

void
QTMVaultChooser::onReturnPressed () {
  if (state == SELECT_FILE) {
    QListWidgetItem* item = resultList->currentItem ();
    if (item) {
      selectedRelPath = item->text ();
      if (selectionChangedByArrows) {
        QString s = selectedRelPath;
        if (s.endsWith (".tm")) s.chop (3);
        else if (s.endsWith (".ath")) s.chop (4);
        fileHint = s;
      } else {
        fileHint = searchEdit->text ();
      }
    } else return;
    
    url absUrl = vault_get_root () * url_unix (from_qstring (selectedRelPath));
    allAnchors = vault_get_anchors (absUrl);

    if (transcludeMode) {
      state = SELECT_ANCHOR_BEGIN;
      prompt->setText ("Search BEGIN Anchor:");
    } else {
      state = SELECT_ANCHOR;
      prompt->setText ("Search Anchor (optional):");
    }
    selectionChangedByArrows = false;
    searchEdit->clear ();
    updateList ();
    
  } else if (state == SELECT_ANCHOR) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchor = item->text ();
      anchorHint = selectedAnchor;
    } else {
      selectedAnchor = (item ? item->text () : "");
      anchorHint = searchEdit->text ();
    }
    
    state = SELECT_DISPLAY_TEXT;
    selectionChangedByArrows = false;
    prompt->setText ("Enter Display Text:");
    searchEdit->clear ();
    resultList->hide ();
    if (!selectedAnchor.isEmpty()) {
      searchEdit->setText (selectedAnchor);
    } else {
      QString s = selectedRelPath;
      if (s.endsWith (".tm")) s.chop (3);
      else if (s.endsWith (".ath")) s.chop (4);
      searchEdit->setText (s);
    }
    searchEdit->selectAll ();
    
  } else if (state == SELECT_ANCHOR_BEGIN) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchorBegin = item->text ();
    } else {
      selectedAnchorBegin = (item ? item->text () : "");
    }
    
    state = SELECT_ANCHOR_END;
    prompt->setText ("Search END Anchor:");
    searchEdit->clear ();
    updateList ();

  } else if (state == SELECT_ANCHOR_END) {
    QListWidgetItem* item = resultList->currentItem ();
    if (selectionChangedByArrows && item) {
      selectedAnchorEnd = item->text ();
      anchorHint = selectedAnchorEnd; 
    } else {
      selectedAnchorEnd = (item ? item->text () : "");
      anchorHint = searchEdit->text ();
    }
    
    resultAccepted = true;
    accept ();

  } else if (state == SELECT_DISPLAY_TEXT) {
    displayText = searchEdit->text ();
    resultAccepted = true;
    accept ();
  }
}

void
QTMVaultChooser::onItemDoubleClicked (QListWidgetItem* item) {
  onReturnPressed ();
}

tree
QTMVaultChooser::getResult () {
  if (!resultAccepted) return UNINIT;
  tree res (TUPLE);
  res << tree (from_qstring (selectedRelPath));
  if (transcludeMode) {
    res << tree (from_qstring (selectedAnchorBegin));
    res << tree (from_qstring (selectedAnchorEnd));
  } else {
    res << tree (from_qstring (selectedAnchor));
  }
  res << tree (from_qstring (fileHint));
  res << tree (from_qstring (anchorHint)); // anchorHint is the search string
  if (!transcludeMode) {
    res << tree (from_qstring (displayText));
  }
  return res;
}

tree
vault_choose_link (bool transcludeMode) {
  QTMVaultChooser chooser (NULL, transcludeMode);
  if (chooser.exec () == QDialog::Accepted) {
    return chooser.getResult ();
  }
  return UNINIT;
}
