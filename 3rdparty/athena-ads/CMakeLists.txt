cmake_minimum_required(VERSION 3.13)

if (POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif (POLICY CMP0091)

project(QtADS LANGUAGES CXX VERSION 4.3.1)
set(VERSION_SHORT ${PROJECT_VERSION})

option(BUILD_STATIC "Build the static library" OFF)
option(BUILD_EXAMPLES "Build the examples" OFF)

if("${CMAKE_SIZEOF_VOID_P}" STREQUAL "4")
    set(ads_PlatformDir "x86")
else()
    set(ads_PlatformDir "x64")
endif()

add_subdirectory(src)

if(BUILD_EXAMPLES)
    add_subdirectory(examples)
    add_subdirectory(demo)
endif()

