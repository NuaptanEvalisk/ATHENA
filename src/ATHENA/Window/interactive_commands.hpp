// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_INTERACTIVE_COMMANDS_HPP
#define ATHENA_INTERACTIVE_COMMANDS_HPP
#include "scheme.hpp"
void interactive_learn (string function, object values);
object interactive_learned (string function);
void interactive_forget (string function);
void interactive_history_save ();
void interactive_command (string function, array<string> labels);
void interactive_schedule_idle (int milliseconds, object continuation);
void interactive_startup ();
void interactive_properties_initialize ();
void interactive_choose_file (object action, string title, string type, object options);
#endif
