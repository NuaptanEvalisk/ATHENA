/******************************************************************************
* MODULE     : QTMVaultPreviewWidget.hpp
* DESCRIPTION: Embedded TeXmacs preview widget for vault link dialogs
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMVAULTPREVIEWWIDGET_HPP
#define QTMVAULTPREVIEWWIDGET_HPP

#include "QTMWidget.hpp"
#include "renderer.hpp"
#include "tree.hpp"
#include <QObject>
#include <QWidget>

class WikilinkPreview : public QObject {
public:
  WikilinkPreview (QObject* parent= nullptr);
  ~WikilinkPreview ();

  void destroyPreview ();
  QWidget* ensureCreated (QWidget* parent);
  void setBody (tree body);
  void refresh ();
  bool eventFilter (QObject* watched, QEvent* event) override;

private:
  void installPreviewEventFilter (QWidget* root);
  void connectPreviewScrollbars (QTMWidget* tmWidget);
  bool isPreviewWatchedObject (QObject* watched) const;
  void showFallbackPreview (QWidget* parent);
  SI currentPreviewWidth () const;
  double currentPreviewZoom () const;
  void recreatePreview ();
  void refreshLayoutNow ();
  void refreshLayout ();

  widget     previewWidget;
  tree       previewBody;
  QWidget*   previewParent;
  QWidget*   previewQtWidget;
  QTMWidget* previewTexmacsWidget;
  SI         previewWidth;
  double     previewZoom;
  bool       recreating;
};

#endif // QTMVAULTPREVIEWWIDGET_HPP
