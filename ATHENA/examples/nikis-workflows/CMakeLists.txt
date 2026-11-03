cmake_minimum_required(VERSION 3.20)
project(NIKISWorkflows VERSION 1.0.0 LANGUAGES CXX)
find_package(Qt6 REQUIRED COMPONENTS Widgets)
get_filename_component(ATHENA_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/../../.." ABSOLUTE)
add_subdirectory("${ATHENA_SOURCE}/clients/cpp" audmap EXCLUDE_FROM_ALL)
add_executable(nikis-workflows main.cpp workflows.cpp)
target_compile_features(nikis-workflows PRIVATE cxx_std_17)
target_link_libraries(nikis-workflows PRIVATE ATHENA::audmap Qt6::Widgets)
set_target_properties(nikis-workflows PROPERTIES
  OUTPUT_NAME plugin_exec RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package")
foreach(resource manifest.json settings.json README.md)
  configure_file("${resource}" "package/${resource}" COPYONLY)
endforeach()
configure_file("${ATHENA_SOURCE}/LICENSE" "package/LICENSE.txt" COPYONLY)
# Keep the matching GPL source with the executable, not only in the repository.
foreach(resource CMakeLists.txt main.cpp workflows.cpp workflows.hpp manifest.json settings.json README.md)
  configure_file("${resource}" "package/source/ATHENA/examples/nikis-workflows/${resource}" COPYONLY)
endforeach()
configure_file("${ATHENA_SOURCE}/LICENSE" "package/source/LICENSE" COPYONLY)
file(COPY "${ATHENA_SOURCE}/clients/cpp" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}/package/source/clients")
foreach(resource codec.cpp codec.hpp identity.cpp identity.hpp protocol.hpp)
  configure_file("${ATHENA_SOURCE}/src/Services/AUDMAP/${resource}" "package/source/src/Services/AUDMAP/${resource}" COPYONLY)
endforeach()
configure_file("${ATHENA_SOURCE}/src/Interop/Engine/value.hpp" "package/source/src/Interop/Engine/value.hpp" COPYONLY)
add_custom_target(nikis-workflows-package
  COMMAND ${CMAKE_COMMAND} -E tar cf "${CMAKE_CURRENT_BINARY_DIR}/nikis-workflows.zip" --format=zip .
  WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/package"
  DEPENDS nikis-workflows)
