find_program (CARGO_EXECUTABLE cargo REQUIRED)

set (ATHENA_MATERIALS_CARGO_TARGET "${CMAKE_BINARY_DIR}/materials-engine-cargo")
set (ATHENA_MATERIALS_RUST_TARGET "" CACHE STRING
     "Rust target triple for the ATHENA Materials engine")
if (WIN32 AND CMAKE_CROSSCOMPILING AND NOT ATHENA_MATERIALS_RUST_TARGET)
  set (ATHENA_MATERIALS_RUST_TARGET "x86_64-pc-windows-gnu")
endif ()
set (ATHENA_MATERIALS_ENGINE_SOURCE
     "${CMAKE_SOURCE_DIR}/tools/materials-engine/src/main.rs")
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

add_custom_command (
  OUTPUT "${ATHENA_MATERIALS_ENGINE_BINARY}"
  COMMAND "${CARGO_EXECUTABLE}" build --release --locked
          ${ATHENA_MATERIALS_CARGO_TARGET_ARGS}
          --manifest-path "${ATHENA_MATERIALS_ENGINE_MANIFEST}"
          --target-dir "${ATHENA_MATERIALS_CARGO_TARGET}"
  DEPENDS "${ATHENA_MATERIALS_ENGINE_SOURCE}"
          "${ATHENA_MATERIALS_ENGINE_MANIFEST}"
          "${CMAKE_SOURCE_DIR}/tools/materials-engine/Cargo.lock"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}/tools/materials-engine"
  COMMENT "Building pinned Hayagriva Materials engine"
  VERBATIM)

add_custom_target (athena-materials-engine ALL
                   DEPENDS "${ATHENA_MATERIALS_ENGINE_BINARY}")
if (WIN32)
  add_dependencies (ATHENA athena-materials-engine)
else ()
  add_dependencies (ATHENA.bin athena-materials-engine)
endif ()

if (WIN32)
  set (ATHENA_MATERIALS_RUNTIME_DESTINATION "${CMAKE_INSTALL_PREFIX}/bin")
else ()
  set (ATHENA_MATERIALS_RUNTIME_DESTINATION "${tmbin}/bin")
endif ()
install (PROGRAMS "${ATHENA_MATERIALS_ENGINE_BINARY}"
         DESTINATION "${ATHENA_MATERIALS_RUNTIME_DESTINATION}")
