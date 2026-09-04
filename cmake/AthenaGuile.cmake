include(ExternalProject)
include(AthenaBdwgc)

find_program(ATHENA_GUILE_MAKE_EXECUTABLE NAMES gmake make REQUIRED)

if(WIN32)
  message(FATAL_ERROR
    "The ATHENA Guile 3 runtime is currently supported by the Linux Qt6 "
    "build only. Windows integration will follow after Linux validation.")
endif()

set(ATHENA_GUILE_SOURCE_DIR
  "${ATHENA_SOURCE_DIR}/3rdparty/athena-guile")
set(ATHENA_GUILE_RUNTIME_ID "athena-guile-3.0.10-native"
  CACHE INTERNAL "ATHENA private Guile runtime identity" FORCE)
set(ATHENA_GUILE_BUILD_DIR
  "${ATHENA_BINARY_DIR}/athena-guile-build")
set(ATHENA_GUILE_PREFIX
  "${ATHENA_BINARY_DIR}/athena-guile-runtime")
set(ATHENA_GUILE_INCLUDE_DIR
  "${ATHENA_GUILE_PREFIX}/include/guile/3.0")
set(ATHENA_GUILE_LIBRARY
  "${ATHENA_GUILE_PREFIX}/lib/libathena-guile${CMAKE_SHARED_LIBRARY_SUFFIX}")

if(ATHENA_ENABLE_TSAN)
  set(ATHENA_GUILE_C_FLAGS
    "-O1 -g -std=gnu17 -fno-omit-frame-pointer ${ATHENA_CPU_COMPILE_FLAGS} -fsanitize=thread")
  set(ATHENA_GUILE_LINK_FLAGS "-fsanitize=thread -fuse-ld=lld")
  set(ATHENA_GUILE_MAKE_OPTIONS "GUILE_OPTIMIZATIONS=-O0")
  set(ATHENA_GUILE_BUILD_COMMAND
    "${CMAKE_COMMAND}" -E env "GC_MARKERS=1"
    "${ATHENA_GUILE_MAKE_EXECUTABLE}")
else()
  set(ATHENA_GUILE_C_FLAGS
    "-O3 -g -std=gnu17 -fno-omit-frame-pointer ${ATHENA_CPU_COMPILE_FLAGS} -funroll-loops")
  set(ATHENA_GUILE_LINK_FLAGS
    "-flto=thin -fuse-ld=lld -Wl,--thinlto-cache-dir=${ATHENA_GUILE_BUILD_DIR}/lto-cache")
  set(ATHENA_GUILE_MAKE_OPTIONS "")
  set(ATHENA_GUILE_BUILD_COMMAND "${ATHENA_GUILE_MAKE_EXECUTABLE}")
endif()
if(CMAKE_C_COMPILER_ID MATCHES "Intel")
  # Guile's numeric semantics require gradual underflow. IntelLLVM defaults
  # optimized builds to fast floating point and enables FTZ/DAZ otherwise.
  string(APPEND ATHENA_GUILE_C_FLAGS " -fp-model=precise -no-ftz")
endif()
set(ATHENA_GUILE_TOOLCHAIN_FINGERPRINT
  "${ATHENA_BINARY_DIR}/athena-guile-toolchain.txt")
athena_write_build_fingerprint(
  "${ATHENA_GUILE_TOOLCHAIN_FINGERPRINT}"
  "CC=${CMAKE_C_COMPILER}\nCFLAGS=${ATHENA_GUILE_C_FLAGS}\nLDFLAGS=${ATHENA_GUILE_LINK_FLAGS}\n")

# Imported include directories must exist when CMake generates the ATHENA
# targets.  The external build populates this directory before compilation.
file(MAKE_DIRECTORY "${ATHENA_GUILE_INCLUDE_DIR}")

file(GLOB_RECURSE ATHENA_GUILE_RUNTIME_SOURCES CONFIGURE_DEPENDS
  "${ATHENA_GUILE_SOURCE_DIR}/*.c"
  "${ATHENA_GUILE_SOURCE_DIR}/*.h"
  "${ATHENA_GUILE_SOURCE_DIR}/*.scm")

ExternalProject_Add(athena_guile_runtime
  SOURCE_DIR "${ATHENA_GUILE_SOURCE_DIR}"
  BINARY_DIR "${ATHENA_GUILE_BUILD_DIR}"
  INSTALL_DIR "${ATHENA_GUILE_PREFIX}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CONFIGURE_COMMAND
    "${CMAKE_COMMAND}" -E env
      "CC=${CMAKE_C_COMPILER}"
      "CFLAGS=${ATHENA_GUILE_C_FLAGS}"
      "LDFLAGS=${ATHENA_GUILE_LINK_FLAGS}"
      "PKG_CONFIG_PATH=${ATHENA_BDWGC_PKGCONFIG_DIR}:$ENV{PKG_CONFIG_PATH}"
      <SOURCE_DIR>/configure
        --prefix=<INSTALL_DIR>
        --libdir=<INSTALL_DIR>/lib
        --enable-shared
        --disable-static
        --enable-jit=yes
        --disable-nls
        --enable-lto=thin
  BUILD_COMMAND ${ATHENA_GUILE_BUILD_COMMAND} -j20 ${ATHENA_GUILE_MAKE_OPTIONS}
  INSTALL_COMMAND ${ATHENA_GUILE_BUILD_COMMAND} -j20
    ${ATHENA_GUILE_MAKE_OPTIONS} install
  BUILD_BYPRODUCTS "${ATHENA_GUILE_LIBRARY}"
  DEPENDS athena_bdwgc_runtime
  USES_TERMINAL_BUILD TRUE
  USES_TERMINAL_INSTALL TRUE)

ExternalProject_Add_Step(athena_guile_runtime toolchain_changes
  COMMAND "${CMAKE_COMMAND}" -E true
  DEPENDERS configure
  DEPENDS "${ATHENA_GUILE_TOOLCHAIN_FINGERPRINT}"
  COMMENT "Checking the private ATHENA Guile toolchain configuration")

# ExternalProject normally treats an in-tree SOURCE_DIR as opaque: without an
# explicit step dependency, editing the vendored runtime would not invalidate
# its completed build stamp.  Keep the runtime incremental while still making
# source edits rebuild and reinstall it exactly once.
ExternalProject_Add_Step(athena_guile_runtime source_changes
  COMMAND "${CMAKE_COMMAND}" -E true
  DEPENDEES configure
  DEPENDERS build
  DEPENDS ${ATHENA_GUILE_RUNTIME_SOURCES}
  COMMENT "Checking the modified ATHENA Guile runtime sources")

add_library(ATHENA::Guile SHARED IMPORTED GLOBAL)
set_target_properties(ATHENA::Guile PROPERTIES
  IMPORTED_LOCATION "${ATHENA_GUILE_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${ATHENA_GUILE_INCLUDE_DIR}")
add_dependencies(ATHENA::Guile athena_guile_runtime)

set(ATHENA_GUILE_STANDARD_LIBRARY
  "${ATHENA_GUILE_PREFIX}/share/guile/3.0")
set(ATHENA_GUILE_COMPILED_LIBRARY
  "${ATHENA_GUILE_PREFIX}/lib/guile/3.0/ccache")
