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

if (WIN32)
  add_executable (${ATHENA_binary_name} WIN32
    ./ATHENA/ATHENA/athena.cpp
  )
else (WIN32)
  add_executable (${ATHENA_binary_name}
    ./ATHENA/ATHENA/athena.cpp
  )
endif (WIN32)
target_link_libraries (${ATHENA_binary_name}
  athena_body
  mimalloc
  ${ATHENA_Libraries}
)

if (APPLE)
  set(MACOSX_BUNDLE_DISPLAY_NAME ATHENA)
  set_target_properties (${ATHENA_binary_name}
    PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_INFO_PLIST ${ATHENA_SOURCE_DIR}/packages/macos/Info.plist.in
    DEBUG_OUTPUT_NAME ${ATHENA_binary_name}${EXE_DEBUG_EXTENSION}
    RELEASE_OUTPUT_NAME ${ATHENA_binary_name}
    RUNTIME_OUTPUT_DIRECTORY ${ATHENA_BINARY_DIR}
  )
elseif (WIN32)
  install (TARGETS ${ATHENA_binary_name} DESTINATION ${CMAKE_INSTALL_PREFIX}/bin)
else (APPLE)
  install (TARGETS ${ATHENA_binary_name} DESTINATION ${tmbin}/bin)
endif (APPLE)

