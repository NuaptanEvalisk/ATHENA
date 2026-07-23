find_package (CURL REQUIRED)

add_library (athena-web-server-common STATIC
  web_server_common.cpp
)

target_include_directories (athena-web-server-common PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${Boost_INCLUDE_DIRS}
)

target_compile_definitions (athena-web-server-common PRIVATE
  BOOST_ERROR_CODE_HEADER_ONLY
)

add_executable (athena-web-server
  athena_web_server.cpp
)

target_include_directories (athena-web-server PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${Boost_INCLUDE_DIRS}
)

target_compile_definitions (athena-web-server PRIVATE
  BOOST_ERROR_CODE_HEADER_ONLY
  ATHENA_WEB_SOURCE_ASSET_DIR="${CMAKE_CURRENT_SOURCE_DIR}/web"
  ATHENA_WEB_INSTALL_ASSET_DIR="${CMAKE_INSTALL_PREFIX}/share/ATHENA/web"
)

target_link_libraries (athena-web-server PRIVATE
  athena-web-server-common
  CURL::libcurl
  pthread
)

if (BUILD_TESTS)
  add_executable (athena-web-server-tests
    web_server_tests.cpp
  )

  target_link_libraries (athena-web-server-tests PRIVATE
    athena-web-server-common
  )

  add_test (NAME athena-web-server-tests COMMAND athena-web-server-tests)
  add_test (
    NAME athena-web-session-helper-tests
    COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/test_session_helper.py
  )
endif ()

install (TARGETS athena-web-server DESTINATION bin)
install (DIRECTORY web/ DESTINATION share/ATHENA/web)
