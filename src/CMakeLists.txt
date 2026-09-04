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
  "${ATHENA_SOURCE_DIR}/src/System/Misc/tsan_mimalloc_shim.cpp")
add_library(athena_body STATIC ${ATHENA_All_SRCS})
add_dependencies(athena_body athena_build_info athena_guile_runtime)
target_compile_definitions(athena_body PRIVATE
  ATHENA_GUILE_BUILD_RUNTIME_ROOT="${ATHENA_GUILE_PREFIX}"
  ATHENA_GUILE_RUNTIME_ID="${ATHENA_GUILE_RUNTIME_ID}"
  $<$<CONFIG:Debug>:ATHENA_DEBUG_BUILD=1>
)
target_link_libraries (athena_body
  PUBLIC
  PkgConfig::SPDLOG
  ${ATHENA_LLAMA_LIBRARIES}
  PRIVATE
  ${ADS_LIBRARIES}
  rapidfuzz::rapidfuzz
  athena_snowball_english
  ncnn
)
if (TARGET KF6::SyntaxHighlighting)
  target_link_libraries (athena_body PRIVATE KF6::SyntaxHighlighting)
endif ()
if (TARGET PkgConfig::KPATHSEA)
  target_compile_definitions (athena_body PRIVATE USE_KPATHSEA_API=1)
  target_link_libraries (athena_body PRIVATE PkgConfig::KPATHSEA)
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
    COMMENT "Deploying the complete ATHENA runtime tree"
    VERBATIM)
  add_dependencies (athena_local_runtime
                    athena_scheme_bytecode athena_guile_runtime)
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
endif (APPLE)
