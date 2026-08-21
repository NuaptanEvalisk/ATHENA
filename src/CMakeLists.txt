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

add_library(athena_body STATIC ${ATHENA_All_SRCS})
add_dependencies(athena_body athena_build_info)
target_compile_definitions(athena_body PRIVATE
  $<$<CONFIG:Debug>:ATHENA_DEBUG_BUILD=1>
)
target_link_libraries (athena_body
  PUBLIC
  PkgConfig::SPDLOG
  ${ATHENA_LLAMA_LIBRARIES}
  PRIVATE
  ${ADS_LIBRARIES}
  rapidfuzz::rapidfuzz
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
target_compile_definitions(${ATHENA_binary_name} PRIVATE
  MI_OVERRIDE=1
  $<$<CONFIG:Debug>:ATHENA_DEBUG_BUILD=1>
)
target_link_libraries (${ATHENA_binary_name}
  athena_body
  mimalloc
  PkgConfig::SPDLOG
  ${ATHENA_LLAMA_LIBRARIES}
  ${ADS_LIBRARIES}
  ${ATHENA_Libraries}
)

if (NOT APPLE AND NOT WIN32)
  add_custom_command (TARGET ${ATHENA_binary_name} POST_BUILD
    COMMAND ${PATCHELF_EXECUTABLE}
            --set-rpath "$ORIGIN/../lib"
            $<TARGET_FILE:${ATHENA_binary_name}>
    COMMENT "Setting relocatable runtime library path for ATHENA"
    VERBATIM)
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
