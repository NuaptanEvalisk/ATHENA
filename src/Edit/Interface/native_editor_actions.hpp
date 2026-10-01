/******************************************************************************
* MODULE     : native_editor_actions.hpp
* DESCRIPTION: Validated parameterized native editor actions
* COPYRIGHT  : (C) 2026 ATHENA contributors
*******************************************************************************
* This software falls under the GNU general public license version 3 or later.
******************************************************************************/

#ifndef ATHENA_NATIVE_EDITOR_ACTIONS_HPP
#define ATHENA_NATIVE_EDITOR_ACTIONS_HPP

#include "editor.hpp"

#include <QJsonObject>
#include <QString>

bool native_editor_action_validate (const QJsonObject& action,
                                    QString* error= nullptr);
void native_editor_action_execute (editor ed, const QJsonObject& action);

#endif // ATHENA_NATIVE_EDITOR_ACTIONS_HPP
