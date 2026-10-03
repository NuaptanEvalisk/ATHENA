/******************************************************************************
* MODULE     : QTMCompoundDocument.hpp
* DESCRIPTION: Virtualized compound-document viewport over real source views
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/
#pragma once
#include "QTMWindow.hpp"
#include "ATHENA/Data/compound_virtual_document.hpp"
#include <memory>
#include <array>

class QTMCompoundViewport;
class QTMEditorToolbarPresenter;
class QTMToolbarController;
class QToolBar;
void compound_document_open (url filename);
void compound_document_create ();

class QTMCompoundDocument: public QTMWindow {
  std::array<QToolBar*, 4> toolbars_ {};
  std::array<std::unique_ptr<QTMEditorToolbarPresenter>, 4> toolbar_presenters_;
  std::unique_ptr<QTMToolbarController> toolbar_controller_;
  std::unique_ptr<QTMCompoundViewport> viewport_;
public:
  QTMCompoundDocument (QString filename, athena::avd::descriptor_file,
                        std::vector<athena::avd::member>);
  ~QTMCompoundDocument () override;
  const QString& filename () const;
  void goToMember (std::size_t index, bool atEnd= false);
  void activateMember (QTMWidget* canvas);
  void refreshToolbars ();
};
