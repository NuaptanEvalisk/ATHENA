file (GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "*.cpp")

list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/watchdog/watchdog_fake_child\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/qt/completion_popup_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/interop_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/authorization_ui_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/confined_filesystem_test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/repl-pane-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-subscription-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-package-test\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/interop/plugin-runtime-test\\.cpp$")

add_executable(plugin_runtime_test interop/plugin-runtime-test.cpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMAudmap.cpp)
set_target_properties(plugin_runtime_test PROPERTIES AUTOMOC OFF)
target_link_libraries(plugin_runtime_test PRIVATE athena_plugin_ui Qt6::Test)
target_compile_definitions(plugin_runtime_test PRIVATE
  PLUGIN_FIXTURE_PATH="${CMAKE_CURRENT_SOURCE_DIR}/interop/plugin-runtime-fixture.py"
  PLUGIN_PYTHON_PATH="${ATHENA_SOURCE_DIR}/clients/python/src")
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

add_executable(audmap_authorization_ui_test interop/authorization_ui_test.cpp
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt/QTMAudmap.cpp)
set_target_properties(audmap_authorization_ui_test PROPERTIES AUTOMOC OFF)
target_include_directories(audmap_authorization_ui_test PRIVATE ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt)
target_link_libraries(audmap_authorization_ui_test PRIVATE athena_audmap_client athena_audmap athena_plugin_runtime Qt6::Widgets Qt6::Test)
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
add_test(NAME evaluation_bar_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(evaluation_bar_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME html5_export_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
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
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/eqnarray-clipboard-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(eqnarray_clipboard_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME proof_eqnarray_navigation_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/proof-eqnarray-navigation-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(proof_eqnarray_navigation_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME scheme_command_order_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/scheme-command-order-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(scheme_command_order_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME font_init_alias_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/font-init-alias-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(font_init_alias_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME lazy_menu_startup_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/lazy-menu-startup-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(lazy_menu_startup_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME window_menu_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --setup-script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-menu-setup.scm
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-menu-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(window_menu_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME link_invalidation_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/link-invalidation-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(link_invalidation_editor_test PROPERTIES TIMEOUT 90)
add_test(NAME completion_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/completion-editor-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(completion_editor_test PROPERTIES TIMEOUT 90)
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
    --mode plain
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(packrat_concurrency_test PROPERTIES TIMEOUT 60)

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
  add_test (NAME window_close_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/window-close-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME buffer_switcher_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/buffer-switcher-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME link_navigation_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/link-navigation-ownership-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME converter_search_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/converter-search-test.scm
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
  if (NOT ATHENA_ENABLE_TSAN)
    target_link_libraries (${_test_name} mimalloc)
  endif ()
  target_link_libraries (${_test_name}
    athena_test_body
    PkgConfig::SPDLOG
    ${ATHENA_LLAMA_LIBRARIES}
    ${ADS_LIBRARIES}
    ${ATHENA_Libraries}
    ${ATHENA_QT_TEST_LIBRARY}
  )
  if (ATHENA_ENABLE_TSAN)
    target_sources (${_test_name} PRIVATE
      "${ATHENA_SOURCE_DIR}/src/System/Misc/tsan_mimalloc_shim.cpp")
  endif ()
  add_test (${_test_name} ${_test_name})
  if (_test_name STREQUAL "artifacts_test" OR
      _test_name STREQUAL "command_line_conversion_test" OR
      _test_name STREQUAL "bold_big_operator_test" OR
      _test_name STREQUAL "finite_part_integral_test" OR
      _test_name STREQUAL "latex_import_test" OR
      _test_name STREQUAL "materials_latex_roundtrip_test" OR
      _test_name STREQUAL "toc_pagination_test")
    if (TARGET ATHENA.bin)
      add_dependencies (${_test_name} ATHENA.bin)
    elseif (TARGET ATHENA)
      add_dependencies (${_test_name} ATHENA)
    endif ()
  endif ()
  set_tests_properties (${_test_name} PROPERTIES TIMEOUT 5)
  if (_test_name STREQUAL "command_line_conversion_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 90)
  endif ()
  if (_test_name STREQUAL "delegation_network_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 600)
  endif ()
  if (_test_name STREQUAL "hodge_recolor_test" OR
      _test_name STREQUAL "file_chooser_test" OR
      _test_name STREQUAL "evaluation_bar_test" OR
      _test_name STREQUAL "evaluation_bar_conversion_test" OR
      _test_name STREQUAL "structured_radioactive_test" OR
      _test_name STREQUAL "vault_maintenance_dialog_test" OR
      _test_name STREQUAL "preview_math_font_test")
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
      _test_name STREQUAL "finite_part_integral_test" OR
      _test_name STREQUAL "toc_pagination_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "latex_import_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 45)
  endif ()
  if (_test_name STREQUAL "materials_latex_roundtrip_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 90)
  endif ()
  set_tests_properties (${_test_name} PROPERTIES ENVIRONMENT "ATHENA_PATH=${ATHENA_SOURCE_DIR}/ATHENA")
endforeach ()
