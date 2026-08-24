/* ATHENA-specific extensions to the private Guile runtime.

   Copyright (C) 2026 Nuaptan Felix Evalisk

   This file is part of ATHENA's Guile runtime and is distributed under the
   GNU Lesser General Public License version 3 or later.  */

#ifndef SCM_ATHENA_RUNTIME_H
#define SCM_ATHENA_RUNTIME_H

#include "libguile/scm.h"

SCM_INTERNAL void scm_init_athena_runtime (void);

#endif
