cmake_minimum_required(VERSION 3.20)
project(ATHENAAudmapClient VERSION 1.0.0 LANGUAGES CXX)
include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
find_package(Threads REQUIRED)
find_package(cppzmq REQUIRED CONFIG)
find_package(msgpack-cxx REQUIRED CONFIG)
find_package(nlohmann_json REQUIRED CONFIG)
get_filename_component(AUDMAP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)

if(NOT TARGET athena_audmap_wire)
  add_library(athena_audmap_wire STATIC
    ${AUDMAP_ROOT}/src/Subsystems/AUDMAP/codec.cpp
    ${AUDMAP_ROOT}/src/Subsystems/AUDMAP/identity.cpp)
  set_target_properties(athena_audmap_wire PROPERTIES AUTOMOC OFF)
  target_compile_features(athena_audmap_wire PUBLIC cxx_std_17)
  target_include_directories(athena_audmap_wire PUBLIC
    $<BUILD_INTERFACE:${AUDMAP_ROOT}/src/Subsystems/AUDMAP>
    $<BUILD_INTERFACE:${AUDMAP_ROOT}/src/ATHENA/Interop>)
  target_link_libraries(athena_audmap_wire PUBLIC nlohmann_json::nlohmann_json PRIVATE msgpack-cxx cppzmq)
endif()

add_library(athena_audmap_sdk STATIC src/client.cpp ${AUDMAP_ROOT}/tools/interop/connection.cpp)
add_library(ATHENA::audmap ALIAS athena_audmap_sdk)
set_target_properties(athena_audmap_sdk PROPERTIES AUTOMOC OFF EXPORT_NAME audmap)
target_compile_features(athena_audmap_sdk PUBLIC cxx_std_17)
target_include_directories(athena_audmap_sdk PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
  PRIVATE ${AUDMAP_ROOT}/tools/interop)
target_link_libraries(athena_audmap_sdk PUBLIC nlohmann_json::nlohmann_json PRIVATE athena_audmap_wire cppzmq Threads::Threads)

install(TARGETS athena_audmap_sdk athena_audmap_wire EXPORT ATHENAAudmapTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(EXPORT ATHENAAudmapTargets NAMESPACE ATHENA:: DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ATHENAAudmap)
configure_package_config_file(ATHENAAudmapConfig.cmake.in
  "${CMAKE_CURRENT_BINARY_DIR}/ATHENAAudmapConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ATHENAAudmap)
write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/ATHENAAudmapConfigVersion.cmake"
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/ATHENAAudmapConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/ATHENAAudmapConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/ATHENAAudmap)
add_executable(audmap-sdk-example example.cpp)
set_target_properties(audmap-sdk-example PROPERTIES AUTOMOC OFF)
target_link_libraries(audmap-sdk-example PRIVATE ATHENA::audmap)
