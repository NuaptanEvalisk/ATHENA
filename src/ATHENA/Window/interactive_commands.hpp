// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_INTERACTIVE_COMMANDS_HPP
#define ATHENA_INTERACTIVE_COMMANDS_HPP
#include "scheme.hpp"
void interactive_learn (object function, object values);
object interactive_learned (object function);
void interactive_forget (object function);
void interactive_history_save ();
void interactive_command (object function, object arguments);
void interactive_schedule_idle (int milliseconds, object continuation);
void interactive_startup ();
void interactive_properties_initialize ();
bool interactive_page_format_applicable (object arguments);
void interactive_choose_file (object action, string title, string type, object options);
#endif
