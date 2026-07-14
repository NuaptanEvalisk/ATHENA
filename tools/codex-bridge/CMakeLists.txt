add_executable(athena-codex-bridge athena_codex_bridge.cpp)

if (TARGET Qt6::Core)
  target_link_libraries(athena-codex-bridge PRIVATE Qt6::Core)
elseif (TARGET Qt5::Core)
  target_link_libraries(athena-codex-bridge PRIVATE Qt5::Core)
endif ()

target_compile_definitions(athena-codex-bridge PRIVATE
  ATHENA_CODEX_CLIENT_VERSION="${DEVEL_VERSION}")
set_target_properties(athena-codex-bridge PROPERTIES
  RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src")

if (WIN32)
  set(ATHENA_CODEX_RUNTIME_DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
else ()
  set(ATHENA_CODEX_RUNTIME_DESTINATION "${tmbin}/bin")
endif ()

install(TARGETS athena-codex-bridge
  RUNTIME DESTINATION "${ATHENA_CODEX_RUNTIME_DESTINATION}")

if (ATHENA_BUNDLE_CODEX)
  get_filename_component(_codex_name "${ATHENA_CODEX_BINARY}" NAME)
  add_custom_command(TARGET athena-codex-bridge POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ATHENA_CODEX_BINARY}"
            "$<TARGET_FILE_DIR:athena-codex-bridge>/${_codex_name}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${ATHENA_CODEX_LICENSE_FILE}"
            "$<TARGET_FILE_DIR:athena-codex-bridge>/codex-LICENSE"
    VERBATIM)
  install(PROGRAMS "${ATHENA_CODEX_BINARY}"
    DESTINATION "${ATHENA_CODEX_RUNTIME_DESTINATION}")
  install(FILES "${ATHENA_CODEX_LICENSE_FILE}"
    DESTINATION "${ATHENA_CODEX_RUNTIME_DESTINATION}/../licenses/codex"
    RENAME LICENSE)
endif ()
