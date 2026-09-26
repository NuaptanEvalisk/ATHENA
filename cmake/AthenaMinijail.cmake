if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  return()
endif()

set(ATHENA_MINIJAIL_SOURCE_DIR "${ATHENA_SOURCE_DIR}/3rdparty/minijail")
set(ATHENA_MINIJAIL_GENERATED_DIR "${ATHENA_BINARY_DIR}/minijail-generated")
file(MAKE_DIRECTORY "${ATHENA_MINIJAIL_GENERATED_DIR}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBCAP REQUIRED IMPORTED_TARGET libcap)

set(ATHENA_MINIJAIL_SYSCALLS "${ATHENA_MINIJAIL_GENERATED_DIR}/libsyscalls.gen.c")
set(ATHENA_MINIJAIL_CONSTANTS "${ATHENA_MINIJAIL_GENERATED_DIR}/libconstants.gen.c")

add_custom_command(
  OUTPUT "${ATHENA_MINIJAIL_SYSCALLS}"
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}"
          "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_syscalls.sh"
          "${ATHENA_MINIJAIL_SYSCALLS}"
  WORKING_DIRECTORY "${ATHENA_MINIJAIL_SOURCE_DIR}"
  DEPENDS
    "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_syscalls.sh"
    "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_syscalls.c"
    "${ATHENA_MINIJAIL_SOURCE_DIR}/libsyscalls.h"
  VERBATIM)

add_custom_command(
  OUTPUT "${ATHENA_MINIJAIL_CONSTANTS}"
  COMMAND "${CMAKE_COMMAND}" -E env "CC=${CMAKE_C_COMPILER}"
          "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_constants.sh"
          "${ATHENA_MINIJAIL_CONSTANTS}"
  WORKING_DIRECTORY "${ATHENA_MINIJAIL_SOURCE_DIR}"
  DEPENDS
    "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_constants.sh"
    "${ATHENA_MINIJAIL_SOURCE_DIR}/gen_constants.c"
    "${ATHENA_MINIJAIL_SOURCE_DIR}/libconstants.h"
  VERBATIM)

add_library(athena_minijail STATIC
  "${ATHENA_MINIJAIL_SOURCE_DIR}/libminijail.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/syscall_filter.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/signal_handler.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/bpf.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/landlock_util.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/util.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/system.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/syscall_wrapper.c"
  "${ATHENA_MINIJAIL_SOURCE_DIR}/config_parser.c"
  "${ATHENA_MINIJAIL_SYSCALLS}"
  "${ATHENA_MINIJAIL_CONSTANTS}")
set_target_properties(athena_minijail PROPERTIES
  C_STANDARD 17 C_STANDARD_REQUIRED ON POSITION_INDEPENDENT_CODE ON)
target_include_directories(athena_minijail PUBLIC
  "${ATHENA_MINIJAIL_SOURCE_DIR}"
  "${ATHENA_MINIJAIL_GENERATED_DIR}")
target_compile_definitions(athena_minijail PRIVATE
  PRELOADPATH="/nonexistent/libminijailpreload.so"
  NEWUIDMAP_PATH="/usr/bin/newuidmap"
  NEWGIDMAP_PATH="/usr/bin/newgidmap"
  DEFAULT_PIVOT_ROOT="/var/empty"
  BINDMOUNT_ALLOWED_PREFIXES="/dev,/sys")
target_compile_options(athena_minijail PRIVATE
  -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_link_libraries(athena_minijail PUBLIC PkgConfig::LIBCAP ${CMAKE_DL_LIBS})
