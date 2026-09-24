/******************************************************************************
* MODULE     : native_latex_commands.hpp
* DESCRIPTION: Native JSON LaTeX command registry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/
#ifndef ATHENA_NATIVE_LATEX_COMMANDS_HPP
#define ATHENA_NATIVE_LATEX_COMMANDS_HPP

#include "command.hpp"
#include "string.hpp"

bool native_latex_get_command (string which, string& help, command& cmd);
int native_latex_command_count ();

#endif
