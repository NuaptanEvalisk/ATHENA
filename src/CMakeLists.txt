### --------------------------------------------------------------------
### Set the executable target name and other stuff
### --------------------------------------------------------------------

if (APPLE)
  set (ATHENA_binary_name "ATHENA")
elseif (WIN32)
  set (ATHENA_binary_name "ATHENA")
else (APPLE)
  set (ATHENA_binary_name "ATHENA.bin")
endif (APPLE)

list (REMOVE_ITEM ATHENA_All_SRCS
  "${ATHENA_SOURCE_DIR}/src/Data/String/unicode_text.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Plugins/subscription.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Plugins/package.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMPluginManager.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMPluginUi.cpp"
  "${ATHENA_SOURCE_DIR}/src/System/Misc/tsan_mimalloc_shim.cpp"
  "${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMAudmapTerminal.cpp")
add_library (athena_unicode STATIC Data/String/unicode_text.cpp)
set_target_properties (athena_unicode PROPERTIES AUTOMOC OFF)
target_include_directories (athena_unicode PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Data/String")
target_link_libraries (athena_unicode PUBLIC ICU::uc ICU::i18n PRIVATE PkgConfig::PANGO)
add_library (athena_audmap_terminal STATIC Subsystems/Qt/QTMAudmapTerminal.cpp)
target_compile_features (athena_audmap_terminal PRIVATE cxx_std_17)
target_link_libraries (athena_audmap_terminal PUBLIC Qt6::Widgets PRIVATE qtermwidget6)
include(AthenaGlue)
add_subdirectory(ATHENA/Interop)
add_subdirectory(Subsystems/Plugins)
add_library(athena_plugin_runtime STATIC Subsystems/Qt/QTMPluginManager.cpp Subsystems/Qt/QTMPluginManager.hpp)
set_target_properties(athena_plugin_runtime PROPERTIES AUTOMOC ON)
target_include_directories(athena_plugin_runtime PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/Subsystems/Qt")
target_link_libraries(athena_plugin_runtime PUBLIC athena_plugins athena_audmap Qt6::Core)
add_library(athena_plugin_ui STATIC Subsystems/Qt/QTMPluginUi.cpp)
set_target_properties(athena_plugin_ui PROPERTIES AUTOMOC OFF)
target_link_libraries(athena_plugin_ui PUBLIC athena_plugin_runtime Qt6::Widgets)
set_source_files_properties(Scheme/Scheme/glue.cpp PROPERTIES
  INCLUDE_DIRECTORIES "${ATHENA_BINARY_DIR}/generated")

function (athena_configure_body_target target_name)
  add_library (${target_name} STATIC ${ATHENA_All_SRCS})
  target_sources (${target_name} PRIVATE ./ATHENA/Watchdog/watchdog_client.cpp)
  add_dependencies (${target_name} athena_build_info athena_guile_runtime athena_glue)
  target_compile_definitions (${target_name} PRIVATE
    ATHENA_GUILE_BUILD_RUNTIME_ROOT="${ATHENA_GUILE_PREFIX}"
    ATHENA_GUILE_RUNTIME_ID="${ATHENA_GUILE_RUNTIME_ID}"
    $<$<CONFIG:Debug>:ATHENA_DEBUG_BUILD=1>
  )
  target_link_libraries (${target_name}
    PUBLIC
    athena_unicode
    athena_audmap
    PkgConfig::SPDLOG
    ${ATHENA_LLAMA_LIBRARIES}
    PRIVATE
    PkgConfig::HUNSPELL
    ${ADS_LIBRARIES}
    rapidfuzz::rapidfuzz
    athena_snowball_english
    ncnn
    KF6::SyntaxHighlighting
    KF6::Completion
    athena_audmap_terminal
    athena_plugin_ui
    ${ATHENA_VTK_LIBRARIES}
  )
  vtk_module_autoinit (
    TARGETS ${target_name}
    MODULES ${ATHENA_VTK_LIBRARIES})
endfunction ()

# Production body: compiled as ThinLTO bitcode when using the optimized local
# IntelLLVM configuration. ATHENA.bin is the only consumer which should pay the
# whole-program optimization/link cost.
athena_configure_body_target (athena_body)
if (CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM" AND
    ATHENA_INTEL_NATIVE_OPTIMIZATION AND NOT ATHENA_ENABLE_TSAN)
  target_compile_options (athena_body PRIVATE -flto=thin)
endif ()

# Test body: normal object code, compiled once and shared by all ordinary test
# executables. This deliberately avoids feeding the production ThinLTO archive
# into every small xxx_test link.
if (BUILD_TESTS)
  athena_configure_body_target (athena_test_body)
  target_compile_definitions (athena_test_body PRIVATE ATHENA_TEST_BODY=1)
endif ()

if (WIN32)
  add_executable (${ATHENA_binary_name} WIN32
    ./ATHENA/ATHENA/athena.cpp
  )
else (WIN32)
  add_executable (${ATHENA_binary_name}
    ./ATHENA/ATHENA/athena.cpp
  )
endif (WIN32)
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable (ATHENA-Watchdog
    ./ATHENA/Watchdog/watchdog_main.cpp)
  target_compile_features (ATHENA-Watchdog PRIVATE cxx_std_17)
  set_target_properties (ATHENA-Watchdog PROPERTIES AUTOMOC OFF)
endif ()
if (ATHENA_ENABLE_TSAN)
  target_sources (${ATHENA_binary_name} PRIVATE
    ./System/Misc/tsan_mimalloc_shim.cpp)
else ()
  target_link_libraries (${ATHENA_binary_name} mimalloc)
endif ()
target_compile_definitions(${ATHENA_binary_name} PRIVATE
  MI_OVERRIDE=1
  ATHENA_GUILE_RUNTIME_ID="${ATHENA_GUILE_RUNTIME_ID}"
  $<$<BOOL:${ATHENA_ENABLE_TSAN}>:ATHENA_TSAN_BUILD=1>
  $<$<CONFIG:Debug>:ATHENA_DEBUG_BUILD=1>
)
target_link_libraries (${ATHENA_binary_name}
  athena_body
  PkgConfig::SPDLOG
  ${ATHENA_LLAMA_LIBRARIES}
  ${ADS_LIBRARIES}
  ${ATHENA_Libraries}
)
if (CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM" AND
    ATHENA_INTEL_NATIVE_OPTIMIZATION AND NOT ATHENA_ENABLE_TSAN)
  target_compile_options (${ATHENA_binary_name} PRIVATE -flto=thin)
  target_link_options (${ATHENA_binary_name} PRIVATE
    -flto=thin
    "-Wl,--thinlto-cache-dir=${CMAKE_BINARY_DIR}/lto_cache")
endif ()

if (NOT APPLE AND NOT WIN32)
  add_custom_command (TARGET ${ATHENA_binary_name} POST_BUILD
    COMMAND ${PATCHELF_EXECUTABLE}
            --set-rpath "$ORIGIN/../lib/athena-guile/lib:$ORIGIN/../lib"
            $<TARGET_FILE:${ATHENA_binary_name}>
    COMMENT "Setting relocatable runtime library path for ATHENA"
    VERBATIM)
endif ()

if (NOT WIN32)
  set (ATHENA_SCHEME_COMPILE_JOBS "20" CACHE STRING
       "Parallel workers used to precompile ATHENA Scheme modules")
  file (GLOB_RECURSE ATHENA_SCHEME_SOURCES CONFIGURE_DEPENDS
        "${ATHENA_SOURCE_DIR}/ATHENA/progs/*.scm")
  list (APPEND ATHENA_SCHEME_SOURCES "${ATHENA_GLUE_RESOURCE}")
  list (REMOVE_DUPLICATES ATHENA_SCHEME_SOURCES)
  list (SORT ATHENA_SCHEME_SOURCES)
  string (JOIN "\n" ATHENA_SCHEME_SOURCE_MANIFEST_CONTENT
          ${ATHENA_SCHEME_SOURCES})
  string (APPEND ATHENA_SCHEME_SOURCE_MANIFEST_CONTENT "\n")
  set (ATHENA_SCHEME_SOURCE_MANIFEST
       "${ATHENA_BINARY_DIR}/athena-scheme-sources.txt")
  file (GENERATE OUTPUT "${ATHENA_SCHEME_SOURCE_MANIFEST}"
        CONTENT "${ATHENA_SCHEME_SOURCE_MANIFEST_CONTENT}")
  set (ATHENA_SCHEME_BYTECODE_DIR
       "${ATHENA_BINARY_DIR}/athena-scheme/${ATHENA_GUILE_RUNTIME_ID}")
  set (ATHENA_SCHEME_BYTECODE_STAMP
       "${ATHENA_SCHEME_BYTECODE_DIR}/.complete")
  set (ATHENA_SCHEME_COMPILE_HOME
       "${ATHENA_BINARY_DIR}/athena-scheme-compile-home")
  if (LLAMA_CPP_LIBRARY)
    get_filename_component (ATHENA_LLAMA_RUNTIME_DIR
                            "${LLAMA_CPP_LIBRARY}" DIRECTORY)
  elseif (LLAMA_LIBRARY_DIRS)
    list (GET LLAMA_LIBRARY_DIRS 0 ATHENA_LLAMA_RUNTIME_DIR)
  else ()
    set (ATHENA_LLAMA_RUNTIME_DIR "")
  endif ()
  get_filename_component (ATHENA_RESVG_RUNTIME_DIR
                          "${RESVG_LIBRARY}" DIRECTORY)
  add_custom_command (
    OUTPUT "${ATHENA_SCHEME_BYTECODE_STAMP}"
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${ATHENA_SCHEME_BYTECODE_DIR}"
            "${ATHENA_SCHEME_COMPILE_HOME}"
    COMMAND /bin/bash
            "${ATHENA_SOURCE_DIR}/tools/compile-athena-scheme-bytecode.sh"
            $<TARGET_FILE:${ATHENA_binary_name}>
            "${ATHENA_SCHEME_BYTECODE_DIR}"
            "${ATHENA_SOURCE_DIR}/ATHENA"
            "${ATHENA_SCHEME_COMPILE_HOME}"
            "${ATHENA_GUILE_PREFIX}"
            "${ATHENA_SOURCE_DIR}/ATHENA/progs"
            "${ATHENA_LLAMA_RUNTIME_DIR}"
            "${ATHENA_BINARY_DIR}/x64/lib"
            "${ATHENA_RESVG_RUNTIME_DIR}"
            "${ATHENA_SCHEME_COMPILE_JOBS}"
            "${ATHENA_GUILE_RUNTIME_ID}"
    DEPENDS ${ATHENA_SCHEME_SOURCES}
            "${ATHENA_GUILE_LIBRARY}"
            "${ATHENA_SCHEME_SOURCE_MANIFEST}"
            "${ATHENA_SOURCE_DIR}/src/ATHENA/ATHENA/athena.cpp"
            "${ATHENA_SOURCE_DIR}/3rdparty/athena-guile/libguile/athena-runtime.c"
            "${ATHENA_SOURCE_DIR}/tools/compile-athena-scheme-bytecode.sh"
            "${ATHENA_SOURCE_DIR}/tools/plan-athena-scheme-bytecode.scm"
    COMMENT "Precompiling ATHENA Scheme modules with the initialized ATHENA runtime"
    VERBATIM)
  add_custom_target (athena_scheme_bytecode ALL
                     DEPENDS "${ATHENA_SCHEME_BYTECODE_STAMP}")
  add_dependencies (athena_scheme_bytecode ${ATHENA_binary_name})

  set (ATHENA_LOCAL_RUNTIME_DIR "${ATHENA_SOURCE_DIR}/ATHENA")
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set (ATHENA_LOCAL_WATCHDOG_COPY
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:ATHENA-Watchdog>
              "${ATHENA_LOCAL_RUNTIME_DIR}/bin/ATHENA-Watchdog")
  else ()
    set (ATHENA_LOCAL_WATCHDOG_COPY)
  endif ()
  add_custom_target (athena_local_runtime ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory
            "${ATHENA_LOCAL_RUNTIME_DIR}/bin"
            "${ATHENA_LOCAL_RUNTIME_DIR}/lib/athena-scheme"
    COMMAND ${CMAKE_COMMAND} -E rm -rf
            "${ATHENA_LOCAL_RUNTIME_DIR}/lib/athena-scheme/${ATHENA_GUILE_RUNTIME_ID}"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${ATHENA_SCHEME_BYTECODE_DIR}"
            "${ATHENA_LOCAL_RUNTIME_DIR}/lib/athena-scheme/${ATHENA_GUILE_RUNTIME_ID}"
    COMMAND "${Python3_EXECUTABLE}"
            "${ATHENA_SOURCE_DIR}/tools/release/copy-private-guile-runtime.py"
            "${ATHENA_GUILE_PREFIX}"
            "${ATHENA_LOCAL_RUNTIME_DIR}"
            "${ATHENA_GUILE_RUNTIME_ID}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:${ATHENA_binary_name}>
            "${ATHENA_LOCAL_RUNTIME_DIR}/bin/ATHENA.bin"
    COMMAND ${CMAKE_COMMAND}
            "-DATHENA_EXECUTABLE=$<TARGET_FILE:${ATHENA_binary_name}>"
            "-DATHENA_RUNTIME_LIB_DIR=${ATHENA_LOCAL_RUNTIME_DIR}/lib"
            "-DATHENA_SEARCH_DIR_0=${ATHENA_LOCAL_RUNTIME_DIR}/lib"
            "-DATHENA_SEARCH_DIR_1=${ATHENA_GUILE_PREFIX}/lib"
            "-DATHENA_SEARCH_DIR_2=${ATHENA_LLAMA_RUNTIME_DIR}"
            "-DATHENA_SEARCH_DIR_3=${ATHENA_BINARY_DIR}/x64/lib"
            -P "${ATHENA_SOURCE_DIR}/tools/release/copy-vtk-runtime.cmake"
    ${ATHENA_LOCAL_WATCHDOG_COPY}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:athena-audmap>
            "${ATHENA_LOCAL_RUNTIME_DIR}/bin/athena-audmap"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:athena-audmap-repl>
            "${ATHENA_LOCAL_RUNTIME_DIR}/bin/athena-audmap-repl"
    COMMENT "Deploying the complete ATHENA runtime tree"
    VERBATIM)
  add_dependencies (athena_local_runtime
                    athena_scheme_bytecode athena_guile_runtime athena-audmap athena-audmap-repl)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    add_dependencies (athena_local_runtime ATHENA-Watchdog)
  endif ()
endif ()

if (APPLE)
  set(MACOSX_BUNDLE_DISPLAY_NAME ATHENA)
  set_target_properties (${ATHENA_binary_name}
    PROPERTIES
    MACOSX_BUNDLE TRUE
    DEBUG_OUTPUT_NAME ${ATHENA_binary_name}${EXE_DEBUG_EXTENSION}
    RELEASE_OUTPUT_NAME ${ATHENA_binary_name}
    RUNTIME_OUTPUT_DIRECTORY ${ATHENA_BINARY_DIR}
  )
elseif (WIN32)
  install (TARGETS ${ATHENA_binary_name} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
else (APPLE)
  install (TARGETS ${ATHENA_binary_name} DESTINATION ${tmbin}/bin)
  if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    install (TARGETS ATHENA-Watchdog DESTINATION ${tmbin}/bin)
  endif ()
endif (APPLE)
