/******************************************************************************
* MODULE     : QTMCommandRegistryInternal.hpp
* DESCRIPTION: Internal helpers shared by native command registry units
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/

#ifndef QTMCOMMANDREGISTRYINTERNAL_HPP
#define QTMCOMMANDREGISTRYINTERNAL_HPP

#include "QTMCommandRegistry.hpp"
#include "QTMWidget.hpp"
#include "QTMWindow.hpp"
#include "native_editor_actions.hpp"
#include "qt_actor_widget.hpp"
#include "file.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QFileDialog>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QTextEdit>

namespace qtm_command_registry_detail {

inline QTMCommandState
enabled_application_command () {
  QTMCommandState state;
  state.available= true;
  state.enabled= true;
  return state;
}

inline QTMCommandDynamicItem
enabled_dynamic_item (QString key, QString label, QString help= QString ()) {
  QTMCommandDynamicItem item;
  item.key= std::move (key);
  item.label= std::move (label);
  item.help= std::move (help);
  item.state.available= true;
  item.state.enabled= true;
  return item;
}

inline QVector<QString>
scheme_string_vector (object value) {
  QVector<QString> result;
  list<string> values= as_list_string (value);
  for (list<string> it= values; !is_nil (it); it= it->next)
    result.append (to_qstring (it->item));
  return result;
}

inline array<string>
scheme_string_array (object value) {
  array<string> result;
  list<string> values= as_list_string (value);
  for (list<string> it= values; !is_nil (it); it= it->next)
    result << it->item;
  return result;
}

inline list<string>
array_string_list (const array<string>& values, int start= 0) {
  list<string> result;
  for (int i=N(values)-1; i>=start; --i)
    result= list<string> (values[i], result);
  return result;
}

inline QString
provider_format_suffix (const QString& format) {
  try {
    object value= call ("format-default-suffix", object (from_qstring (format)));
    if (is_string (value)) return to_qstring (as_string (value));
  }
  catch (...) {}
  return QString ();
}

inline QString
provider_file_filter (const QString& format, const QString& suffix) {
  if (suffix.isEmpty ()) return QObject::tr ("All files (*)");
  QString display= format;
  if (!display.isEmpty ()) display[0]= display[0].toUpper ();
  return QObject::tr ("%1 files (*.%2);;All files (*)").arg (display, suffix);
}

inline QTMWidget*
editor_canvas_for_context (const QTMCommandContext& context) {
  QWidget* pane= context.workPane.data ();
  if (pane == nullptr) return nullptr;
  if (QTMWidget* canvas= qobject_cast<QTMWidget*> (pane)) return canvas;
  if (QTMWindow* window= qobject_cast<QTMWindow*> (pane))
    return window->editorCanvas ();
  return nullptr;
}

inline qt_actor_widget_rep*
editor_proxy_for_context (const QTMCommandContext& context) {
  QTMWidget* canvas= editor_canvas_for_context (context);
  if (canvas == nullptr) return nullptr;
  return dynamic_cast<qt_actor_widget_rep*> (canvas->tm_widget ());
}

inline bool
submit_inline_editor_action (
  const QTMCommandContext& context, const QJsonObject& action,
  std::uint32_t required= 0, std::uint32_t forbidden= 0,
  std::uint32_t any= 0) {
  QString validation;
  if (!native_editor_action_validate (action, &validation)) return false;
  qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
  if (proxy == nullptr) return false;
  QString encoded= QString::fromUtf8 (
    QJsonDocument (action).toJson (QJsonDocument::Compact));
  return proxy->submit_editor_action (encoded, required, forbidden, any);
}

inline bool
local_text_input_owns_edit_command (const QTMCommandContext& context) {
  QWidget* input= context.inputWidget.data ();
  QWidget* pane= context.workPane.data ();
  if (input == nullptr || pane == nullptr ||
      (input != pane && !pane->isAncestorOf (input)))
    return false;
  if (qobject_cast<QLineEdit*> (input) != nullptr ||
      qobject_cast<QTextEdit*> (input) != nullptr ||
      qobject_cast<QPlainTextEdit*> (input) != nullptr ||
      qobject_cast<QAbstractSpinBox*> (input) != nullptr)
    return true;
  if (QComboBox* combo= qobject_cast<QComboBox*> (input))
    return combo->isEditable ();
  return false;
}

inline bool
open_document_from_shell (
  const QTMCommandContext& context, bool newWindow= false) {
  QWidget* parent= context.shell.data ();
  QString path= QFileDialog::getOpenFileName (
    parent, QObject::tr ("Open document"), QString (),
    QObject::tr ("ATHENA documents (*.ath *.tm);;All files (*)"));
  if (path.isEmpty ()) return true;
  try {
    (void) call (newWindow ? "load-buffer-in-new-window" : "load-buffer",
                 object (url_system (from_qstring (path))));
    return true;
  }
  catch (...) {
    std_warning << "native command "
                << (newWindow ? "application.open-new-window"
                              : "application.open")
                << " could not load "
                << from_qstring (path) << LF;
    return false;
  }
}

inline url
frozen_document_url (const QTMCommandContext& context) {
  if (!context.lastDocument.has_buffer_name ()) return url_none ();
  const std::string& name= context.lastDocument.native_url_name;
  return url (string (name.data (), static_cast<int> (name.size ())));
}

QTMCommandState native_editor_command_state (
  const QString& id, const QTMCommandContext& context);
bool invoke_native_editor_command (
  const QString& id, const QTMCommandContext& context);

} // namespace qtm_command_registry_detail

#endif // QTMCOMMANDREGISTRYINTERNAL_HPP
