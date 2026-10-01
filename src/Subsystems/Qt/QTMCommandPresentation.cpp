/******************************************************************************
* MODULE     : QTMCommandRegistry.cpp
* DESCRIPTION: Native application command registry and work-context routing
* COPYRIGHT  : (C) 2026 Felix
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
* It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
* in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.
******************************************************************************/


#include "QTMCommandRegistry.hpp"
#include "QTMCommandRegistryInternal.hpp"

#include "file.hpp"
#include "scheme.hpp"
#include "tm_ostream.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>
#include <QSet>

using namespace qtm_command_registry_detail;

namespace {
bool
editor_capability_bit (const QString& name, std::uint32_t& bit) {
  static const QHash<QString, std::uint32_t> bits {
    {"read-only", ACTOR_EDITOR_COMMAND_STATE_READ_ONLY},
    {"selection", ACTOR_EDITOR_COMMAND_STATE_SELECTION},
    {"non-small-selection", ACTOR_EDITOR_COMMAND_STATE_NON_SMALL_SELECTION},
    {"math-mode", ACTOR_EDITOR_COMMAND_STATE_MATH_MODE},
    {"presentation-mode", ACTOR_EDITOR_COMMAND_STATE_PRESENTATION_MODE},
    {"screens-mode", ACTOR_EDITOR_COMMAND_STATE_SCREENS_MODE},
    {"text-mode", ACTOR_EDITOR_COMMAND_STATE_TEXT_MODE},
    {"prog-mode", ACTOR_EDITOR_COMMAND_STATE_PROG_MODE},
    {"source-mode", ACTOR_EDITOR_COMMAND_STATE_SOURCE_MODE},
    {"graphics-mode", ACTOR_EDITOR_COMMAND_STATE_GRAPHICS_MODE},
    {"poster-style", ACTOR_EDITOR_COMMAND_STATE_POSTER_STYLE},
    {"manual-style", ACTOR_EDITOR_COMMAND_STATE_MANUAL_STYLE},
    {"header-letter", ACTOR_EDITOR_COMMAND_STATE_HEADER_LETTER},
    {"book-style", ACTOR_EDITOR_COMMAND_STATE_BOOK_STYLE},
    {"section-base", ACTOR_EDITOR_COMMAND_STATE_SECTION_BASE},
    {"env-theorem", ACTOR_EDITOR_COMMAND_STATE_ENV_THEOREM},
    {"std-markup", ACTOR_EDITOR_COMMAND_STATE_STD_MARKUP},
    {"std-list", ACTOR_EDITOR_COMMAND_STATE_STD_LIST},
    {"env-float", ACTOR_EDITOR_COMMAND_STATE_ENV_FLOAT},
    {"std-fold", ACTOR_EDITOR_COMMAND_STATE_STD_FOLD},
    {"std-dtd", ACTOR_EDITOR_COMMAND_STATE_STD_DTD},
    {"inside-letter-header",
     ACTOR_EDITOR_COMMAND_STATE_INSIDE_LETTER_HEADER},
    {"inside-float-or-footnote",
     ACTOR_EDITOR_COMMAND_STATE_INSIDE_FLOAT_OR_FOOTNOTE},
    {"main-flow", ACTOR_EDITOR_COMMAND_STATE_MAIN_FLOW},
    {"env-math", ACTOR_EDITOR_COMMAND_STATE_ENV_MATH},
    {"tmdoc-traverse", ACTOR_EDITOR_COMMAND_STATE_TMDOC_TRAVERSE},
    {"tmdoc-explain", ACTOR_EDITOR_COMMAND_STATE_TMDOC_EXPLAIN},
    {"overlays-context", ACTOR_EDITOR_COMMAND_STATE_OVERLAYS_CONTEXT},
    {"screens-buffer", ACTOR_EDITOR_COMMAND_STATE_SCREENS_BUFFER}
  };
  auto found= bits.constFind (name);
  if (found == bits.constEnd ()) return false;
  bit= found.value ();
  return true;
}

bool
parse_editor_capability_mask (
  const QJsonValue& value, std::uint32_t& mask, QString& error) {
  mask= 0;
  if (value.isUndefined ()) return true;
  if (!value.isArray ()) {
    error= "editor capability list must be an array";
    return false;
  }
  for (const QJsonValue& item: value.toArray ()) {
    if (!item.isString ()) {
      error= "editor capability name must be a string";
      return false;
    }
    std::uint32_t bit= 0;
    QString name= item.toString ().trimmed ();
    if (!editor_capability_bit (name, bit)) {
      error= QString ("unknown editor capability: %1").arg (name);
      return false;
    }
    mask |= bit;
  }
  return true;
}

bool
focus_capability_bit (const QString& name, std::uint32_t& bit) {
  static const QHash<QString, std::uint32_t> bits {
    {"buffer", ACTOR_FOCUS_TOOLBAR_BUFFER},
    {"can-move", ACTOR_FOCUS_TOOLBAR_CAN_MOVE},
    {"can-insert-remove", ACTOR_FOCUS_TOOLBAR_CAN_INSERT_REMOVE},
    {"horizontal", ACTOR_FOCUS_TOOLBAR_HORIZONTAL},
    {"vertical", ACTOR_FOCUS_TOOLBAR_VERTICAL},
    {"can-insert", ACTOR_FOCUS_TOOLBAR_CAN_INSERT},
    {"can-remove", ACTOR_FOCUS_TOOLBAR_CAN_REMOVE},
    {"cursor-inside", ACTOR_FOCUS_TOOLBAR_CURSOR_INSIDE},
    {"has-variants", ACTOR_FOCUS_TOOLBAR_HAS_VARIANTS},
    {"has-preferences", ACTOR_FOCUS_TOOLBAR_HAS_PREFERENCES},
    {"has-parameters", ACTOR_FOCUS_TOOLBAR_HAS_PARAMETERS},
    {"can-search", ACTOR_FOCUS_TOOLBAR_CAN_SEARCH},
    {"has-search-menu", ACTOR_FOCUS_TOOLBAR_HAS_SEARCH_MENU},
    {"has-label", ACTOR_FOCUS_TOOLBAR_HAS_LABEL},
    {"has-hidden-children", ACTOR_FOCUS_TOOLBAR_HAS_HIDDEN_CHILDREN},
    {"code-context", ACTOR_FOCUS_TOOLBAR_CODE_CONTEXT},
    {"screens-context", ACTOR_FOCUS_TOOLBAR_SCREENS_CONTEXT},
    {"table-context", ACTOR_FOCUS_TOOLBAR_TABLE_CONTEXT},
    {"doc-title-context", ACTOR_FOCUS_TOOLBAR_DOC_TITLE_CONTEXT},
    {"doc-author-context", ACTOR_FOCUS_TOOLBAR_DOC_AUTHOR_CONTEXT},
    {"abstract-context", ACTOR_FOCUS_TOOLBAR_ABSTRACT_CONTEXT},
    {"algorithm-context", ACTOR_FOCUS_TOOLBAR_ALGORITHM_CONTEXT},
    {"marginal-note-context", ACTOR_FOCUS_TOOLBAR_MARGINAL_NOTE_CONTEXT},
    {"rich-float-context", ACTOR_FOCUS_TOOLBAR_RICH_FLOAT_CONTEXT},
    {"phantom-float-context", ACTOR_FOCUS_TOOLBAR_PHANTOM_FLOAT_CONTEXT},
    {"floatable-context", ACTOR_FOCUS_TOOLBAR_FLOATABLE_CONTEXT},
    {"footnote-context", ACTOR_FOCUS_TOOLBAR_FOOTNOTE_CONTEXT},
    {"balloon-context", ACTOR_FOCUS_TOOLBAR_BALLOON_CONTEXT},
    {"detached-note-context", ACTOR_FOCUS_TOOLBAR_DETACHED_NOTE_CONTEXT},
    {"titled-context", ACTOR_FOCUS_TOOLBAR_TITLED_CONTEXT},
    {"frame-context", ACTOR_FOCUS_TOOLBAR_FRAME_CONTEXT}
  };
  auto found= bits.constFind (name);
  if (found == bits.constEnd ()) return false;
  bit= found.value ();
  return true;
}

bool
parse_focus_capability_mask (
  const QJsonValue& value, std::uint32_t& mask, QString& error) {
  mask= 0;
  if (value.isUndefined ()) return true;
  if (!value.isArray ()) {
    error= "focus capability list must be an array";
    return false;
  }
  for (const QJsonValue& item: value.toArray ()) {
    if (!item.isString ()) {
      error= "focus capability name must be a string";
      return false;
    }
    std::uint32_t bit= 0;
    QString name= item.toString ().trimmed ();
    if (!focus_capability_bit (name, bit)) {
      error= QString ("unknown focus capability: %1").arg (name);
      return false;
    }
    mask |= bit;
  }
  return true;
}

} // namespace

bool
QTMCommandRegistry::failPresentation (const QString& message) {
  std_warning << "native command presentation error: "
              << from_qstring (message) << LF;
  commands_.clear ();
  menus_.clear ();
  toolbars_.clear ();
  commandIndex_.clear ();
  return false;
}

bool
QTMCommandRegistry::loadPresentation () {
  string text;
  const char* resource= "$ATHENA_PATH/misc/ui/application-shell.json";
  if (load_string (url (resource), text, false))
    return failPresentation (
      QString ("cannot read %1").arg (QString::fromLatin1 (resource)));

  c_string bytes (text);
  QJsonParseError parse;
  QJsonDocument document= QJsonDocument::fromJson (
    QByteArray (bytes, N(text)), &parse);
  if (parse.error != QJsonParseError::NoError || !document.isObject ())
    return failPresentation (
      QString ("invalid JSON: %1").arg (parse.errorString ()));

  QJsonObject root= document.object ();
  if (root.value ("version").toInt (-1) != 3)
    return failPresentation ("unsupported or missing version");
  if (!root.value ("commands").isArray ())
    return failPresentation ("commands must be an array");
  if (!root.value ("menus").isArray ())
    return failPresentation ("menus must be an array");
  if (!root.value ("toolbars").isArray ())
    return failPresentation ("toolbars must be an array");

  QJsonArray commandValues= root.value ("commands").toArray ();
  QJsonArray menuValues= root.value ("menus").toArray ();
  QJsonArray toolbarValues= root.value ("toolbars").toArray ();
  for (const char* extensionResource: {
         "$ATHENA_PATH/misc/ui/editor-mode-toolbar.json",
         "$ATHENA_PATH/misc/ui/editor-focus-toolbar.json"}) {
    string extensionText;
    if (load_string (url (extensionResource), extensionText, false))
      return failPresentation (
        QString ("cannot read %1")
          .arg (QString::fromLatin1 (extensionResource)));
    c_string extensionBytes (extensionText);
    QJsonParseError extensionParse;
    QJsonDocument extensionDocument= QJsonDocument::fromJson (
      QByteArray (extensionBytes, N(extensionText)), &extensionParse);
    if (extensionParse.error != QJsonParseError::NoError ||
        !extensionDocument.isObject ())
      return failPresentation (
        QString ("invalid toolbar extension JSON %1: %2")
          .arg (QString::fromLatin1 (extensionResource),
                extensionParse.errorString ()));
    QJsonObject extensionRoot= extensionDocument.object ();
    if (extensionRoot.value ("version").toInt (-1) != 1 ||
        !extensionRoot.value ("commands").isArray () ||
        !extensionRoot.value ("toolbars").isArray ())
      return failPresentation (
        QString ("invalid toolbar extension schema: %1")
          .arg (QString::fromLatin1 (extensionResource)));
    for (const QJsonValue& value:
         extensionRoot.value ("commands").toArray ())
      commandValues.append (value);
    for (const QJsonValue& value:
         extensionRoot.value ("toolbars").toArray ())
      toolbarValues.append (value);
  }

  QSet<QString> commandIds;
  QHash<QString, QString> shortcuts;
  for (const QJsonValue& value: commandValues) {
    if (!value.isObject ())
      return failPresentation ("every commands entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    QString label= object.value ("label").toString ().trimmed ();
    QString category= object.value ("category").toString ().trimmed ();
    if (id.isEmpty () || label.isEmpty () || category.isEmpty ())
      return failPresentation (
        "every command requires non-empty id, label, and category");
    if (commandIds.contains (id))
      return failPresentation (QString ("duplicate command id: %1").arg (id));
    if (!behaviors_.contains (id)) {
      if (!object.value ("editor_action").isObject ())
        return failPresentation (
          QString ("presentation references unknown command id: %1").arg (id));
      QJsonObject action= object.value ("editor_action").toObject ();
      QString actionError;
      if (!native_editor_action_validate (action, &actionError))
        return failPresentation (
          QString ("invalid editor_action for %1: %2").arg (id, actionError));
      std::uint32_t required= 0;
      std::uint32_t forbidden= 0;
      std::uint32_t any= 0;
      QString maskError;
      if (!parse_editor_capability_mask (
            object.value ("requires"), required, maskError) ||
          !parse_editor_capability_mask (
            object.value ("forbids"), forbidden, maskError) ||
          !parse_editor_capability_mask (
            object.value ("requires_any"), any, maskError))
        return failPresentation (
          QString ("invalid capability mask for %1: %2").arg (id, maskError));
      if ((required & forbidden) != 0)
        return failPresentation (
          QString ("command %1 requires and forbids the same capability")
            .arg (id));
      QString encoded= QString::fromUtf8 (
        QJsonDocument (action).toJson (QJsonDocument::Compact));
      registerBehavior (
        id, QTMCommandScope::Editor,
        [encoded, required, forbidden, any] (const QTMCommandContext& context) {
          qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
          return proxy != nullptr &&
                 proxy->submit_editor_action (
                   encoded, required, forbidden, any);
        },
        [required, forbidden, any] (const QTMCommandContext& context) {
          QTMCommandState state;
          if (local_text_input_owns_edit_command (context)) return state;
          qt_actor_widget_rep* proxy= editor_proxy_for_context (context);
          if (proxy == nullptr) return state;
          actor_editor_command_snapshot snapshot= proxy->editor_command_state ();
          if (!snapshot.valid ()) return state;
          state.available=
            (snapshot.flags & required) == required &&
            (snapshot.flags & forbidden) == 0 &&
            (any == 0 || (snapshot.flags & any) != 0);
          state.enabled= state.available;
          return state;
        });
    }

    QString shortcutText= object.value ("shortcut").toString ().trimmed ();
    QKeySequence shortcut;
    if (!shortcutText.isEmpty ()) {
      shortcut= QKeySequence::fromString (shortcutText,
                                          QKeySequence::PortableText);
      if (shortcut.isEmpty ())
        return failPresentation (
          QString ("invalid shortcut '%1' for %2").arg (shortcutText, id));
      QString canonical= shortcut.toString (QKeySequence::PortableText);
      if (shortcuts.contains (canonical))
        return failPresentation (
          QString ("shortcut conflict %1 between %2 and %3")
            .arg (canonical, shortcuts.value (canonical), id));
      shortcuts.insert (canonical, id);
    }

    QTMCommandDefinition definition;
    definition.id= id;
    definition.label= label;
    definition.icon= object.value ("icon").toString ().trimmed ();
    definition.category= category;
    definition.help= object.value ("help").toString ().trimmed ();
    definition.shortcut= shortcut;
    definition.scope= behaviors_.value (id).scope;
    definition.showInPalette= object.value ("palette").toBool (true);
    commandIndex_.insert (id, commands_.size ());
    commands_.append (std::move (definition));
    commandIds.insert (id);
  }

  for (auto it= behaviors_.constBegin (); it != behaviors_.constEnd (); ++it)
    if (!commandIds.contains (it.key ()))
      return failPresentation (
        QString ("native command has no presentation entry: %1").arg (it.key ()));

  QSet<QString> menuIds;
  std::function<bool(
    const QString&, const QJsonArray&, QVector<QTMCommandMenuItem>&)>
  parseMenuItems;
  parseMenuItems=
    [&] (const QString& ownerId, const QJsonArray& values,
         QVector<QTMCommandMenuItem>& out) -> bool {
    for (const QJsonValue& itemValue: values) {
      if (!itemValue.isObject ())
        return failPresentation (
          QString ("menu %1 contains a non-object item").arg (ownerId));
      QJsonObject itemObject= itemValue.toObject ();
      bool separator= itemObject.value ("separator").toBool (false);
      QString commandId=
        itemObject.value ("command").toString ().trimmed ();
      QString submenuId=
        itemObject.value ("submenu").toString ().trimmed ();
      QString providerId=
        itemObject.value ("provider").toString ().trimmed ();
      int kinds= (separator ? 1 : 0) + (!commandId.isEmpty () ? 1 : 0) +
                  (!submenuId.isEmpty () ? 1 : 0) +
                  (!providerId.isEmpty () ? 1 : 0);
      if (kinds != 1)
        return failPresentation (
          QString ("menu %1 item requires one item kind").arg (ownerId));

      QTMCommandMenuItem item;
      QString itemMaskError;
      if (!parse_editor_capability_mask (
            itemObject.value ("requires"), item.requiredFlags,
            itemMaskError) ||
          !parse_editor_capability_mask (
            itemObject.value ("forbids"), item.forbiddenFlags,
            itemMaskError) ||
          !parse_editor_capability_mask (
            itemObject.value ("requires_any"), item.anyFlags,
            itemMaskError) ||
          !parse_focus_capability_mask (
            itemObject.value ("focus_requires"), item.focusRequiredFlags,
            itemMaskError) ||
          !parse_focus_capability_mask (
            itemObject.value ("focus_forbids"), item.focusForbiddenFlags,
            itemMaskError) ||
          !parse_focus_capability_mask (
            itemObject.value ("focus_requires_any"), item.focusAnyFlags,
            itemMaskError))
        return failPresentation (
          QString ("invalid item capability mask in %1: %2")
            .arg (ownerId, itemMaskError));
      if (itemObject.contains ("when_main_toolbar_hidden") &&
          !itemObject.value ("when_main_toolbar_hidden").isBool ())
        return failPresentation (
          QString ("menu %1 item has invalid main-toolbar condition")
            .arg (ownerId));
      item.whenMainToolbarHidden=
        itemObject.value ("when_main_toolbar_hidden").toBool (false);
      if (separator)
        item.kind= QTMCommandMenuItem::Kind::Separator;
      else if (!commandId.isEmpty ()) {
        if (!commandIndex_.contains (commandId))
          return failPresentation (
            QString ("menu %1 references unknown command: %2")
              .arg (ownerId, commandId));
        item.kind= QTMCommandMenuItem::Kind::Command;
        item.commandId= commandId;
      }
      else if (!providerId.isEmpty ()) {
        if (!providers_.contains (providerId))
          return failPresentation (
            QString ("menu %1 references unknown provider: %2")
              .arg (ownerId, providerId));
        item.kind= QTMCommandMenuItem::Kind::Provider;
        item.providerId= providerId;
      }
      else {
        QString label= itemObject.value ("label").toString ().trimmed ();
        if (label.isEmpty () || !itemObject.value ("items").isArray ())
          return failPresentation (
            QString ("submenu %1 requires label and items").arg (submenuId));
        if (menuIds.contains (submenuId))
          return failPresentation (
            QString ("duplicate menu/submenu id: %1").arg (submenuId));
        menuIds.insert (submenuId);
        item.kind= QTMCommandMenuItem::Kind::Submenu;
        item.submenuId= submenuId;
        item.label= label;
        item.icon= itemObject.value ("icon").toString ().trimmed ();
        if (!parseMenuItems (
              submenuId, itemObject.value ("items").toArray (), item.items))
          return false;
      }
      out.append (std::move (item));
    }
    return true;
  };
  for (const QJsonValue& value: menuValues) {
    if (!value.isObject ())
      return failPresentation ("every menus entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    QString label= object.value ("label").toString ().trimmed ();
    if (id.isEmpty () || label.isEmpty () || menuIds.contains (id))
      return failPresentation (
        QString ("invalid or duplicate menu id: %1").arg (id));
    if (!object.value ("items").isArray ())
      return failPresentation (
        QString ("menu %1 has no items array").arg (id));

    QTMCommandMenuDefinition menu;
    menu.id= id;
    menu.label= label;
    menuIds.insert (id);
    if (!parseMenuItems (
          id, object.value ("items").toArray (), menu.items))
      return false;
    menus_.append (std::move (menu));
  }

  for (const QJsonValue& value: toolbarValues) {
    if (!value.isObject ())
      return failPresentation ("every toolbars entry must be an object");
    QJsonObject object= value.toObject ();
    QString id= object.value ("id").toString ().trimmed ();
    if (id.isEmpty () || menuIds.contains (id))
      return failPresentation (
        QString ("invalid or duplicate toolbar id: %1").arg (id));
    if (!object.value ("items").isArray ())
      return failPresentation (
        QString ("toolbar %1 has no items array").arg (id));
    QTMCommandToolbarDefinition toolbar;
    toolbar.id= id;
    menuIds.insert (id);
    if (!parseMenuItems (
          id, object.value ("items").toArray (), toolbar.items))
      return false;
    toolbars_.append (std::move (toolbar));
  }

  return true;
}
