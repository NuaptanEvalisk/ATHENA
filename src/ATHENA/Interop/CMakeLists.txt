find_package(pegtl 3 REQUIRED CONFIG)
find_package(Threads REQUIRED)
find_package(msgpack-cxx REQUIRED CONFIG)
if(ATHENA_ENABLE_AUDMAP_LISTENER OR ATHENA_ENABLE_CLI OR ATHENA_ENABLE_AUDMAP_REPL)
  find_package(cppzmq REQUIRED CONFIG)
endif()

add_library(athena_interop STATIC selection.cpp resolution.cpp traversal.cpp)
set_target_properties(athena_interop PROPERTIES AUTOMOC OFF)
target_compile_features(athena_interop PUBLIC cxx_std_17)
target_include_directories(athena_interop PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
  ${NLOHMANN_JSON_INCLUDE_DIR})
target_link_libraries(athena_interop PUBLIC Threads::Threads PRIVATE taocpp::pegtl)

if(ATHENA_ENABLE_AUDMAP_LISTENER OR ATHENA_ENABLE_CLI OR ATHENA_ENABLE_AUDMAP_REPL)
  add_subdirectory(${ATHENA_SOURCE_DIR}/clients/cpp ${CMAKE_CURRENT_BINARY_DIR}/client-sdk)
else()
  add_library(athena_audmap_wire STATIC
    ../../Subsystems/AUDMAP/codec.cpp)
  set_target_properties(athena_audmap_wire PROPERTIES AUTOMOC OFF)
  target_compile_features(athena_audmap_wire PUBLIC cxx_std_17)
  target_include_directories(athena_audmap_wire PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../../Subsystems/AUDMAP
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${NLOHMANN_JSON_INCLUDE_DIR})
  target_link_libraries(athena_audmap_wire PRIVATE msgpack-cxx)
endif()

add_library(athena_audmap STATIC
  ../../Subsystems/AUDMAP/session.cpp)
if(ATHENA_ENABLE_AUDMAP_LISTENER)
  target_sources(athena_audmap PRIVATE ../../Subsystems/AUDMAP/server.cpp)
endif()
set_target_properties(athena_audmap PROPERTIES AUTOMOC OFF)
target_include_directories(athena_audmap PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/../../Subsystems/AUDMAP)
target_link_libraries(athena_audmap PUBLIC athena_interop athena_audmap_wire)
if(ATHENA_ENABLE_AUDMAP_LISTENER)
  target_link_libraries(athena_audmap PRIVATE cppzmq)
endif()

if(ATHENA_ENABLE_CLI OR ATHENA_ENABLE_AUDMAP_REPL)
add_library(athena_audmap_client STATIC
  ${ATHENA_SOURCE_DIR}/tools/interop/connection.cpp
  ${ATHENA_SOURCE_DIR}/tools/interop/repl_session.cpp)
set_target_properties(athena_audmap_client PROPERTIES AUTOMOC OFF)
target_include_directories(athena_audmap_client PUBLIC ${ATHENA_SOURCE_DIR}/tools/interop)
target_link_libraries(athena_audmap_client PUBLIC athena_audmap_wire cppzmq PRIVATE athena_interop)

add_executable(athena-audmap ${ATHENA_SOURCE_DIR}/tools/interop/client.cpp)
set_target_properties(athena-audmap PROPERTIES AUTOMOC OFF)
target_link_libraries(athena-audmap PRIVATE athena_audmap_client)
endif()

# GNU Readline supplies maintained line editing, Unicode terminal handling and
# callback-mode input; its GPL-3.0-or-later license matches ATHENA.
if(ATHENA_ENABLE_AUDMAP_REPL)
find_path(READLINE_INCLUDE_DIR readline/readline.h REQUIRED)
find_library(READLINE_LIBRARY NAMES readline REQUIRED)
add_executable(athena-audmap-repl ${ATHENA_SOURCE_DIR}/tools/interop/repl.cpp)
set_target_properties(athena-audmap-repl PROPERTIES AUTOMOC OFF)
target_include_directories(athena-audmap-repl PRIVATE ${READLINE_INCLUDE_DIR})
target_link_libraries(athena-audmap-repl PRIVATE athena_audmap_client ${READLINE_LIBRARY})
endif()
