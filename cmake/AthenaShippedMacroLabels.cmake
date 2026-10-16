include_guard(GLOBAL)
find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(ATHENA_SHIPPED_LABEL_DIR "${ATHENA_BINARY_DIR}/generated/athena-drd")
set(ATHENA_SHIPPED_LABEL_HEADER "${ATHENA_SHIPPED_LABEL_DIR}/shipped_macro_labels.inc")
file(GLOB_RECURSE ATHENA_SHIPPED_STYLE_SOURCES CONFIGURE_DEPENDS
  "${ATHENA_SOURCE_DIR}/ATHENA/packages/*.ats"
  "${ATHENA_SOURCE_DIR}/ATHENA/styles/*.ats")
set(ATHENA_SHIPPED_LABEL_MANIFEST "${ATHENA_SHIPPED_LABEL_DIR}/style_sources.txt")
string(REPLACE ";" "\n" ATHENA_SHIPPED_LABEL_SOURCE_LIST "${ATHENA_SHIPPED_STYLE_SOURCES}")
file(GENERATE OUTPUT "${ATHENA_SHIPPED_LABEL_MANIFEST}"
  CONTENT "${ATHENA_SHIPPED_LABEL_SOURCE_LIST}\n")
add_custom_command(
  OUTPUT "${ATHENA_SHIPPED_LABEL_HEADER}"
  COMMAND "${Python3_EXECUTABLE}"
    "${ATHENA_SOURCE_DIR}/tools/generate-shipped-macro-labels.py"
    --resources "${ATHENA_SOURCE_DIR}/ATHENA"
    --output "${ATHENA_SHIPPED_LABEL_HEADER}"
  COMMAND "${CMAKE_COMMAND}" -E touch "${ATHENA_SHIPPED_LABEL_HEADER}"
  DEPENDS ${ATHENA_SHIPPED_STYLE_SOURCES}
    "${ATHENA_SHIPPED_LABEL_MANIFEST}"
    "${ATHENA_SOURCE_DIR}/tools/generate-shipped-macro-labels.py"
  COMMENT "Cataloging declared labels from shipped ATHENA styles"
  VERBATIM)
add_custom_target(athena_shipped_macro_labels DEPENDS "${ATHENA_SHIPPED_LABEL_HEADER}")

function(athena_use_shipped_macro_labels target)
  add_dependencies(${target} athena_shipped_macro_labels)
  set_source_files_properties(
    "${ATHENA_SOURCE_DIR}/src/Data/Drd/standard_tree_labels.cpp"
    PROPERTIES INCLUDE_DIRECTORIES "${ATHENA_SHIPPED_LABEL_DIR}")
endfunction()
