# ATHENA iPadOS device cross-compilation toolchain.
#
# This file is intentionally device-only.  It targets arm64 iPhoneOS (the
# historical Apple SDK name shared by iPhone and iPad) with ATHENA's minimum
# iPadOS version.  Host programs remain discoverable, while target headers,
# libraries and packages are restricted to the Apple SDK plus the explicit
# ATHENA target dependency prefix.

set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_PROCESSOR arm64)

execute_process(
  COMMAND xcrun --sdk iphoneos --show-sdk-path
  OUTPUT_VARIABLE ATHENA_IPADOS_SDK
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND xcrun --sdk iphoneos --find clang
  OUTPUT_VARIABLE ATHENA_IPADOS_CLANG
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND xcrun --sdk iphoneos --find clang++
  OUTPUT_VARIABLE ATHENA_IPADOS_CLANGXX
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND xcrun --sdk iphoneos --find ar
  OUTPUT_VARIABLE ATHENA_IPADOS_AR
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND xcrun --sdk iphoneos --find ranlib
  OUTPUT_VARIABLE ATHENA_IPADOS_RANLIB
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

execute_process(
  COMMAND xcrun --sdk iphoneos --find nm
  OUTPUT_VARIABLE ATHENA_IPADOS_NM
  OUTPUT_STRIP_TRAILING_WHITESPACE
  COMMAND_ERROR_IS_FATAL ANY)

set(CMAKE_C_COMPILER "${ATHENA_IPADOS_CLANG}" CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER "${ATHENA_IPADOS_CLANGXX}" CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${ATHENA_IPADOS_AR}" CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${ATHENA_IPADOS_RANLIB}" CACHE FILEPATH "" FORCE)
set(CMAKE_NM "${ATHENA_IPADOS_NM}" CACHE FILEPATH "" FORCE)

set(CMAKE_OSX_SYSROOT "${ATHENA_IPADOS_SDK}" CACHE PATH "" FORCE)
set(CMAKE_OSX_ARCHITECTURES arm64 CACHE STRING "" FORCE)
set(CMAKE_OSX_DEPLOYMENT_TARGET 27.0 CACHE STRING "" FORCE)

set(ATHENA_IPADOS_TARGET_PREFIX "" CACHE PATH
  "Unified prefix containing arm64 iPhoneOS dependencies")
set(ATHENA_IPADOS_TARGET_PREFIXES "" CACHE STRING
  "Additional arm64 iPhoneOS dependency prefixes")

set(_athena_ipados_roots "${ATHENA_IPADOS_SDK}")
if(ATHENA_IPADOS_TARGET_PREFIX)
  list(APPEND _athena_ipados_roots "${ATHENA_IPADOS_TARGET_PREFIX}")
  list(PREPEND CMAKE_PREFIX_PATH "${ATHENA_IPADOS_TARGET_PREFIX}")
endif()
foreach(_athena_ipados_prefix IN LISTS ATHENA_IPADOS_TARGET_PREFIXES)
  if(_athena_ipados_prefix)
    list(APPEND _athena_ipados_roots "${_athena_ipados_prefix}")
    list(PREPEND CMAKE_PREFIX_PATH "${_athena_ipados_prefix}")
  endif()
endforeach()
list(REMOVE_DUPLICATES _athena_ipados_roots)
set(CMAKE_FIND_ROOT_PATH "${_athena_ipados_roots}" CACHE STRING "" FORCE)

# Build generators, Python, pkg-config and similar executables are host tools.
# Headers/libraries/CMake packages must never fall through to /opt/local,
# /usr/local or another macOS host prefix.
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_TRY_COMPILE_PLATFORM_VARIABLES
  CMAKE_OSX_SYSROOT
  CMAKE_OSX_ARCHITECTURES
  CMAKE_OSX_DEPLOYMENT_TARGET
  ATHENA_IPADOS_TARGET_PREFIX
  ATHENA_IPADOS_TARGET_PREFIXES)

