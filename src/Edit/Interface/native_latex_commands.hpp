/******************************************************************************
* MODULE     : native_latex_commands.hpp
* DESCRIPTION: Native JSON LaTeX command registry
* COPYRIGHT  : (C) 2026 Nuaptan Felix Evalisk
*******************************************************************************/
#ifndef ATHENA_NATIVE_LATEX_COMMANDS_HPP
#define ATHENA_NATIVE_LATEX_COMMANDS_HPP

#include "command.hpp"
#include "string.hpp"
#include "tree.hpp"
#include <memory>

class evaluation_environment_checkpoint;

bool native_latex_get_command (string which, string& help, command& cmd);
// Resolve invocation inputs before a caller consumes its macro input draft.
// Non-native commands are returned unchanged.
command native_latex_prepare_command (
  command cmd, std::shared_ptr<const evaluation_environment_checkpoint> context);
int native_latex_command_count ();
tree native_latex_command_catalog ();

#endif
