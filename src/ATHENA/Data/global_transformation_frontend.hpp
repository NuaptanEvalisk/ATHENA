// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#ifndef ATHENA_GLOBAL_TRANSFORMATION_FRONTEND_HPP
#define ATHENA_GLOBAL_TRANSFORMATION_FRONTEND_HPP
#include "scheme.hpp"
void register_global_transformation (string name, object transformer);
object load_global_transformation_script (url script);
void run_global_transformation ();
#endif
