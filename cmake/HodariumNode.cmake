# Standalone build graph: never include src/CMakeLists.txt or desktop discovery.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "Hodarium node currently requires Linux: the private file key backend uses renameat2")
endif()
set(CMAKE_AUTOMOC OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Threads REQUIRED)
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Network WebSockets)
find_package(SQLite3 REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBSODIUM REQUIRED IMPORTED_TARGET libsodium)
pkg_check_modules(GNUTLS REQUIRED IMPORTED_TARGET gnutls)
pkg_check_modules(SPDLOG REQUIRED IMPORTED_TARGET spdlog)
find_path(NLOHMANN_JSON_INCLUDE_DIR nlohmann/json.hpp REQUIRED)
find_package(ICU REQUIRED COMPONENTS uc i18n)
find_package(mimalloc REQUIRED)

# The Hodarium subdirectory also supports the desktop's older SQLite variables.
set(SQLITE3_INCLUDE_DIRS "${SQLite3_INCLUDE_DIRS}")
if(TARGET SQLite3::SQLite3)
  set(SQLITE3_LIBRARIES SQLite3::SQLite3)
else()
  set(SQLITE3_LIBRARIES SQLite::SQLite3)
endif()
set(LIBSODIUM_LIBRARIES PkgConfig::LIBSODIUM)
set(GNUTLS_LIBRARIES PkgConfig::GNUTLS)
add_subdirectory(src/ATHENA/Hodarium)

set(HODARIUM_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/hodarium-config")
math(EXPR HODARIUM_WORD_LENGTH_INC "${CMAKE_SIZEOF_VOID_P} - 1")
set(OS_GNU_LINUX 1)
configure_file("${CMAKE_CURRENT_LIST_DIR}/HodariumNodeConfig.h.in"
  "${HODARIUM_CONFIG_DIR}/config.h" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/HodariumNodeConfig.h.in"
  "${HODARIUM_CONFIG_DIR}/tm_configure.hpp" @ONLY)

# Explicit source list: do not accidentally import editor or inference code.
# This archive is also the integration boundary for the extracted disk adapters.
add_library(athena_hodarium_node_core STATIC
  src/System/Misc/fast_alloc.cpp
  src/System/Boot/headless.cpp
  src/System/Files/tm_ostream.cpp
  src/Kernel/Abstractions/basic_runtime.cpp
  src/Kernel/Types/string.cpp
  src/Kernel/Types/tree.cpp
  src/Kernel/Types/tree_label.cpp
  src/Kernel/Types/node_metadata.cpp
  src/Data/String/unicode_text.cpp
  src/Data/Drd/standard_tree_labels.cpp
  src/Data/Drd/vars.cpp
  src/Data/Convert/Xml/athena_document_xml.cpp
  src/Data/Convert/Xml/document_upgrade_file.cpp
  src/System/Files/confined_filesystem.cpp
  src/ATHENA/Data/background_workers.cpp
  src/ATHENA/Data/document_history_store.cpp
  src/ATHENA/Data/fossil_delta.cpp
  src/ATHENA/Data/hodarium_resources.cpp
  src/ATHENA/Data/hodarium_application.cpp
  src/ATHENA/Data/hodarium_worker.cpp
  src/ATHENA/Data/hodarium_local_notifications.cpp
  src/ATHENA/Data/hodarium_inventory.cpp
  src/ATHENA/Data/hodarium_logical_database.cpp
  src/ATHENA/Data/hodarium_artifact_database.cpp
  src/ATHENA/Data/hodarium_logical_sync.cpp
  src/ATHENA/Data/hodarium_rename_records.cpp
  src/ATHENA/Data/namespaces_schema.cpp
  src/ATHENA/Data/namespaces_template.cpp
  src/ATHENA/Data/materials.cpp
  src/ATHENA/Data/materials_schema.cpp
  src/ATHENA/Data/vaultfile_json.cpp
  src/ATHENA/Data/artifact_title_filter.cpp
  src/ATHENA/Data/interop_document_validation.cpp)
target_compile_features(athena_hodarium_node_core PUBLIC cxx_std_17)
target_include_directories(athena_hodarium_node_core PUBLIC
  "${HODARIUM_CONFIG_DIR}"
  "${ATHENA_SOURCE_DIR}/src")
foreach(directory
    Kernel/Abstractions Kernel/Containers Kernel/Types
    System System/Misc System/Files System/Classes System/Boot
    Data/String Data/Convert Data/Convert/Xml Data/Drd Data/Tree Data/Document
    ATHENA ATHENA/Data)
  target_include_directories(athena_hodarium_node_core PUBLIC
    "${ATHENA_SOURCE_DIR}/src/${directory}")
endforeach()
target_include_directories(athena_hodarium_node_core PRIVATE
  "${NLOHMANN_JSON_INCLUDE_DIR}")
target_link_libraries(athena_hodarium_node_core PUBLIC
  athena_hodarium Qt6::Core ${SQLITE3_LIBRARIES} ICU::uc ICU::i18n
  mimalloc Threads::Threads PkgConfig::SPDLOG)

option(ATHENA_HODARIUM_NODE_TESTS "Build focused Hodarium node test targets" OFF)
if(ATHENA_HODARIUM_NODE_TESTS)
  add_executable(hodarium_native_runtime_test EXCLUDE_FROM_ALL
    tests/hodarium/native_runtime_test.cpp)
  target_link_libraries(hodarium_native_runtime_test PRIVATE athena_hodarium_node_core)
endif()

# The executable is owned by the node implementation, not the desktop graph.
add_subdirectory(src/ATHENA/HodariumNode)
