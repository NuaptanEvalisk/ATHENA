find_program (CARGO_EXECUTABLE cargo REQUIRED)

set (ATHENA_MATERIALS_CARGO_TARGET "${CMAKE_BINARY_DIR}/materials-engine-cargo")
set (ATHENA_MATERIALS_RUST_TARGET "" CACHE STRING
     "Rust target triple for the ATHENA Materials engine")
if (WIN32 AND CMAKE_CROSSCOMPILING AND NOT ATHENA_MATERIALS_RUST_TARGET)
  set (ATHENA_MATERIALS_RUST_TARGET "x86_64-pc-windows-gnu")
endif ()
if (ATHENA_PLATFORM_IPADOS AND NOT ATHENA_MATERIALS_RUST_TARGET)
  if (CMAKE_OSX_SYSROOT MATCHES "simulator")
    set (ATHENA_MATERIALS_RUST_TARGET "aarch64-apple-ios-sim")
  else ()
    set (ATHENA_MATERIALS_RUST_TARGET "aarch64-apple-ios")
  endif ()
endif ()
set (ATHENA_MATERIALS_ENGINE_SOURCE
     "${CMAKE_SOURCE_DIR}/tools/materials-engine/src/lib.rs")
set (ATHENA_MATERIALS_ENGINE_MANIFEST
     "${CMAKE_SOURCE_DIR}/tools/materials-engine/Cargo.toml")
if (ATHENA_MATERIALS_RUST_TARGET)
  set (ATHENA_MATERIALS_CARGO_TARGET_ARGS
       --target "${ATHENA_MATERIALS_RUST_TARGET}")
  set (ATHENA_MATERIALS_ENGINE_BINARY
       "${ATHENA_MATERIALS_CARGO_TARGET}/${ATHENA_MATERIALS_RUST_TARGET}/release/athena-materials-engine${CMAKE_EXECUTABLE_SUFFIX}")
else ()
  set (ATHENA_MATERIALS_CARGO_TARGET_ARGS)
  set (ATHENA_MATERIALS_ENGINE_BINARY
       "${ATHENA_MATERIALS_CARGO_TARGET}/release/athena-materials-engine${CMAKE_EXECUTABLE_SUFFIX}")
endif ()
get_filename_component (ATHENA_MATERIALS_OUTPUT_DIR
  "${ATHENA_MATERIALS_ENGINE_BINARY}" DIRECTORY)
set (ATHENA_MATERIALS_LIBRARY
  "${ATHENA_MATERIALS_OUTPUT_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}athena_materials${CMAKE_STATIC_LIBRARY_SUFFIX}")
set (ATHENA_MATERIALS_BUILD_ARGS --lib)
set (ATHENA_MATERIALS_OUTPUTS "${ATHENA_MATERIALS_LIBRARY}")
set (ATHENA_MATERIALS_CARGO_COMMAND "${CARGO_EXECUTABLE}")
if (ATHENA_PLATFORM_IPADOS)
  if (NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message (FATAL_ERROR
      "The iPadOS Materials build requires CMAKE_OSX_DEPLOYMENT_TARGET")
  endif ()
  set (ATHENA_MATERIALS_CARGO_COMMAND
       "${CMAKE_COMMAND}" -E env
       "IPHONEOS_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}"
       "${CARGO_EXECUTABLE}")
endif ()
if (ATHENA_ENABLE_CLI)
  list (APPEND ATHENA_MATERIALS_BUILD_ARGS --bin athena-materials-engine)
  list (APPEND ATHENA_MATERIALS_OUTPUTS "${ATHENA_MATERIALS_ENGINE_BINARY}")
endif ()

add_custom_command (
  OUTPUT ${ATHENA_MATERIALS_OUTPUTS}
  COMMAND ${ATHENA_MATERIALS_CARGO_COMMAND} build --release --locked
          ${ATHENA_MATERIALS_BUILD_ARGS}
          ${ATHENA_MATERIALS_CARGO_TARGET_ARGS}
          --manifest-path "${ATHENA_MATERIALS_ENGINE_MANIFEST}"
          --target-dir "${ATHENA_MATERIALS_CARGO_TARGET}"
  DEPENDS "${ATHENA_MATERIALS_ENGINE_SOURCE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/src/main.rs"
          "${ATHENA_MATERIALS_ENGINE_MANIFEST}"
          "${CMAKE_SOURCE_DIR}/tools/materials-engine/Cargo.lock"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/materials-engine"
  COMMENT "Building pinned Hayagriva Materials engine"
  VERBATIM)

add_custom_target (athena-materials-engine ALL
                   DEPENDS ${ATHENA_MATERIALS_OUTPUTS})
add_library (athena_materials_engine STATIC IMPORTED GLOBAL)
set_target_properties (athena_materials_engine PROPERTIES
  IMPORTED_LOCATION "${ATHENA_MATERIALS_LIBRARY}")
add_dependencies (athena_materials_engine athena-materials-engine)
target_link_libraries (athena_materials_engine INTERFACE ${CMAKE_DL_LIBS})
if (UNIX)
  target_link_libraries (athena_materials_engine INTERFACE pthread m)
endif ()
if (WIN32)
  target_link_libraries (athena_materials_engine INTERFACE
    ws2_32 userenv bcrypt ntdll)
endif ()
target_link_libraries (athena_body PUBLIC athena_materials_engine)
if (TARGET athena_test_body)
  target_link_libraries (athena_test_body PUBLIC athena_materials_engine)
endif ()
if (WIN32 OR APPLE)
  add_dependencies (ATHENA athena-materials-engine)
else ()
  add_dependencies (ATHENA.bin athena-materials-engine)
endif ()

if (WIN32)
  set (ATHENA_MATERIALS_RUNTIME_DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
else ()
  set (ATHENA_MATERIALS_RUNTIME_DESTINATION "${tmbin}/bin")
endif ()
if (ATHENA_ENABLE_CLI)
  install (PROGRAMS "${ATHENA_MATERIALS_ENGINE_BINARY}"
           DESTINATION "${ATHENA_MATERIALS_RUNTIME_DESTINATION}")
endif ()
