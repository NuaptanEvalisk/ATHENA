// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_APPLICATION_PREFERENCES_HPP
#define ATHENA_APPLICATION_PREFERENCES_HPP
#include "string.hpp"
void application_preferences_initialize ();
bool application_preferences_ready ();
void application_notify_look (string key, string value);
void application_notify_security (string key, string value);
void application_notify_latex (string key, string value);
void application_notify_tool (string key, string value);
void application_notify_enunciations (string key, string value);
string application_exercises_smaller ();
string application_number_solutions ();
#endif
