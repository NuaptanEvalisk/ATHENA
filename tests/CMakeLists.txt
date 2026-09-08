file (GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "*.cpp")

list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/watchdog/watchdog_fake_child\\.cpp$")
list (FILTER TEST_SRC_FILES EXCLUDE REGEX "/qt/completion_popup_test\\.cpp$")

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
add_test(NAME builtin_scheme_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/evaluation-bar-test.py
    --script ${CMAKE_CURRENT_SOURCE_DIR}/scheme/builtin-scheme-test.scm
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(builtin_scheme_editor_test PROPERTIES TIMEOUT 90)
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
  add_test (NAME anchor_confirmation_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/anchor-confirmation-test.scm
      ${ATHENA_SOURCE_DIR})
  add_test (NAME autosave_ownership_scheme_test
    COMMAND ${ATHENA_TEST_GUILE} --no-auto-compile
      ${CMAKE_CURRENT_SOURCE_DIR}/scheme/autosave-ownership-test.scm
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
    athena_body
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
      _test_name STREQUAL "preview_math_font_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  if (_test_name STREQUAL "artifacts_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 60)
  endif ()
  if (_test_name STREQUAL "vault_map_sqlite_test" OR
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
