
###############################################################################
#
# MODULE      : CMakeLists.txt
# DESCRIPTION : CMake file for ATHENA
# COPYRIGHT   : (C) 2008-2009  Massimiliano Gubinelli
#               (C) 2017-2020  Darcy Shen
#
# This software falls under the GNU general public license version 3 or later.
# It comes WITHOUT ANY WARRANTY WHATSOEVER. For details, see the file LICENSE
# in the root directory or <http://www.gnu.org/licenses/gpl-3.0.html>.


# The name of our project is "TEXMACS". CMakeLists files in this project can
# refer to the root source directory of the project as ${ATHENA_SOURCE_DIR} and
# to the root binary directory of the project as ${ATHENA_BINARY_DIR}.


### --------------------------------------------------------------------
### Basic settings (project independent)
### --------------------------------------------------------------------

cmake_minimum_required (VERSION 3.13)

set (CMAKE_CXX_STANDARD 17)
set (CMAKE_AUTOMOC ON)
set (CMAKE_INCLUDE_CURRENT_DIR ON)

if (NOT CMAKE_BUILD_TYPE)
  set (CMAKE_BUILD_TYPE Debug CACHE STRING
    "Choose the type of build, options are: None Debug Release RelWithDebInfo MinSizeRel."
    FORCE)
endif (NOT CMAKE_BUILD_TYPE)

#INCLUDE(CMakeDetermineSystem)

# Configure CCache if available
find_program (CCACHE_FOUND ccache)
if (CCACHE_FOUND)
  set_property (GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
  set_property (GLOBAL PROPERTY RULE_LAUNCH_LINK ccache)
endif (CCACHE_FOUND)


### --------------------------------------------------------------------
### Project name
### --------------------------------------------------------------------

project (ATHENA CXX C) # check for C/C++ compiler

set (PACKAGE ATHENA)
set (CMAKE_MODULE_PATH ${ATHENA_SOURCE_DIR}/cmake)

set (VERSION_MAJOR "2")
set (VERSION_MINOR "1")
set (VERSION_BUILD "4")
set (TEXMACS_COMPAT_VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_BUILD})
set (ATHENA_APP_VERSION "0.8")
set (ATHENA_VERSION ${ATHENA_APP_VERSION})

set (DEVEL_VERSION ${ATHENA_VERSION})
set (DEVEL_RELEASE "1")
set (STABLE_VERSION ${ATHENA_VERSION})
set (STABLE_RELEASE "1")
set (ATHENA_REVISION ${ATHENA_VERSION})

if (CMAKE_BUILD_TYPE STREQUAL "")
  set (ATHENA_BUILD_TYPE "Default")
else ()
  set (ATHENA_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
endif ()
set (ATHENA_COMPILER
  "${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")


### --------------------------------------------------------------------
### Options
### --------------------------------------------------------------------
if (NOT ATHENA_GUI)
  set (ATHENA_GUI "Qt" CACHE STRING "ATHENA GUI (Qt6)")
endif (NOT ATHENA_GUI)
if (NOT (ATHENA_GUI STREQUAL "Qt" OR ATHENA_GUI STREQUAL "Qt6"))
  message (FATAL_ERROR
    "Unsupported ATHENA_GUI='${ATHENA_GUI}'. "
    "ATHENA requires Qt6; use ATHENA_GUI=Qt or ATHENA_GUI=Qt6.")
endif ()

find_package (mimalloc REQUIRED)
find_package (Boost REQUIRED)

option (QTPIPES "use Qt pipes" ON)

option (ATHENA_ENABLE_TSAN
  "Instrument ATHENA and its private Guile runtime with ThreadSanitizer" OFF)

set (ATHENA_CPU_TARGET "native" CACHE STRING
  "CPU ISA target: native, x86-64-v3, x86-64-v2, or x86-64")
set_property (CACHE ATHENA_CPU_TARGET PROPERTY STRINGS
  native x86-64-v3 x86-64-v2 x86-64)
if (ATHENA_CPU_TARGET STREQUAL "native")
  set (ATHENA_CPU_COMPILE_FLAGS "-march=native -mavx2")
elseif (ATHENA_CPU_TARGET MATCHES "^x86-64(-v[23])?$")
  set (ATHENA_CPU_COMPILE_FLAGS
    "-march=${ATHENA_CPU_TARGET} -mtune=generic")
else ()
  message (FATAL_ERROR
    "Unsupported ATHENA_CPU_TARGET='${ATHENA_CPU_TARGET}'. "
    "Use native, x86-64-v3, x86-64-v2, or x86-64.")
endif ()
message (STATUS
  "ATHENA CPU target: ${ATHENA_CPU_TARGET} (${ATHENA_CPU_COMPILE_FLAGS})")

function (athena_write_build_fingerprint path content)
  set (previous "")
  if (EXISTS "${path}")
    file (READ "${path}" previous)
  endif ()
  if (NOT previous STREQUAL "${content}")
    file (WRITE "${path}" "${content}")
  endif ()
endfunction ()

option (ATHENA_BUNDLE_CODEX
  "Bundle a redistributable Codex executable with ATHENA" OFF)
set (ATHENA_CODEX_BINARY "" CACHE FILEPATH
  "Codex executable to bundle when ATHENA_BUNDLE_CODEX is enabled")
set (ATHENA_CODEX_LICENSE_FILE "" CACHE FILEPATH
  "Codex license file to bundle when ATHENA_BUNDLE_CODEX is enabled")
if (ATHENA_BUNDLE_CODEX)
  if (NOT EXISTS "${ATHENA_CODEX_BINARY}")
    message (FATAL_ERROR
      "ATHENA_BUNDLE_CODEX requires ATHENA_CODEX_BINARY")
  endif ()
  if (NOT EXISTS "${ATHENA_CODEX_LICENSE_FILE}")
    message (FATAL_ERROR
      "ATHENA_BUNDLE_CODEX requires ATHENA_CODEX_LICENSE_FILE")
  endif ()
endif ()

find_package (SQLite3 REQUIRED)
set (USE_SQLITE3 ON CACHE BOOL "Use SQLite3" FORCE)
set (LINKED_SQLITE3 ON CACHE BOOL "Use Linked SQLite3" FORCE)

find_package (Python3 REQUIRED COMPONENTS Interpreter)
find_program (UV_EXECUTABLE uv REQUIRED)
if (NOT APPLE AND NOT WIN32)
  find_program (PATCHELF_EXECUTABLE patchelf REQUIRED)
endif ()

set (ATHENA_DATA_ART_TOOL_DIR
  "${CMAKE_SOURCE_DIR}/ATHENA/tools/data-art")
set (ATHENA_DATA_ART_VENV_STAMP
  "${ATHENA_DATA_ART_TOOL_DIR}/.venv/.athena-uv-sync-stamp")
set (ATHENA_UV_CACHE_DIR
  "${CMAKE_BINARY_DIR}/athena-uv-cache")
add_custom_command (
  OUTPUT ${ATHENA_DATA_ART_VENV_STAMP}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${ATHENA_UV_CACHE_DIR}
  COMMAND ${CMAKE_COMMAND} -E env
          "UV_CACHE_DIR=${ATHENA_UV_CACHE_DIR}"
          "UV_PROJECT_ENVIRONMENT=${ATHENA_DATA_ART_TOOL_DIR}/.venv"
          ${UV_EXECUTABLE} sync --locked --no-dev
          --project ${ATHENA_DATA_ART_TOOL_DIR}
  COMMAND ${CMAKE_COMMAND} -E touch ${ATHENA_DATA_ART_VENV_STAMP}
  DEPENDS
          ${ATHENA_DATA_ART_TOOL_DIR}/pyproject.toml
          ${ATHENA_DATA_ART_TOOL_DIR}/uv.lock
  WORKING_DIRECTORY ${ATHENA_DATA_ART_TOOL_DIR}
  COMMENT "Synchronizing ATHENA DataArt Python environment"
  VERBATIM)
add_custom_target (athena-data-art-python ALL
  DEPENDS ${ATHENA_DATA_ART_VENV_STAMP})

find_package (Freetype REQUIRED)

option (USE_IMLIB2 "use Imlib2" OFF)
if (USE_IMLIB2)
  find_package(Imlib2)
  option (LINKED_IMLIB2 "linked Imlib2" ON)
endif (USE_IMLIB2)

option (PDFHUMMUS_NO_TIFF "Disable TIFF in PDF Hummus" ON)

set (USE_GMP ON CACHE BOOL "use GMP" FORCE)
find_package (GMP REQUIRED)

set (USE_GNUTLS ON CACHE BOOL "use GnuTLS" FORCE)
find_package (GnuTLS REQUIRED)
set (LINKED_GNUTLS ON CACHE BOOL "Use Linked GnuTLS" FORCE)

if (WIN32)
  option (OS_MINGW "Using OS MINGW" ON)
  if (CMAKE_SIZEOF_VOID_P EQUAL 8)
    set (OS_MINGW64 1)
  endif ()
else (WIN32)
  option (OS_MINGW "Using OS MINGW" OFF)
endif (WIN32)

if (APPLE)
  set (MACOSX_EXTENSIONS 1)
  set (OS_MACOS 1)
  option (MACOS_QT_MENU "Disable MacOS native menu" OFF)
endif (APPLE)


### --------------------------------------------------------------------
### Include standard modules
### --------------------------------------------------------------------

include (CheckFunctionExists)
include (CheckLibraryExists)
include (CheckIncludeFile)
include (CheckTypeSize)
include (FindPkgConfig)
include (CheckCSourceCompiles)

pkg_check_modules (SPDLOG REQUIRED IMPORTED_TARGET spdlog)
pkg_check_modules (HARFBUZZ REQUIRED IMPORTED_TARGET harfbuzz)
pkg_check_modules (LLAMA QUIET IMPORTED_TARGET llama)
pkg_check_modules (KPATHSEA QUIET IMPORTED_TARGET kpathsea)
if (UNIX AND NOT APPLE)
  pkg_check_modules (FONTCONFIG REQUIRED IMPORTED_TARGET fontconfig)
  add_definitions (-DUSE_FONTCONFIG)
endif ()
set (LLAMA_CPP_SOURCE_DIR "" CACHE PATH
  "llama.cpp source tree, needed for common/common.h")
if (LLAMA_CPP_SOURCE_DIR STREQUAL "" AND DEFINED ENV{LLAMA_CPP_SOURCE_DIR})
  set (LLAMA_CPP_SOURCE_DIR "$ENV{LLAMA_CPP_SOURCE_DIR}")
endif ()
find_path (LLAMA_COMMON_INCLUDE_DIR common.h
  HINTS
    ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES common)
find_path (LLAMA_JSON_INCLUDE_DIR nlohmann/json_fwd.hpp
  HINTS
    ${LLAMA_CPP_SOURCE_DIR}
    ${LLAMA_COMMON_INCLUDE_DIR}/..
  PATH_SUFFIXES vendor)
find_path (LLAMA_CPP_INCLUDE_DIR llama.h
  HINTS
    ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES include
  NO_DEFAULT_PATH)
find_library (LLAMA_COMMON_LIBRARY
  NAMES llama-common common
  HINTS
    ${LLAMA_LIBRARY_DIRS}
    ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/common build/bin)
find_library (LLAMA_CPP_LIBRARY
  NAMES llama
  HINTS
    ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/bin
  NO_DEFAULT_PATH)
find_library (LLAMA_GGML_LIBRARY
  NAMES ggml
  HINTS ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/bin
  NO_DEFAULT_PATH)
find_library (LLAMA_GGML_BASE_LIBRARY
  NAMES ggml-base
  HINTS ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/bin
  NO_DEFAULT_PATH)
find_library (LLAMA_GGML_CPU_LIBRARY
  NAMES ggml-cpu
  HINTS ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/bin
  NO_DEFAULT_PATH)
find_library (LLAMA_GGML_SYCL_LIBRARY
  NAMES ggml-sycl
  HINTS ${LLAMA_CPP_SOURCE_DIR}
  PATH_SUFFIXES lib lib64 build/bin
  NO_DEFAULT_PATH)
if (NOT LLAMA_CPP_SOURCE_DIR STREQUAL "" AND
    LLAMA_CPP_LIBRARY AND LLAMA_CPP_INCLUDE_DIR)
  # Keep llama.h, common.a, and libllama from one source tree. Mixing the
  # pkg-config runtime with a source-tree common library is ABI-unsafe and also
  # prevents source-tree GPU backends such as SYCL from being used at runtime.
  if (NOT LLAMA_GGML_LIBRARY OR NOT LLAMA_GGML_BASE_LIBRARY)
    message (FATAL_ERROR
      "The selected llama.cpp source build has no matching ggml runtime.")
  endif ()
  set (ATHENA_LLAMA_LIBRARIES
    ${LLAMA_COMMON_LIBRARY}
    ${LLAMA_CPP_LIBRARY}
    ${LLAMA_GGML_LIBRARY})
  if (LLAMA_GGML_CPU_LIBRARY)
    list (APPEND ATHENA_LLAMA_LIBRARIES ${LLAMA_GGML_CPU_LIBRARY})
  endif ()
  if (LLAMA_GGML_SYCL_LIBRARY)
    list (APPEND ATHENA_LLAMA_LIBRARIES ${LLAMA_GGML_SYCL_LIBRARY})
  endif ()
  list (APPEND ATHENA_LLAMA_LIBRARIES ${LLAMA_GGML_BASE_LIBRARY})
  set (ATHENA_LLAMA_INCLUDE_DIR ${LLAMA_CPP_INCLUDE_DIR})
elseif (LLAMA_FOUND)
  set (ATHENA_LLAMA_LIBRARIES
    PkgConfig::LLAMA
    ${LLAMA_COMMON_LIBRARY})
  set (ATHENA_LLAMA_INCLUDE_DIR "")
else ()
  message (FATAL_ERROR
    "llama.cpp is required. Provide a coherent source build through "
    "-DLLAMA_CPP_SOURCE_DIR or install its pkg-config package.")
endif ()
if (NOT LLAMA_COMMON_INCLUDE_DIR OR NOT LLAMA_COMMON_LIBRARY)
  message (FATAL_ERROR
    "llama.cpp common support is required for ATHENA RAG embeddings. "
    "Install/build llama.cpp common, or configure with "
    "-DLLAMA_CPP_SOURCE_DIR=/path/to/llama.cpp. "
    "Found common.h='${LLAMA_COMMON_INCLUDE_DIR}', "
    "library='${LLAMA_COMMON_LIBRARY}'.")
endif ()
if (NOT LLAMA_JSON_INCLUDE_DIR)
  message (FATAL_ERROR
    "llama.cpp chat templates require its vendored nlohmann JSON headers. "
    "Configure LLAMA_CPP_SOURCE_DIR to the matching llama.cpp source tree.")
endif ()

find_path (RESVGQT_INCLUDE_DIR ResvgQt.h)
find_library (RESVG_LIBRARY resvg)
if (NOT RESVGQT_INCLUDE_DIR OR NOT RESVG_LIBRARY)
  message (FATAL_ERROR
    "resvg and ResvgQt.h are required for ATHENA SVG rendering. "
    "Install resvg/ResvgQt development packages or provide "
    "RESVGQT_INCLUDE_DIR/RESVG_LIBRARY.")
endif ()
find_path (TCC_INCLUDE_DIR libtcc.h)
find_library (TCC_LIBRARY NAMES tcc libtcc)
if (NOT TCC_INCLUDE_DIR OR NOT TCC_LIBRARY)
  message (FATAL_ERROR
    "libtcc is required for ATHENA namespace sorters. "
    "Install libtcc-devel or provide TCC_INCLUDE_DIR/TCC_LIBRARY.")
endif ()


### --------------------------------------------------------------------
### Check for standard functionalities
### --------------------------------------------------------------------
check_type_size (FILE HAVE_FILE)
check_type_size (intptr_t HAVE_INTPTR_T)
check_type_size (time_t HAVE_TIME_T)
check_include_file (inttypes.h HAVE_INTTYPES_H)
check_include_file (memory.h HAVE_MEMORY_H)
check_include_file (stdlib.h HAVE_STDLIB_H)
check_include_file (strings.h HAVE_STRINGS_H)
check_include_file (string.h HAVE_STRING_H)
check_include_file (sys/stat.h HAVE_SYS_STAT_H)
check_include_file (unistd.h HAVE_UNISTD_H)
check_include_file (pty.h HAVE_PTY_H)
check_include_file (util.h HAVE_UTIL_H)
check_function_exists (gettimeofday HAVE_GETTIMEOFDAY)


### --------------------------------------------------------------------
### Check for dl library
### --------------------------------------------------------------------

check_function_exists (dlopen HAVE_DLOPEN)

if (NOT ${HAVE_DLOPEN})
  check_library_exists (dl dlopen "" HAVE_DLOPEN)
  if (${HAVE_DLOPEN})
    set (CONFIG_BDL "-ldl")
  endif (${HAVE_DLOPEN})
endif (NOT ${HAVE_DLOPEN})

if (NOT ${HAVE_DLOPEN})
  check_library_exists (dld dld_link "" HAVE_DLOPEN)
  if (${HAVE_DLOPEN})
    set (CONFIG_BDL "-ldl")
  endif (${HAVE_DLOPEN})
endif (NOT ${HAVE_DLOPEN})

if (NOT ${HAVE_DLOPEN})
  check_function_exists (shl_load HAVE_DLOPEN)
  if (${HAVE_DLOPEN})
    set (CONFIG_BDL "-ldl")
  endif (${HAVE_DLOPEN})
endif (NOT ${HAVE_DLOPEN})


if (${HAVE_DLOPEN})
  #SET(TM_DYNAMIC_LINKING 1) # dynamic linking works
endif (${HAVE_DLOPEN})

### --------------------------------------------------------------------
### Handle different systems case by case
### --------------------------------------------------------------------

set (CONFIG_OS "GNU_LINUX")
set (CONFIG_OS_SUFFIX "gnu-linux")
set (CONFIG_CXXTEMPLATE "")
set (CONFIG_STD_SETENV "#define STD_SETENV")
set (CONFIG_SO "so")
set (CONFIG_LIB_PATH "LD_LIBRARY_PATH")
set (CONFIG_CHMOD "chmod -f")
set (CONFIG_CXXOPTIMIZE "-O2")
set (CONFIG_BSTATIC "-Wl,-Bstatic")
set (CONFIG_BSHARED "-Wl,-Bdynamic")
set (CONFIG_BFLAGS "")
set (CONFIG_BPATH "-Wl,-rpath,")
set (CONFIG_WORD_LENGTH "4")
set (CONFIG_WORD_LENGTH_INC "3")
set (CONFIG_WORD_MASK "0xfffffffc")
set (CONFIG_MAX_FAST "260 // WORD_LENGTH more than power of 2")

check_type_size ("void*" SIZE_OF_VOIDP)

message (STATUS "Check if we are on a 64-bits computer")
if (${SIZE_OF_VOIDP} EQUAL 8)
  set (CONFIG_WORD_LENGTH "8")
  set (CONFIG_WORD_LENGTH_INC "7")
  set (CONFIG_WORD_MASK "0xfffffffffffffff8")
  set (CONFIG_MAX_FAST "264 // WORD_LENGTH more than power of 2")
endif (${SIZE_OF_VOIDP} EQUAL 8)


if (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
  message (STATUS "final adjustments for an Intel or AMD GNU/Linux host")
  set (CONFIG_CXXOPTIMIZE "-O3 -fexpensive-optimizations")
endif (${CMAKE_SYSTEM_NAME} STREQUAL "Linux")

if (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
  message (STATUS "final adjustments for an Intel or AMD GNU/BSD host")
  set (CONFIG_CXXOPTIMIZE "-O3 -fexpensive-optimizations")
endif (${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")

if (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")
  message (STATUS "final adjustments for an Intel or AMS Solaris host")
  set (CONFIG_CXXOPTIMIZE "-O3 -fexpensive-optimizations")
  set (CONFIG_CXXOPTIMIZE "-O3")
  set (CONFIG_BPATH "-Wl,-R,")
endif (${CMAKE_SYSTEM_NAME} STREQUAL "SunOS")

if (${CMAKE_SYSTEM_NAME} STREQUAL "CYGWIN_NT-5.1")
  message (STATUS "final adjustments for cygwin host")
  set (CONFIG_OS "CYGWIN")
  set (CONFIG_BFLAGS "-Wl,-stack,8388608")
endif (${CMAKE_SYSTEM_NAME} STREQUAL "CYGWIN_NT-5.1")

### --------------------------------------------------------------------
### Package Data
### --------------------------------------------------------------------

set (PACKAGE_BUGREPORT 1)
set (PACKAGE_NAME 1)
set (PACKAGE_STRING 1)
set (PACKAGE_TARNAME 1)
set (PACKAGE_URL 1)
set (PACKAGE_VERSION 1)

### --------------------------------------------------------------------
### Memory allocation options
### --------------------------------------------------------------------

#if(${DISABLE_FASTALLOC})
#  set(NO_FAST_ALLOC 1)
#endif(${DISABLE_FASTALLOC})


### --------------------------------------------------------------------
### Experimental options
### --------------------------------------------------------------------

#  SET(EXPERIMENTAL 1)

### --------------------------------------------------------------------
###  Test for Libraries
### --------------------------------------------------------------------

find_package (PNG REQUIRED)
find_package (Iconv REQUIRED)
set (USE_ICONV ON CACHE BOOL "Use Iconv" FORCE)
find_package (ZLIB REQUIRED)
find_package (JPEG REQUIRED)
pkg_check_modules (ZSTD REQUIRED libzstd)
pkg_check_modules (LIBSODIUM REQUIRED libsodium)
pkg_check_modules (MAGICKWAND REQUIRED MagickWand)
add_definitions (${MAGICKWAND_CFLAGS_OTHER})
### --------------------------------------------------------------------
### ATHENA's private Guile 3 runtime
### --------------------------------------------------------------------

include(AthenaGuile)
set(CONFIG_GUILE_SERIAL "ATHENA-GUILE3")
set(ATHENA_GUILE3 1)
set(SCM_SIZET size_t)
set(CMAKE_REQUIRED_INCLUDES "${ATHENA_GUILE_INCLUDE_DIR}")
set (guile_str_size_t size_t)

### --------------------------------------------------------------------
### Generate configure headers
### --------------------------------------------------------------------
set (DEBUG_ASSERT 1)

set (USE_GS 1)
find_program (GS_EXECUTABLE NAMES gs gswin64c gswin32c REQUIRED)
set (GS_EXE ${GS_EXECUTABLE})
set (GS_FONTS ../share/ghostscript/fonts:/usr/share/fonts:)
set (GS_LIB ../share/ghostscript/9.06/lib:)

set (GUILE_NUM 1)
set (PDF_RENDERER 1)
set (SIZEOF_VOID_P 8)
set (STDC_HEADERS 1)

if (WIN32)
  option (USE_STACK_TRACE "Use stack trace" OFF)
else (WIN32)
  option (USE_STACK_TRACE "Use stack trace" ON)
endif (WIN32)

set (CONFIG_STD_SETENV "#define STD_SETENV")
set (tm_orig ${ATHENA_SOURCE_DIR})
set (CONFIG_HOST_OS "linux-gnu") # TODO: should not be hard-coded
set (CONFIG_HOST_VENDOR "pc") # TODO: should not be hard-coded
set (CONFIG_HOST_CPU ${CMAKE_SYSTEM_PROCESSOR})
set (CONFIG_USER $ENV{USER})
string (TIMESTAMP CONFIG_DATE)
set (tm_devel ATHENA-${DEVEL_VERSION})
set (tm_stable ATHENA-${STABLE_VERSION})
set (tm_devel_release ${tm_devel}-${DEVEL_RELEASE})
set (tm_stable_release ${tm_stable}-${STABLE_RELEASE})

set (prefix ${CMAKE_INSTALL_PREFIX})
set (exec_prefix ${prefix})
set (datarootdir ${prefix}/share)
set (datadir ${datarootdir})
set (tmdata ${datarootdir}/ATHENA)
set (tmbin ${exec_prefix}/lib/athena/ATHENA)

set (STACK_SIZE 0x8000000)

configure_file (${ATHENA_SOURCE_DIR}/src/System/config.h.cmake ${ATHENA_BINARY_DIR}/src/System/config.h)
configure_file (${ATHENA_SOURCE_DIR}/src/System/tm_configure.hpp.cmake ${ATHENA_BINARY_DIR}/src/System/tm_configure.hpp)

set (ATHENA_BUILD_INFO_HEADER
  "${ATHENA_BINARY_DIR}/src/System/athena_build_info.hpp")
set (ATHENA_BUILD_INFO_COMMAND
  ${CMAKE_COMMAND}
  "-DATHENA_SOURCE_DIR=${ATHENA_SOURCE_DIR}"
  "-DATHENA_OUTPUT=${ATHENA_BUILD_INFO_HEADER}"
  "-DATHENA_APP_VERSION=${ATHENA_APP_VERSION}"
  "-DATHENA_BUILD_TYPE=${ATHENA_BUILD_TYPE}"
  "-DATHENA_COMPILER=${ATHENA_COMPILER}"
  -P "${ATHENA_SOURCE_DIR}/cmake/GenerateAthenaBuildInfo.cmake")
execute_process (COMMAND ${ATHENA_BUILD_INFO_COMMAND})
add_custom_target (athena_build_info
  COMMAND ${ATHENA_BUILD_INFO_COMMAND}
  BYPRODUCTS "${ATHENA_BUILD_INFO_HEADER}"
  COMMENT "Refreshing ATHENA build identity"
  VERBATIM)

set (CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -include ${ATHENA_BINARY_DIR}/src/System/config.h")

option (ATHENA_INTEL_NATIVE_OPTIMIZATION
        "Use native CPU tuning when building with IntelLLVM" ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM" AND
    ATHENA_INTEL_NATIVE_OPTIMIZATION AND NOT ATHENA_ENABLE_TSAN)
  message(STATUS "Detected IntelLLVM compiler, adding aggressive optimization flags")
  # Release builders select a portable ISA baseline explicitly. Local builds
  # retain native tuning by default.
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 ${ATHENA_CPU_COMPILE_FLAGS} -funroll-loops -flto=thin -fp-model fast")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 ${ATHENA_CPU_COMPILE_FLAGS} -funroll-loops -flto=thin -fp-model fast")
  
  # Linker flags: Use lld, ThinLTO, and enable caching in the build directory
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto=thin -fuse-ld=lld -Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto_cache -static-intel")
  set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -flto=thin -fuse-ld=lld -Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto_cache -static-intel")
endif()

if (ATHENA_ENABLE_TSAN)
  if (NOT CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|IntelLLVM")
    message (FATAL_ERROR
      "ATHENA_ENABLE_TSAN requires a compiler with -fsanitize=thread support")
  endif ()
  message (STATUS "ATHENA ThreadSanitizer instrumentation enabled")
  set (ATHENA_TSAN_FLAGS
    "-O1 -g -fsanitize=thread -fno-omit-frame-pointer")
  string (APPEND CMAKE_C_FLAGS " ${ATHENA_TSAN_FLAGS}")
  string (APPEND CMAKE_CXX_FLAGS " ${ATHENA_TSAN_FLAGS}")
  string (APPEND CMAKE_EXE_LINKER_FLAGS
    " -fsanitize=thread -fuse-ld=lld")
  string (APPEND CMAKE_SHARED_LINKER_FLAGS
    " -fsanitize=thread -fuse-ld=lld")
endif ()


### --------------------------------------------------------------------
### the include dirs
### --------------------------------------------------------------------
set (ATHENA_Include_Dirs
  ${ATHENA_BINARY_DIR}/src/System/
  ${ATHENA_SOURCE_DIR}/src/Data/Convert
  ${ATHENA_SOURCE_DIR}/src/Data/Document
  ${ATHENA_SOURCE_DIR}/src/Data/Drd
  ${ATHENA_SOURCE_DIR}/src/Data/History
  ${ATHENA_SOURCE_DIR}/src/Data/Observers
  ${ATHENA_SOURCE_DIR}/src/Data/String
  ${ATHENA_SOURCE_DIR}/src/Data/Tree
  ${ATHENA_SOURCE_DIR}/src/Edit
  ${ATHENA_SOURCE_DIR}/src/Edit/Editor
  ${ATHENA_SOURCE_DIR}/src/Edit/Interface
  ${ATHENA_SOURCE_DIR}/src/Edit/Modify
  ${ATHENA_SOURCE_DIR}/src/Edit/Process
  ${ATHENA_SOURCE_DIR}/src/Edit/Replace
  ${ATHENA_SOURCE_DIR}/src/Graphics/Bitmap_fonts
  ${ATHENA_SOURCE_DIR}/src/Graphics/Colors
  ${ATHENA_SOURCE_DIR}/src/Graphics/Fonts
  ${ATHENA_SOURCE_DIR}/src/Graphics/Gui
  ${ATHENA_SOURCE_DIR}/src/Graphics/Handwriting
  ${ATHENA_SOURCE_DIR}/src/Graphics/Mathematics
  ${ATHENA_SOURCE_DIR}/src/Graphics/Pictures
  ${ATHENA_SOURCE_DIR}/src/Graphics/Renderer
  ${ATHENA_SOURCE_DIR}/src/Graphics/Spacial
  ${ATHENA_SOURCE_DIR}/src/Graphics/Types
  ${ATHENA_SOURCE_DIR}/src/Kernel/Abstractions
  ${ATHENA_SOURCE_DIR}/src/Kernel/Containers
  ${ATHENA_SOURCE_DIR}/src/Kernel/Types
  ${ATHENA_SOURCE_DIR}/src/Subsystems
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Google
  ${ATHENA_SOURCE_DIR}/src/Subsystems/PEGLib
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Pdf/LibAesgm
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Gnutls
  ${ATHENA_SOURCE_DIR}/src/Subsystems/MCP
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt
  ${ATHENA_SOURCE_DIR}/src/Subsystems/RAG
  ${ATHENA_SOURCE_DIR}/src/Scheme
  ${ATHENA_SOURCE_DIR}/src/Scheme/Guile
  ${ATHENA_SOURCE_DIR}/src/Scheme/Scheme
  ${ATHENA_SOURCE_DIR}/src/Style/Environment
  ${ATHENA_SOURCE_DIR}/src/Style/Evaluate
  ${ATHENA_SOURCE_DIR}/src/Style/Memorizer
  ${ATHENA_SOURCE_DIR}/src/System
  ${ATHENA_SOURCE_DIR}/src/System/Boot
  ${ATHENA_SOURCE_DIR}/src/System/Classes
  ${ATHENA_SOURCE_DIR}/src/System/Files
  ${ATHENA_SOURCE_DIR}/src/System/Language
  ${ATHENA_SOURCE_DIR}/src/System/Link
  ${ATHENA_SOURCE_DIR}/src/System/Misc
  ${ATHENA_SOURCE_DIR}/src/ATHENA
  ${ATHENA_SOURCE_DIR}/src/ATHENA/Data
  ${ATHENA_SOURCE_DIR}/src/Typeset
  ${ATHENA_SOURCE_DIR}/src/Typeset/Bridge
  ${ATHENA_SOURCE_DIR}/src/Typeset/Concat
  ${ATHENA_SOURCE_DIR}/src/Typeset/Page
  ${ATHENA_SOURCE_DIR}/ATHENA/include
)

if (WIN32)
  if (OS_MINGW64)
    set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs}
      ${ATHENA_SOURCE_DIR}/src/Subsystems/Windows64
    )
  else ()
    set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs}
      ${ATHENA_SOURCE_DIR}/src/Subsystems/Windows
      ${ATHENA_SOURCE_DIR}/src/Subsystems/Windows/nowide
    )
  endif ()
else (WIN32)
endif (WIN32)

set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs}
  ${ATHENA_GUILE_INCLUDE_DIR} ${FREETYPE_INCLUDE_DIRS}
  ${IMLIB2_INCLUDE_DIR} ${GMP_INCLUDES} ${GNUTLS_INCLUDE_DIRS}
  ${TCC_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIRS} ${ICONV_INCLUDE_DIR}
  ${ATHENA_LLAMA_INCLUDE_DIR} ${LLAMA_COMMON_INCLUDE_DIR}
  ${LLAMA_JSON_INCLUDE_DIR}
)

### --------------------------------------------------------------------
### the main sources
### --------------------------------------------------------------------
file (GLOB_RECURSE ATHENA_Base_SRCS CONFIGURE_DEPENDS
  "${ATHENA_SOURCE_DIR}/src/Data/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Edit/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Graphics/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Kernel/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Scheme/Scheme/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Scheme/Guile/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/System/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/ATHENA/Data/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/ATHENA/Math/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/ATHENA/Server/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/ATHENA/Window/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Typeset/*.cpp"
)

file (GLOB_RECURSE ATHENA_Subsystems_SRCS CONFIGURE_DEPENDS
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Database/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Freetype/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Google/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Gnutls/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Pdf/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Pdf/*.c"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Ghostscript/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Imlib2/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Ispell/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Metafont/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/LaTeX_Preview/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/MCP/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Mplayer/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Openssl/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/RAG/*.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Sqlite3/*.cpp"
)

file (GLOB_RECURSE ATHENA_Style_SRCS
  "${ATHENA_SOURCE_DIR}/src/Style/*.cpp"
)

set (ATHENA_All_SRCS ${ATHENA_Base_SRCS}  ${ATHENA_Subsystems_SRCS})

### --------------------------------------------------------------------
### GUI Interfaces
### --------------------------------------------------------------------

file (GLOB_RECURSE ATHENA_Qt_SRCS CONFIGURE_DEPENDS "${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/*.cpp")

set (ATHENA_Qt_Moc_HDRS
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMAbout.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMApplication.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMFileDialog.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMGuiHelper.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMInteractiveInputHelper.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMInteractivePrompt.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMMenuHelper.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMPipeLink.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMPrintDialog.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMPrinterSettings.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMScrollView.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMStyle.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMTreeModel.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMVaultChooser.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMWidget.hpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMWindow.hpp
)

set (ATHENA_MacOS_SRCS
  ${ATHENA_SOURCE_DIR}/src/Subsystems/MacOS/HIDRemote.m
  ${ATHENA_SOURCE_DIR}/src/Subsystems/MacOS/mac_images.mm
  ${ATHENA_SOURCE_DIR}/src/Subsystems/MacOS/mac_spellservice.mm
  ${ATHENA_SOURCE_DIR}/src/Subsystems/MacOS/mac_utilities.mm
)

### --------------------------------------------------------------------
### Determine ATHENA_Libraries
### --------------------------------------------------------------------

if (APPLE AND EXISTS /opt/homebrew/lib)
  link_directories (/opt/homebrew/lib)
endif ()

set (ATHENA_Libraries
  ATHENA::Guile
  ${ZLIB_LIBRARIES}
  ${JPEG_LIBRARIES}
  ${PNG_LIBRARIES}
  ${ZSTD_LIBRARIES}
  ${LIBSODIUM_LIBRARIES}
  ${MAGICKWAND_LIBRARIES}
  ${ICONV_LIBRARIES}
  ${TCC_LIBRARY}
  ${CMAKE_DL_LIBS}
  -lpthread
)

if (LINKED_SQLITE3)
  set (ATHENA_Libraries ${ATHENA_Libraries} ${SQLITE3_LIBRARIES})
endif (LINKED_SQLITE3)

set (ATHENA_Libraries ${ATHENA_Libraries} ${FREETYPE_LIBRARIES})
set (ATHENA_Libraries ${ATHENA_Libraries} PkgConfig::HARFBUZZ)

if (UNIX AND NOT APPLE)
  set (ATHENA_Libraries ${ATHENA_Libraries} PkgConfig::FONTCONFIG)
endif ()

set (ATHENA_Libraries ${ATHENA_Libraries} ${GMP_LIBRARIES})

set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs} ${Boost_INCLUDE_DIRS})

if (LINKED_GNUTLS)
  set (ATHENA_Libraries ${ATHENA_Libraries} ${GNUTLS_LIBRARIES})
endif (LINKED_GNUTLS)

### --------------------------------------------------------------------
### GUI selection
### --------------------------------------------------------------------

set (GUI_TYPE) # nothing or WIN32, MACOSX bundles are treated independetly
find_package (KF6SyntaxHighlighting REQUIRED)

if (ATHENA_GUI STREQUAL "Qt" OR ATHENA_GUI STREQUAL "Qt6")
  find_package (Qt6 COMPONENTS Core Gui Widgets PrintSupport Svg Network Core5Compat REQUIRED)
  set (QT_LIBRARIES Qt6::Core Qt6::Gui Qt6::Widgets Qt6::PrintSupport Qt6::Svg Qt6::Network Qt6::Core5Compat)

  set (ATHENA_Include_Dirs
    ${ATHENA_Include_Dirs}
    ${Qt6Core_INCLUDE_DIRS}
    ${Qt6Gui_INCLUDE_DIRS}
    ${Qt6Widgets_INCLUDE_DIRS}
    ${Qt6PrintSupport_INCLUDE_DIRS}
    ${Qt6Svg_INCLUDE_DIRS}
    ${Qt6Network_INCLUDE_DIRS}
    ${Qt6Core5Compat_INCLUDE_DIRS}
  )
  if (Qt6_POSITION_INDEPENDENT_CODE)
    set (CMAKE_POSITION_INDEPENDENT_CODE ON)
  endif ()

  add_definitions ("-DUSE_KF6_SYNTAX_HIGHLIGHTING")

  if (UNIX AND NOT APPLE)
    option (USE_KF6_KIO_FILE_DIALOGS
            "Use KDE Frameworks KIO file dialogs" ON)
    if (USE_KF6_KIO_FILE_DIALOGS)
      find_package (KF6KIO REQUIRED)
      find_package (KF6ConfigWidgets REQUIRED)
      get_target_property (KF6_KIO_CORE_INCLUDE_DIRS
                           KF6::KIOCore
                           INTERFACE_INCLUDE_DIRECTORIES)
      get_target_property (KF6_KIO_GUI_INCLUDE_DIRS
                           KF6::KIOGui
                           INTERFACE_INCLUDE_DIRECTORIES)
      get_target_property (KF6_KIO_FILE_WIDGETS_INCLUDE_DIRS
                           KF6::KIOFileWidgets
                           INTERFACE_INCLUDE_DIRECTORIES)
      get_target_property (KF6_KIO_WIDGETS_INCLUDE_DIRS
                           KF6::KIOWidgets
                           INTERFACE_INCLUDE_DIRECTORIES)
      get_target_property (KF6_CONFIG_WIDGETS_INCLUDE_DIRS
                           KF6::ConfigWidgets
                           INTERFACE_INCLUDE_DIRECTORIES)
      list (GET KF6_KIO_FILE_WIDGETS_INCLUDE_DIRS 0
            KF6_KIO_FILE_WIDGETS_INCLUDE_DIR)
      get_filename_component (KF6_INCLUDE_ROOT
                              "${KF6_KIO_FILE_WIDGETS_INCLUDE_DIR}/.."
                              ABSOLUTE)
      add_definitions ("-DUSE_KF6")
      set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs}
           ${KF6_INCLUDE_ROOT}
           ${KF6_KIO_CORE_INCLUDE_DIRS}
           ${KF6_KIO_GUI_INCLUDE_DIRS}
           ${KF6_KIO_FILE_WIDGETS_INCLUDE_DIRS}
           ${KF6_KIO_WIDGETS_INCLUDE_DIRS}
           ${KF6_CONFIG_WIDGETS_INCLUDE_DIRS})
      set (ATHENA_Libraries ${ATHENA_Libraries}
           KF6::KIOFileWidgets
           KF6::ConfigWidgets)
    endif ()
  endif ()

  add_definitions (${QT_DEFINITIONS})
  add_definitions ("-DQTTEXMACS")

  add_definitions ("-DUSE_RESVGQT")
  set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs} ${RESVGQT_INCLUDE_DIR})
  set (ATHENA_Libraries ${ATHENA_Libraries} ${RESVG_LIBRARY})

  if (CMAKE_BUILD_TYPE STREQUAL "Release")
    add_definitions ("-DQT_NO_DEBUG")
  endif (CMAKE_BUILD_TYPE STREQUAL "Release")

  set (ATHENA_All_SRCS ${ATHENA_All_SRCS} ${ATHENA_Qt_SRCS} ${ATHENA_Qt_Moc_HDRS})
  set (ATHENA_Include_Dirs ${ATHENA_Include_Dirs} ${QT_INCLUDES})
  set (ATHENA_Libraries ${ATHENA_Libraries}  ${QT_LIBRARIES})

  if (WIN32)
    set (GUI_TYPE WIN32)
  endif (WIN32)

  set (QTTEXMACS 1)
  set (CONFIG_GUI "QT")
  set (CONFIG_QT "Qt")
  message (STATUS "Enabling Qt port")

  include(FetchContent)
  ### --------------------------------------------------------------------
  ### RapidFuzz C++
  ### --------------------------------------------------------------------
  FetchContent_Declare(
    rapidfuzz
    GIT_REPOSITORY https://github.com/rapidfuzz/rapidfuzz-cpp.git
    GIT_TAG        v3.3.3
  )
  set(RAPIDFUZZ_BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(RAPIDFUZZ_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(rapidfuzz)

  ### --------------------------------------------------------------------
  ### Snowball English stemmer (radioactive artifact links)
  ### --------------------------------------------------------------------
  FetchContent_Declare(
    snowball_libstemmer_c
    URL      https://snowballstem.org/dist/libstemmer_c-3.1.1.tar.gz
    URL_HASH SHA256=0e82fb0e29006cad2831b4b25980a1dae85959c6a1faaaf96547f551e352042a
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(snowball_libstemmer_c)
  add_library(athena_snowball_english STATIC
    ${snowball_libstemmer_c_SOURCE_DIR}/runtime/api.c
    ${snowball_libstemmer_c_SOURCE_DIR}/runtime/utilities.c
    ${snowball_libstemmer_c_SOURCE_DIR}/src_c/stem_UTF_8_english.c
  )
  target_include_directories(athena_snowball_english PUBLIC
    ${snowball_libstemmer_c_SOURCE_DIR}/runtime
    ${snowball_libstemmer_c_SOURCE_DIR}/src_c
  )

  ### --------------------------------------------------------------------
  ### ncnn (Hand TeX handwriting classifier)
  ### --------------------------------------------------------------------
  FetchContent_Declare(
    ncnn
    GIT_REPOSITORY https://github.com/Tencent/ncnn.git
    GIT_TAG        e54f7b1f88434e1d844ea0551b880a1cfb079ce1
  )
  set(NCNN_VULKAN OFF CACHE BOOL "" FORCE)
  # ATHENA may already host Intel SYCL and libraries using GNU OpenMP.  This
  # small classifier runs asynchronously and does not justify introducing an
  # additional OpenMP parallel region into that mixed-runtime process.
  set(NCNN_OPENMP OFF CACHE BOOL "" FORCE)
  set(NCNN_SHARED_LIB OFF CACHE BOOL "" FORCE)
  set(NCNN_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(NCNN_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(NCNN_BUILD_BENCHMARK OFF CACHE BOOL "" FORCE)
  set(NCNN_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(NCNN_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ncnn)

  ### --------------------------------------------------------------------
  ### Qt Advanced Docking System
  ### --------------------------------------------------------------------
  FetchContent_Declare(
    ads
    GIT_REPOSITORY https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System.git
    GIT_TAG        4.3.1
    PATCH_COMMAND  ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/patch_ads.py <SOURCE_DIR>
  )
  set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(ads)
  set(ADS_LIBRARIES qt6advanceddocking)
  if (NOT APPLE AND NOT WIN32)
    set_target_properties (${ADS_LIBRARIES} PROPERTIES
      SKIP_BUILD_RPATH TRUE
      BUILD_RPATH ""
      INSTALL_RPATH "")
  endif ()
endif ()

### --------------------------------------------------------------------
### OS System selection
### --------------------------------------------------------------------

if (WIN32)
  if (OS_MINGW64)
    file (GLOB_RECURSE ATHENA_System_SRCS "${ATHENA_SOURCE_DIR}/src/Subsystems/Windows64/*.cpp")
  else ()
    file (GLOB_RECURSE ATHENA_System_SRCS "${ATHENA_SOURCE_DIR}/src/Subsystems/Windows/*.cpp")
  endif ()
else (WIN32)
  file (GLOB_RECURSE ATHENA_System_SRCS "${ATHENA_SOURCE_DIR}/src/Subsystems/Unix/*.cpp")
endif (WIN32)

set (ATHENA_All_SRCS ${ATHENA_All_SRCS} ${ATHENA_System_SRCS})

if (APPLE)
  set (ATHENA_Libraries
    ${ATHENA_Libraries}
    "-framework ApplicationServices"
    "-framework CoreFoundation"
    "-framework Security"
    "-framework Carbon"
    "-framework AppKit"
    "-framework Cocoa"
    "-framework IOKit"
    "-framework CoreGraphics"
    "-framework CoreServices"
    "-framework CoreText"
    "-framework Foundation"
    "-framework ImageIO"
  )
  set (ATHENA_All_SRCS ${ATHENA_All_SRCS} ${ATHENA_MacOS_SRCS})

  set (GUI_TYPE MACOSX_BUNDLE)
  set (NO_FAST_ALLOC 1)
  message (STATUS "Enabling macOS Qt integration")
else (APPLE)
endif (APPLE)

include_directories (${ATHENA_Include_Dirs})
include_directories (${ZSTD_INCLUDE_DIRS})
include_directories (${LIBSODIUM_INCLUDE_DIRS})
include_directories (${MAGICKWAND_INCLUDE_DIRS})
link_directories (${MAGICKWAND_LIBRARY_DIRS})

### --------------------------------------------------------------------
### Debugging options
### --------------------------------------------------------------------


### --------------------------------------------------------------------
### Warning options
### --------------------------------------------------------------------

### --------------------------------------------------------------------
### Optimization options
### --------------------------------------------------------------------

### --------------------------------------------------------------------
### Compile sources
### --------------------------------------------------------------------

option (BUILD_TESTS "Build unit tests" OFF)
if (BUILD_TESTS)
  include (CTest)
  enable_testing ()
endif ()

add_subdirectory (src)
add_subdirectory (tools/codex-bridge)
add_subdirectory (tools/materials-engine)
if (UNIX AND NOT APPLE)
  add_subdirectory (tools/athena-transmitter)
  add_subdirectory (tools/athena-web-server)
endif ()
add_subdirectory (plugins)
add_subdirectory (misc)
add_subdirectory (ATHENA)

# Keep the pinned Materials schema beside build-tree executables, matching the
# installed ATHENA/misc layout used at runtime.
file (COPY "${CMAKE_SOURCE_DIR}/ATHENA/misc/materials"
      DESTINATION "${CMAKE_BINARY_DIR}/misc")

### --------------------------------------------------------------------
### Installation
### --------------------------------------------------------------------

### include/
install (DIRECTORY ATHENA/include DESTINATION ${CMAKE_INSTALL_PREFIX}
  FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ)

### share/
if (WIN32)
  install (DIRECTORY plugins DESTINATION ${CMAKE_INSTALL_PREFIX}
    FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    PATTERN "bin" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
    PATTERN ".gitignore" EXCLUDE)
else (WIN32)
  install (DIRECTORY plugins DESTINATION share/ATHENA
    FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
    PATTERN "bin" EXCLUDE
    PATTERN "CMakeLists.txt" EXCLUDE
    PATTERN ".gitignore" EXCLUDE)
endif (WIN32)

install (FILES ATHENA/misc/mime/athena.desktop DESTINATION share/applications)
install (FILES ATHENA/misc/images/athena.svg RENAME ATHENA.svg
  DESTINATION share/icons/hicolor/scalable/apps)
install (FILES ATHENA/misc/images/text-x-athena.svg
  DESTINATION share/icons/hicolor/scalable/mimetypes)
install (FILES ATHENA/misc/mime/athena.xml DESTINATION share/mime/packages)
install (FILES ATHENA/misc/pixmaps/ATHENA.xpm DESTINATION share/pixmaps)
install (DIRECTORY "${ATHENA_GUILE_PREFIX}/share/guile/3.0"
  DESTINATION share/ATHENA/lib/athena-guile/share/guile)
install (DIRECTORY "${ATHENA_GUILE_PREFIX}/lib/guile/3.0"
  DESTINATION share/ATHENA/lib/athena-guile/lib/guile)
install (DIRECTORY "${ATHENA_GUILE_PREFIX}/lib/"
  DESTINATION share/ATHENA/lib/athena-guile/lib
  FILES_MATCHING PATTERN "libathena-guile.so*")
foreach (size IN ITEMS 16 22 24 32 48 64 128 256 512)
  message (STATUS ${size})
  install (FILES ATHENA/misc/images/athena-${size}.png RENAME ATHENA.png
    DESTINATION share/icons/hicolor/${size}x${size}/apps/)
endforeach ()

### --------------------------------------------------------------------
### Testing
### --------------------------------------------------------------------

if (BUILD_TESTS)
  find_package(Qt6 COMPONENTS Test REQUIRED)
  set(ATHENA_QT_TEST_LIBRARY Qt6::Test)
  add_test(NAME athena_guile_native_runtime
    COMMAND ${CMAKE_COMMAND} -E env
      "LD_LIBRARY_PATH=${ATHENA_GUILE_PREFIX}/lib"
      "GUILE_LOAD_PATH=${CMAKE_SOURCE_DIR}/tests/scheme/native-runtime:${ATHENA_GUILE_STANDARD_LIBRARY}"
      "GUILE_LOAD_COMPILED_PATH=${ATHENA_GUILE_COMPILED_LIBRARY}"
      "GUILE_AUTO_COMPILE=0"
      ${ATHENA_GUILE_PREFIX}/bin/guile --no-auto-compile
      ${CMAKE_SOURCE_DIR}/tests/scheme/native-runtime/native-runtime-test.scm)
  if (NOT WIN32)
    add_test(NAME codex_bridge_protocol
      COMMAND ${Python3_EXECUTABLE}
              ${CMAKE_SOURCE_DIR}/tools/codex-bridge/tests/test_bridge.py
              $<TARGET_FILE:athena-codex-bridge>
              ${CMAKE_SOURCE_DIR}/tools/codex-bridge/tests/fake_codex.py)
  endif ()
  add_subdirectory (tests)
  # add_subdirectory (misc/benchmark)
endif (BUILD_TESTS)

### --------------------------------------------------------------------
### Check for qarma (optional but recommended for crash reporting)
### --------------------------------------------------------------------
find_program (QARMA_EXECUTABLE qarma)
if (QARMA_EXECUTABLE)
  message (STATUS "Found qarma: ${QARMA_EXECUTABLE}")
else ()
  message (STATUS "Could NOT find qarma (optional, but recommended for crash reporting)")
endif ()

message (STATUS "ATHENA_Libraries" ${ATHENA_Libraries})
