/* Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later. */
#pragma once

// Desktop bootstrap must call this before loading preferences or running native
// data operations, independently of any Hodarium profile/service. This installs
// the sole local-change observer; later adapters must not replace it. Repeated
// calls are harmless. Registration itself does not access the active vault.
void athena_install_desktop_data_notifications ();
