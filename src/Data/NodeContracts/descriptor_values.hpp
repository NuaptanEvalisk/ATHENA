// Copyright (C) 2026 ATHENA contributors. GPL-3.0-or-later.
#pragma once
#include <string_view>

// Shared descriptor vocabulary; no mutable registry or tree ownership.
#define TYPE_INVALID         -1
#define TYPE_REGULAR          0
#define TYPE_ADHOC            1
#define TYPE_RAW              2
#define TYPE_VARIABLE         3
#define TYPE_ARGUMENT         4
#define TYPE_BINDING          5
#define TYPE_BOOLEAN          6
#define TYPE_INTEGER          7
#define TYPE_STRING           8
#define TYPE_LENGTH           9
#define TYPE_NUMERIC         10
#define TYPE_CODE            11
#define TYPE_IDENTIFIER      12
#define TYPE_URL             13
#define TYPE_COLOR           14
#define TYPE_GRAPHICAL       15
#define TYPE_POINT           16
#define TYPE_CONSTRAINT      17
#define TYPE_GRAPHICAL_ID    18
#define TYPE_EFFECT          19
#define TYPE_DURATION        21
#define TYPE_FONT_SIZE       22
#define TYPE_OBSOLETE        23
#define TYPE_UNKNOWN         24
#define TYPE_ERROR           25
#define ARITY_NORMAL          0
#define ARITY_OPTIONS         1
#define ARITY_REPEAT          2
#define ARITY_VAR_REPEAT      3
#define CHILD_UNIFORM         0
#define CHILD_BIFORM          1
#define CHILD_DETAILED        2
#define BLOCK_NO              0
#define BLOCK_YES             1
#define BLOCK_OR              2
#define BORDER_YES            0
#define BORDER_INNER          1
#define BORDER_OUTER          2
#define BORDER_NO             3
#define VAR_MACRO             0
#define VAR_PARAMETER         1
#define VAR_MACRO_PARAMETER   2
#define ACCESSIBLE_NEVER      0
#define ACCESSIBLE_HIDDEN     1
#define ACCESSIBLE_ALWAYS     2
#define WRITABILITY_NORMAL    0
#define WRITABILITY_DISABLE   1
#define WRITABILITY_ENABLE    2
#define BLOCK_REQUIRE_BLOCK   0
#define BLOCK_REQUIRE_INLINE  1
#define BLOCK_REQUIRE_NONE    2

namespace athena::contracts {
int type_code (std::string_view name);
std::string_view type_name (int code);
}
