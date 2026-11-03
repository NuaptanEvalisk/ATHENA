# Standalone build graph: never include src/CMakeLists.txt or desktop discovery.
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
  message(FATAL_ERROR
    "Hodarium node currently requires Linux: the private file key backend uses renameat2")
endif()
set(CMAKE_AUTOMOC OFF)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
include("${CMAKE_CURRENT_LIST_DIR}/AthenaShippedMacroLabels.cmake")
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
add_subdirectory(src/Sync/Hodarium)

set(HODARIUM_CONFIG_DIR "${CMAKE_CURRENT_BINARY_DIR}/hodarium-config")
math(EXPR HODARIUM_WORD_LENGTH_INC "${CMAKE_SIZEOF_VOID_P} - 1")
set(OS_GNU_LINUX 1)
configure_file("${CMAKE_CURRENT_LIST_DIR}/HodariumNodeConfig.h.in"
  "${HODARIUM_CONFIG_DIR}/config.h" @ONLY)
configure_file("${CMAKE_CURRENT_LIST_DIR}/HodariumNodeConfig.h.in"
  "${HODARIUM_CONFIG_DIR}/tm_configure.hpp" @ONLY)

# The standalone source graph consumes the same embedded node field contracts
# as the desktop, but does not enter src/CMakeLists.txt to generate the header.
file(READ "${ATHENA_SOURCE_DIR}/ATHENA/misc/node-fields.json"
  ATHENA_NATIVE_FIELDS_JSON)
configure_file("${ATHENA_SOURCE_DIR}/src/Document/NodeContracts/native_fields_data.hpp.in"
  "${HODARIUM_CONFIG_DIR}/native_fields_data.hpp" @ONLY)

# Explicit source list: do not accidentally import editor or inference code.
# This archive is also the integration boundary for the extracted disk adapters.
add_library(athena_hodarium_node_core STATIC
  src/Infrastructure/Misc/fast_alloc.cpp
  src/App/Boot/headless.cpp
  src/Infrastructure/Files/tm_ostream.cpp
  src/Core/Abstractions/basic_runtime.cpp
  src/Core/Types/string.cpp
  src/Core/Types/tree.cpp
  src/Core/Types/tree_label.cpp
  src/Core/Types/node_metadata.cpp
  src/Core/Text/unicode_text.cpp
  src/Core/Types/standard_tree_labels.cpp
  src/Typesetting/Env/vars.cpp
  src/Document/Convert/Xml/athena_document_xml.cpp
  src/Document/Convert/Xml/athena_document_xml_v3.cpp
  src/Document/Convert/Xml/native_xml_codec.cpp
  src/Document/Tree/math_tree_v3.cpp
  src/Document/NodeContracts/native_fields.cpp
  src/Core/Language/named_symbol.cpp
  src/Document/Convert/Xml/document_upgrade_file.cpp
  src/Infrastructure/Files/confined_filesystem.cpp
  src/App/Runtime/background_workers.cpp
  src/Sync/State/document_history_store.cpp
  src/Sync/State/fossil_delta.cpp
  src/Sync/State/hodarium_resources.cpp
  src/Sync/State/hodarium_application.cpp
  src/Sync/State/hodarium_worker.cpp
  src/Sync/State/hodarium_local_notifications.cpp
  src/Sync/State/hodarium_inventory.cpp
  src/Sync/State/hodarium_logical_database.cpp
  src/Sync/State/hodarium_artifact_database.cpp
  src/Sync/State/hodarium_logical_sync.cpp
  src/Sync/State/hodarium_rename_records.cpp
  src/Domains/Namespaces/namespaces_schema.cpp
  src/Domains/Namespaces/namespaces_template.cpp
  src/Materials/Model/materials.cpp
  src/Materials/Model/materials_schema.cpp
  src/Domains/Vault/vaultfile_json.cpp
  src/Domains/Artifacts/artifact_title_filter.cpp
  src/Interop/Adapters/interop_document_validation.cpp)
target_compile_features(athena_hodarium_node_core PUBLIC cxx_std_17)
target_compile_definitions(athena_hodarium_node_core PRIVATE ATHENA_HODARIUM_NODE_CORE=1)
qt_add_resources(athena_hodarium_node_core athena_hodarium_symbols
  PREFIX "/athena/symbols"
  BASE "${ATHENA_SOURCE_DIR}/ATHENA/misc/symbols"
  FILES "${ATHENA_SOURCE_DIR}/ATHENA/misc/symbols/named-symbols.json")
athena_use_shipped_macro_labels(athena_hodarium_node_core)
target_include_directories(athena_hodarium_node_core PUBLIC
  "${HODARIUM_CONFIG_DIR}"
  "${ATHENA_SOURCE_DIR}/src")
foreach(directory
    App/Boot App/Runtime Core/Abstractions
    Core/Containers Core/Language Core/Text
    Core/Types Core/Utilities Document/Convert
    Document/Convert/Xml Document/Model Document/NodeContracts
    Document/State Document/Tree Domains/Artifacts
    Domains/Namespaces Domains/Vault Infrastructure
    Infrastructure/Files Infrastructure/Misc Interop/Adapters
    Interop/Engine Materials/Model Platform/Unix
    Rendering/Fonts Sync/State Typesetting/Env
)
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
add_subdirectory(src/Sync/Node)
