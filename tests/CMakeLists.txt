file (GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "*.cpp")

list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/hodarium/.*_test\\.cpp$")
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(hodarium_authority_client_test EXCLUDE_FROM_ALL hodarium/authority_client_test.cpp)
  set_target_properties(hodarium_authority_client_test PROPERTIES AUTOMOC OFF)
  target_include_directories(hodarium_authority_client_test PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
  target_link_libraries(hodarium_authority_client_test PRIVATE athena_hodarium)
  # Explicit-only: the launcher provisions an isolated D-Bus/keyring session.
  add_executable(hodarium_identity_test EXCLUDE_FROM_ALL hodarium/identity_test.cpp)
  set_target_properties(hodarium_identity_test PROPERTIES AUTOMOC OFF)
  target_include_directories(hodarium_identity_test PRIVATE ${LIBSODIUM_INCLUDE_DIRS})
  target_link_libraries(hodarium_identity_test PRIVATE athena_hodarium
    PkgConfig::HODARIUM_SECRET ${LIBSODIUM_LIBRARIES})
endif()
add_executable(hodarium_membership_test hodarium/membership_test.cpp)
set_target_properties(hodarium_membership_test PROPERTIES AUTOMOC OFF)
target_include_directories(hodarium_membership_test PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
target_link_libraries(hodarium_membership_test PRIVATE athena_hodarium)
add_test(NAME hodarium_membership_test COMMAND hodarium_membership_test
  ${CMAKE_CURRENT_SOURCE_DIR}/hodarium/membership-wire.json)
set_tests_properties(hodarium_membership_test PROPERTIES TIMEOUT 15)
add_executable(hodarium_revisions_test hodarium/revisions_test.cpp)
set_target_properties(hodarium_revisions_test PROPERTIES AUTOMOC OFF)
target_link_libraries(hodarium_revisions_test PRIVATE athena_hodarium)
add_test(NAME hodarium_revisions_test COMMAND hodarium_revisions_test)
set_tests_properties(hodarium_revisions_test PROPERTIES TIMEOUT 15)

function (athena_link_full_test_runtime target)
  # Keep special test executables on the same production-runtime link closure
  # as the automatically registered C++ tests below.  Hand-maintained subsets
  # go stale whenever athena_test_body acquires a new library dependency.
  if (NOT ATHENA_ENABLE_TSAN)
    target_link_libraries (${target} PRIVATE mimalloc)
  endif ()
  target_link_libraries (${target} PRIVATE
    athena_test_body
    PkgConfig::SPDLOG
    ${ATHENA_LLAMA_LIBRARIES}
    ${ADS_LIBRARIES}
    ${ATHENA_Libraries}
    ${ATHENA_QT_TEST_LIBRARY})
  target_include_directories (${target} PRIVATE
    "${ATHENA_SOURCE_DIR}/src/Subsystems/Plugins")
  if (ATHENA_ENABLE_TSAN)
    target_sources (${target} PRIVATE
      "${ATHENA_SOURCE_DIR}/src/System/Misc/tsan_mimalloc_shim.cpp")
  endif ()
endfunction ()
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/Data/String/unicode_text_test\\.cpp$")
add_executable (unicode_text_test Data/String/unicode_text_test.cpp)
set_target_properties (unicode_text_test PROPERTIES AUTOMOC OFF)
target_link_libraries (unicode_text_test PRIVATE athena_unicode Threads::Threads)
add_test (NAME unicode_text_test COMMAND unicode_text_test)
set_tests_properties (unicode_text_test PROPERTIES TIMEOUT 15)

add_test(NAME package_utf8_resources_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/package-utf8-resources-test.py
    ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(package_utf8_resources_test PROPERTIES TIMEOUT 15)

list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/watchdog/watchdog_fake_child\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/qt/completion_popup_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/interop_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/authorization_ui_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/confined_filesystem_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/repl-pane-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-subscription-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-package-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-runtime-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-runtime-fixture\\.cpp$")

add_executable(plugin_runtime_fixture interop/plugin-runtime-fixture.cpp)
set_target_properties(plugin_runtime_fixture PROPERTIES AUTOMOC OFF)
target_link_libraries(plugin_runtime_fixture PRIVATE athena_audmap_sdk)

add_executable(plugin_runtime_test interop/plugin-runtime-test.cpp)
set_target_properties(plugin_runtime_test PROPERTIES AUTOMOC OFF)
athena_link_full_test_runtime(plugin_runtime_test)
target_link_libraries(plugin_runtime_test PRIVATE Qt6::Test)
if (TARGET athena-plugin-sandbox)
  add_dependencies(plugin_runtime_test athena-plugin-sandbox)
  add_custom_command(TARGET plugin_runtime_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:athena-plugin-sandbox>
      $<TARGET_FILE_DIR:plugin_runtime_test>/athena-plugin-sandbox)
endif ()
if (TARGET athena-plugin-sandbox)
  add_dependencies(plugin_runtime_test athena-plugin-sandbox)
  add_custom_command(TARGET plugin_runtime_test POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      $<TARGET_FILE:athena-plugin-sandbox>
      $<TARGET_FILE_DIR:plugin_runtime_test>/athena-plugin-sandbox)
endif ()
target_compile_definitions(plugin_runtime_test PRIVATE
  PLUGIN_FIXTURE_PATH="$<TARGET_FILE:plugin_runtime_fixture>")
add_dependencies(plugin_runtime_test plugin_runtime_fixture)
add_test(NAME plugin_runtime_test COMMAND plugin_runtime_test)
set_tests_properties(plugin_runtime_test PROPERTIES TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

find_package(LibArchive REQUIRED)
add_executable(plugin_package_test interop/plugin-package-test.cpp)
set_target_properties(plugin_package_test PROPERTIES AUTOMOC OFF)
target_link_libraries(plugin_package_test PRIVATE athena_plugins LibArchive::LibArchive)
add_test(NAME plugin_package_test COMMAND plugin_package_test)
set_tests_properties(plugin_package_test PROPERTIES TIMEOUT 15)

add_executable(plugin_subscription_test interop/plugin-subscription-test.cpp)
set_target_properties(plugin_subscription_test PROPERTIES AUTOMOC OFF)
target_link_libraries(plugin_subscription_test PRIVATE athena_plugins athena_audmap athena_audmap_sdk)
add_test(NAME plugin_subscription_test COMMAND plugin_subscription_test)
set_tests_properties(plugin_subscription_test PROPERTIES TIMEOUT 25)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_executable(confined_filesystem_test interop/confined_filesystem_test.cpp
    ${ATHENA_SOURCE_DIR}/src/System/Files/confined_filesystem.cpp)
  set_target_properties(confined_filesystem_test PROPERTIES AUTOMOC OFF)
  target_link_libraries(confined_filesystem_test PRIVATE Threads::Threads)
  add_test(NAME confined_filesystem_test COMMAND confined_filesystem_test)
  set_tests_properties(confined_filesystem_test PROPERTIES TIMEOUT 15)
endif ()

add_executable(interop_test interop/interop_test.cpp)
set_target_properties(interop_test PROPERTIES AUTOMOC OFF)
target_link_libraries(interop_test PRIVATE athena_audmap_client athena_audmap athena_audmap_sdk)
add_dependencies(interop_test athena-audmap-repl)
target_compile_definitions(interop_test PRIVATE AUDMAP_REPL_PATH="$<TARGET_FILE:athena-audmap-repl>")
add_test(NAME interop_test COMMAND interop_test)
set_tests_properties(interop_test PROPERTIES TIMEOUT 35)
add_test(NAME audmap_repl_terminal_test
  COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/interop/repl-terminal-test.py
    $<TARGET_FILE:interop_test> $<TARGET_FILE:athena-audmap-repl>)
set_tests_properties(audmap_repl_terminal_test PROPERTIES TIMEOUT 25)

add_executable(audmap_repl_pane_test interop/repl-pane-test.cpp)
set_target_properties(audmap_repl_pane_test PROPERTIES AUTOMOC OFF)
target_include_directories(audmap_repl_pane_test PRIVATE ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt)
target_link_libraries(audmap_repl_pane_test PRIVATE athena_audmap_terminal athena_audmap qtermwidget6 Qt6::Test)
target_compile_definitions(audmap_repl_pane_test PRIVATE AUDMAP_REPL_PATH="$<TARGET_FILE:athena-audmap-repl>")
add_dependencies(audmap_repl_pane_test athena-audmap-repl)
add_test(NAME audmap_repl_pane_test COMMAND audmap_repl_pane_test)
set_tests_properties(audmap_repl_pane_test PROPERTIES TIMEOUT 25 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(audmap_authorization_ui_test interop/authorization_ui_test.cpp)
set_target_properties(audmap_authorization_ui_test PROPERTIES AUTOMOC OFF)
target_include_directories(audmap_authorization_ui_test PRIVATE ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt)
athena_link_full_test_runtime(audmap_authorization_ui_test)
target_link_libraries(audmap_authorization_ui_test PRIVATE athena_audmap_client Qt6::Widgets Qt6::Test)
add_test(NAME audmap_authorization_ui_test COMMAND audmap_authorization_ui_test)
set_tests_properties(audmap_authorization_ui_test PROPERTIES TIMEOUT 20 ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable (completion_popup_test qt/completion_popup_test.cpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMCompletionPopup.cpp)
target_include_directories (completion_popup_test PRIVATE
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt)
target_link_libraries (completion_popup_test PRIVATE KF6::Completion Qt6::Test)
add_test (NAME completion_popup_test COMMAND completion_popup_test)
set_tests_properties (completion_popup_test PROPERTIES TIMEOUT 20
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  add_test (NAME stacktrace_symbolize_test
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/stacktrace-symbolize-test.py
      --compiler ${CMAKE_CXX_COMPILER}
      --source-dir ${ATHENA_SOURCE_DIR})
  set_tests_properties (stacktrace_symbolize_test PROPERTIES TIMEOUT 150)
  add_executable (watchdog_fake_child watchdog/watchdog_fake_child.cpp)
  target_sources (watchdog_fake_child PRIVATE
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Watchdog/watchdog_client.cpp)
  target_link_libraries (watchdog_fake_child PRIVATE Qt6::Core)
  target_include_directories (watchdog_fake_child PRIVATE ${ATHENA_SOURCE_DIR}/src)
  add_test (NAME watchdog_supervisor_test
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/watchdog-test.py
      --watchdog $<TARGET_FILE:ATHENA-Watchdog>
      --child $<TARGET_FILE:watchdog_fake_child>)
  set_tests_properties (watchdog_supervisor_test PROPERTIES TIMEOUT 15)

  add_test (NAME crash_report_test
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/crash-report-test.py
      --compiler ${CMAKE_CXX_COMPILER}
      --source-dir ${ATHENA_SOURCE_DIR}
      --binary $<TARGET_FILE:ATHENA.bin>
      --runtime ${ATHENA_GUILE_PREFIX}
      --resources ${ATHENA_SOURCE_DIR}/ATHENA)
  set_tests_properties (crash_report_test PROPERTIES TIMEOUT 90)
endif ()

add_test(NAME glue_generator_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/glue-generator-test.py)
if(TARGET ATHENA.bin)
  set(ATHENA_GLUE_TEST_BINARY ATHENA.bin)
else()
  set(ATHENA_GLUE_TEST_BINARY ATHENA)
endif()
get_target_property(ATHENA_GLUE_TEST_INPUTS athena_glue ATHENA_GLUE_INTERFACES)
add_test(NAME glue_runtime_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/glue-runtime-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA
    ${ATHENA_GLUE_TEST_INPUTS})
set_tests_properties(glue_runtime_test PROPERTIES TIMEOUT 100)
add_test(NAME vault_random_document_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/vault-random-document-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(vault_random_document_test PROPERTIES TIMEOUT 45)
add_test(NAME evaluation_bar_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(evaluation_bar_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME html5_export_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/html5-export-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(html5_export_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME commutative_diagram_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/commutative-diagram-render-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(commutative_diagram_editor_test PROPERTIES
  TIMEOUT 120 SKIP_RETURN_CODE 77)
add_test(NAME eqnarray_clipboard_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/eqnarray-clipboard-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(eqnarray_clipboard_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME proof_eqnarray_navigation_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/proof-eqnarray-navigation-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(proof_eqnarray_navigation_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME font_init_alias_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/font-init-alias-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(font_init_alias_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME window_ownership_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-ownership-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-ownership-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(window_ownership_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME document_tabs_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/document-tabs-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/document-tabs-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(document_tabs_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME link_invalidation_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/link-invalidation-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(link_invalidation_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME completion_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/completion-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(completion_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME geometry_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/geometry-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(geometry_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME format_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/format-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(format_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME structured_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/structured-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(structured_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME generic_keyboard_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/generic-keyboard-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(generic_keyboard_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME generic_keybindings_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/generic-keybindings-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(generic_keybindings_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME native_math_keyboard_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/native-math-keyboard-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(native_math_keyboard_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME generic_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/generic-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(generic_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME generic_extra_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/generic-extra-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(generic_extra_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME document_edit_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/document-edit-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(document_edit_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME document_style_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/document-style-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(document_style_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME embedded_image_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/embedded-image-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(embedded_image_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME document_part_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/document-part-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(document_part_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME live_spell_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/live-spell-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(live_spell_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME global_typeset_refresh_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/editor-owner-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/global-typeset-refresh-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(global_typeset_refresh_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME node_move_lifecycle_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-move-lifecycle-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_new_v2_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-new-v2-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_v2_persistence_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-v2-persistence-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(
  node_move_lifecycle_test node_new_v2_test node_v2_persistence_test
  PROPERTIES TIMEOUT 150)
add_test(NAME vault_node_model_upgrade_runtime_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/Data/Convert/Xml/vault_node_model_upgrade_test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(vault_node_model_upgrade_runtime_test PROPERTIES TIMEOUT 240)
add_test(NAME audmap_v3_runtime_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/interop/audmap-v3-runtime-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(audmap_v3_runtime_test PROPERTIES TIMEOUT 240)
add_test(NAME node_data_art_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-data-art-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_model_bridge_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-model-bridge-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_source_edit_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-source-edit-test.scm
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_tree_diff_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-tree-diff-test.scm
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anonymous-editor-owner-setup.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME node_reference_headless_export_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/node-reference-headless-export-test.scm
    --root-env ATHENA_NODE_EXPORT_TEST_ROOT
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA
    --timeout 120)
add_test(NAME group_bytecode_scheme_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/runtime-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/group-bytecode-test.scm
    --root-env ATHENA_GROUP_TEST_DIR
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(
  node_data_art_scheme_test node_model_bridge_scheme_test
  node_source_edit_scheme_test node_tree_diff_scheme_test
  node_reference_headless_export_scheme_test group_bytecode_scheme_test
  PROPERTIES TIMEOUT 150)
add_test(NAME two_buffer_save_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/two-buffer-save-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(two_buffer_save_test PROPERTIES TIMEOUT 120)
add_test(NAME save_as_ownership_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/save-as-ownership-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(save_as_ownership_test PROPERTIES TIMEOUT 60)
add_test(NAME packrat_concurrency_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/two-buffer-save-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/packrat-concurrency-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(packrat_concurrency_test PROPERTIES TIMEOUT 60)

# These tests launch a complete ATHENA application/runtime, often with private
# Guile, Qt, font and actor subsystems.  Running many of them at once turns the
# ordinary suite into an accidental multi-instance stress test and has produced
# startup timeouts and BufferActor crashes that do not reproduce in isolation.
# Serialize application integrations with each other while leaving C++ unit
# tests and host-Guile source tests fully parallel.
set(_athena_application_runtime_tests
  glue_runtime_test
  vault_random_document_test
  evaluation_bar_editor_test
  html5_export_editor_test
  commutative_diagram_editor_test
  eqnarray_clipboard_editor_test
  proof_eqnarray_navigation_editor_test
  font_init_alias_editor_test
  window_ownership_editor_test
  document_tabs_editor_test
  link_invalidation_editor_test
  completion_editor_test
  geometry_editor_test
  format_editor_test
  structured_editor_test
  generic_keyboard_editor_test
  generic_keybindings_editor_test
  native_math_keyboard_editor_test
  generic_editor_test
  generic_extra_editor_test
  document_edit_editor_test
  document_style_editor_test
  embedded_image_editor_test
  document_part_editor_test
  live_spell_editor_test
  global_typeset_refresh_editor_test
  node_move_lifecycle_test
  node_new_v2_test
  node_v2_persistence_test
  vault_node_model_upgrade_runtime_test
  audmap_v3_runtime_test
  node_data_art_scheme_test
  node_model_bridge_scheme_test
  node_source_edit_scheme_test
  node_tree_diff_scheme_test
  node_reference_headless_export_scheme_test
  group_bytecode_scheme_test
  two_buffer_save_test
  save_as_ownership_test
  packrat_concurrency_test)
set_tests_properties(${_athena_application_runtime_tests}
  PROPERTIES RESOURCE_LOCK athena_application_runtime)

find_program (ATHENA_TEST_GUILE guile)
if (ATHENA_TEST_GUILE)
  add_test (NAME math_keyboard_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/math-keyboard-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME unsaved_buffers_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/unsaved-buffers-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME autosave_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/autosave-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME reload_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/reload-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME new_file_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/new-file-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME window_close_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-close-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME link_navigation_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/link-navigation-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME converter_search_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/converter-search-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME aux_buffer_permissions_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/aux-buffer-permissions-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME vault_inserter_result_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/vault-inserter-result-test.scm
      ${ATHENA_SOURCE_DIR})
endif ()

# from list of files we'll create tests test_name.cpp -> test_name
foreach (_test_file ${TEST_SRC_FILES})
  get_filename_component (_test_name ${_test_file} NAME_WE)
  add_executable (${_test_name}
    ${_test_file}
  )
  target_include_directories (${_test_name} PRIVATE
    ${ATHENA_SOURCE_DIR}/src
  )
  # Match ATHENA.bin: the allocator must outlive SYCL's unload-time allocations.
  athena_link_full_test_runtime (${_test_name})
  if (_test_name STREQUAL "program_model_test")
    target_link_libraries (${_test_name} PRIVATE KF6::SyntaxHighlighting)
  endif ()
  add_test (${_test_name} ${_test_name})
  if (_test_name STREQUAL "artifacts_test" OR
      _test_name STREQUAL "command_line_conversion_test" OR
      _test_name STREQUAL "bold_big_operator_test" OR
      _test_name STREQUAL "latex_import_test" OR
      _test_name STREQUAL "materials_latex_roundtrip_test" OR
      _test_name STREQUAL "toc_pagination_test")
    if (TARGET ATHENA.bin)
      add_dependencies (${_test_name} ATHENA.bin)
    elseif (TARGET ATHENA)
      add_dependencies (${_test_name} ATHENA)
    endif ()
  endif ()
  # Five seconds is too small for tests that legitimately initialize fonts,
  # render through Qt, or launch ATHENA.bin on a loaded CI host.  Keep a sane
  # default and override genuinely long integrations below.
  set_tests_properties (${_test_name} PROPERTIES TIMEOUT 15)
  if (_test_name STREQUAL "command_line_conversion_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 180)
  endif ()
  if (_test_name STREQUAL "delegation_network_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 600)
  endif ()
  if (_test_name STREQUAL "hodge_recolor_test" OR
      _test_name STREQUAL "native_interactive_dialog_test" OR
      _test_name STREQUAL "completing_combo_box_test" OR
      _test_name STREQUAL "evaluation_bar_test" OR
      _test_name STREQUAL "evaluation_bar_conversion_test" OR
      _test_name STREQUAL "commutative_diagram_native_editor_test" OR
      _test_name STREQUAL "structured_radioactive_test" OR
      _test_name STREQUAL "utf8_editor_test" OR
      _test_name STREQUAL "vault_maintenance_dialog_test" OR
      _test_name STREQUAL "preview_math_font_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "file_chooser_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "file_chooser_test" OR
      _test_name STREQUAL "native_interactive_dialog_test" OR
      _test_name STREQUAL "completing_combo_box_test")
    set_property (TEST ${_test_name} APPEND PROPERTY ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen")
  endif ()
  if (_test_name STREQUAL "shaped_pdf_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "artifacts_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 60)
  endif ()
  if (_test_name STREQUAL "vault_map_sqlite_test" OR
      _test_name STREQUAL "namespace_database_test" OR
      _test_name STREQUAL "namespace_ontology_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "bold_big_operator_test" OR
      _test_name STREQUAL "toc_pagination_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "latex_import_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 45)
  endif ()
  if (_test_name STREQUAL "materials_latex_roundtrip_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 90)
  endif ()
  set_property (TEST ${_test_name} APPEND PROPERTY ENVIRONMENT
    "ATHENA_PATH=${ATHENA_SOURCE_DIR}/ATHENA")
endforeach ()

# CTest must be able to launch the same runtime dependencies as ATHENA.bin
# without requiring developers to remember an out-of-band LD_LIBRARY_PATH.
# This is especially important for oneDNN and other libraries deployed into
# ATHENA/lib rather than the system loader's default search path.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
  get_property (_athena_registered_tests DIRECTORY PROPERTY TESTS)
  foreach (_test_name ${_athena_registered_tests})
    set_property (TEST ${_test_name} APPEND PROPERTY ENVIRONMENT
      "LD_LIBRARY_PATH=${ATHENA_SOURCE_DIR}/ATHENA/lib:$ENV{LD_LIBRARY_PATH}")
  endforeach ()
endif ()
