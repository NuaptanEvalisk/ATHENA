// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include "scheme.hpp"
#include <QJsonObject>

void user_shortcuts_initialize ();
void user_shortcuts_open (object options);
void user_shortcuts_open_native (string shortcut= "", string action= "");
bool user_shortcuts_has (string action);
array<string> user_shortcuts_list ();
string user_shortcuts_get (string shortcut);
void user_shortcuts_set (string shortcut, string action);
void user_shortcuts_remove (string shortcut);
object user_shortcuts_find (string rewritten_key);
string user_shortcuts_inverse (object source);
void user_shortcuts_run (string rewritten_key);
bool user_shortcut_action_valid (const QString&, QString* error);
