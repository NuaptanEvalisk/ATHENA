file (GLOB_RECURSE TEST_SRC_FILES CONFIGURE_DEPENDS "*.cpp")

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
add_test(NAME commutative_diagram_editor_test
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/scheme/commutative-diagram-render-test.py
    --binary $<TARGET_FILE:${ATHENA_GLUE_TEST_BINARY}>
    --runtime ${ATHENA_GUILE_PREFIX}
    --resources ${ATHENA_SOURCE_DIR}/ATHENA)
set_tests_properties(commutative_diagram_editor_test PROPERTIES
  TIMEOUT 120 SKIP_RETURN_CODE 77)

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
  else ()
    target_link_libraries (${_test_name} mimalloc)
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
