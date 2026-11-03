file (GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "*.cpp")
add_test(NAME shipped_macro_labels_generator_test
  COMMAND "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/Data/NodeContracts/shipped-macro-labels-test.py")
set_tests_properties(shipped_macro_labels_generator_test PROPERTIES TIMEOUT 15)

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

add_executable(hodarium_decision_feed_test hodarium/decision_feed_test.cpp)
set_target_properties(hodarium_decision_feed_test PROPERTIES AUTOMOC OFF)
target_include_directories(hodarium_decision_feed_test PRIVATE
  ${NLOHMANN_JSON_INCLUDE_DIR} ${LIBSODIUM_INCLUDE_DIRS})
target_link_libraries(hodarium_decision_feed_test PRIVATE athena_hodarium ${LIBSODIUM_LIBRARIES})
add_test(NAME hodarium_decision_feed_test COMMAND hodarium_decision_feed_test)
set_tests_properties(hodarium_decision_feed_test PROPERTIES TIMEOUT 15)

add_executable(hodarium_path_collisions_test hodarium/path_collisions_test.cpp)
set_target_properties(hodarium_path_collisions_test PROPERTIES AUTOMOC OFF)
target_link_libraries(hodarium_path_collisions_test PRIVATE athena_hodarium)
add_test(NAME hodarium_path_collisions_test COMMAND hodarium_path_collisions_test)
set_tests_properties(hodarium_path_collisions_test PROPERTIES TIMEOUT 15)

# Run through the isolated Go TestNativeRecovery harness, which supplies a
# temporary HTTPS authority and fixture via ATHENA_HODARIUM_NATIVE_RECOVERY.
add_executable(hodarium_recovery_test EXCLUDE_FROM_ALL hodarium/recovery_test.cpp)
set_target_properties(hodarium_recovery_test PROPERTIES AUTOMOC OFF)
target_include_directories(hodarium_recovery_test PRIVATE ${NLOHMANN_JSON_INCLUDE_DIR})
target_link_libraries(hodarium_recovery_test PRIVATE athena_hodarium)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR APPLE)
  add_executable(hodarium_resources_test EXCLUDE_FROM_ALL
    hodarium/resources_test.cpp
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Data/hodarium_resources.cpp
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Data/background_workers.cpp
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Data/document_history_store.cpp
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Data/fossil_delta.cpp
    ${ATHENA_SOURCE_DIR}/src/System/Files/confined_filesystem.cpp)
  set_target_properties(hodarium_resources_test PROPERTIES AUTOMOC OFF)
  target_include_directories(hodarium_resources_test PRIVATE
    ${ATHENA_SOURCE_DIR}/src ${ATHENA_SOURCE_DIR}/src/ATHENA
    ${ATHENA_SOURCE_DIR}/src/ATHENA/Data ${ATHENA_SOURCE_DIR}/src/System/Files
    ${NLOHMANN_JSON_INCLUDE_DIR} ${SQLITE3_INCLUDE_DIRS})
  target_link_libraries(hodarium_resources_test PRIVATE athena_hodarium
    ${SQLITE3_LIBRARIES} Threads::Threads)
  add_test(NAME hodarium_resources_test COMMAND hodarium_resources_test)
  set_tests_properties(hodarium_resources_test PROPERTIES TIMEOUT 30)
endif()

function (athena_link_full_test_runtime target)
  # Keep special test executables on the same production-runtime link closure
  # as the automatically registered C++ tests below.  Hand-maintained subsets
  # go stale whenever athena_test_body acquires a new library dependency.
  if (NOT ATHENA_ENABLE_TSAN)
    athena_link_allocator (${target})
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

add_executable (link_icon_cache_test qt/link_icon_cache_test.cc)
set_target_properties (link_icon_cache_test PROPERTIES AUTOMOC OFF)
target_include_directories (link_icon_cache_test PRIVATE
  ${ATHENA_SOURCE_DIR}/src/Subsystems/Qt)
target_link_libraries (link_icon_cache_test PRIVATE Qt6::Widgets Threads::Threads)
add_test (NAME link_icon_cache_test COMMAND link_icon_cache_test)
set_tests_properties (link_icon_cache_test PROPERTIES TIMEOUT 15
  ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

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
      ${CMAKE_CURRENT_SOURCE_DIR}/runtime/stacktrace-symbolize-test.py
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
      ${CMAKE_CURRENT_SOURCE_DIR}/runtime/watchdog-test.py
      --watchdog $<TARGET_FILE:ATHENA-Watchdog>
      --child $<TARGET_FILE:watchdog_fake_child>)
  set_tests_properties (watchdog_supervisor_test PROPERTIES TIMEOUT 15)

  add_test (NAME crash_report_test
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/runtime/crash-report-test.py
      --compiler ${CMAKE_CXX_COMPILER}
      --source-dir ${ATHENA_SOURCE_DIR}
      --build-dir ${ATHENA_BINARY_DIR}
      --binary $<TARGET_FILE:ATHENA.bin>
      --resources ${ATHENA_SOURCE_DIR}/ATHENA)
  set_tests_properties (crash_report_test PROPERTIES TIMEOUT 90)
endif ()

# Command contracts are XML-generated and exercised without an interpreter.
add_test(NAME native_command_generator_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/ATHENA/Commands/command-generator-test.py
    --compiler ${CMAKE_CXX_COMPILER})
set_tests_properties(native_command_generator_test PROPERTIES TIMEOUT 30)

foreach(_case website-export website-math-export native-v3-website-export
              native-v3-hodarium-shared-audit native-v3-math-alphabet-stage)
  string(REPLACE "-" "_" _test_name "${_case}_test")
  add_test(NAME ${_test_name}
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/runtime/${_case}-test.py
      --binary $<TARGET_FILE:ATHENA.bin>
      --resources ${ATHENA_SOURCE_DIR}/ATHENA)
  set_tests_properties(${_test_name} PROPERTIES
    TIMEOUT 360 RESOURCE_LOCK athena_application_runtime)
endforeach()

add_test(NAME vault_node_model_upgrade_runtime_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/Data/Convert/Xml/vault_node_model_upgrade_test.py
    --binary $<TARGET_FILE:ATHENA.bin>
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
add_test(NAME audmap_v3_runtime_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/interop/audmap-v3-runtime-test.py
    --binary $<TARGET_FILE:ATHENA.bin>
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(vault_node_model_upgrade_runtime_test audmap_v3_runtime_test
  PROPERTIES TIMEOUT 240 RESOURCE_LOCK athena_application_runtime)

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
  if (_test_name STREQUAL "native_conversion_test")
    set_tests_properties (${_test_name} PROPERTIES
      TIMEOUT 120 RESOURCE_LOCK athena_application_runtime)
  endif ()
  if (_test_name STREQUAL "native_v3_export_test")
    # This also compiles generated LaTeX twice when pdflatex is available.
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 90)
  endif ()
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
  if (_test_name STREQUAL "native_interactive_dialog_test" OR
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
