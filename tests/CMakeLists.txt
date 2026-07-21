file (GLOB_RECURSE TEST_SRC_FILES "*.cpp")

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
    mimalloc
    PkgConfig::SPDLOG
    ${ATHENA_LLAMA_LIBRARIES}
    ${ADS_LIBRARIES}
    ${ATHENA_Libraries}
    ${ATHENA_QT_TEST_LIBRARY}
  )
  add_test (${_test_name} ${_test_name})
  if (_test_name STREQUAL "artifacts_test")
    add_dependencies (${_test_name} ${ATHENA_binary_name})
  endif ()
  set_tests_properties (${_test_name} PROPERTIES TIMEOUT 5)
  if (_test_name STREQUAL "delegation_network_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 600)
  endif ()
  if (_test_name STREQUAL "hodge_recolor_test")
    set_tests_properties (${_test_name} PROPERTIES TIMEOUT 30)
  endif ()
  set_tests_properties (${_test_name} PROPERTIES ENVIRONMENT "ATHENA_PATH=${ATHENA_SOURCE_DIR}/ATHENA")
endforeach ()
