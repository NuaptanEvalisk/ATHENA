/******************************************************************************
* MODULE     : vault_frontend.hpp
* DESCRIPTION: Native vault startup and welcome-page policy
*******************************************************************************/

#ifndef ATHENA_VAULT_FRONTEND_HPP
#define ATHENA_VAULT_FRONTEND_HPP

#include "string.hpp"
#include "tree.hpp"

void vault_track_current_buffer_if_enabled ();
void vault_show_explorer_and_track_native ();
void vault_startup_open_initial_buffer_native ();
void vault_load_latest_action_native (string path);
void go_to_system_welcome_page_native ();
void go_to_welcome_page_native ();
void go_to_vault_initial_page_native ();
tree vault_welcome_page_native ();

#endif // ATHENA_VAULT_FRONTEND_HPP
