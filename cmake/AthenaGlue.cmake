set(ATHENA_GLUE_SOURCE_DIR "${ATHENA_SOURCE_DIR}/src/Scheme/Glue")
set(ATHENA_GLUE_OUTPUT_DIR "${ATHENA_BINARY_DIR}/generated/athena-glue")
set(ATHENA_GLUE_INPUTS)
set(ATHENA_GLUE_OUTPUTS)
foreach(group basic editor server native)
  list(APPEND ATHENA_GLUE_INPUTS "${ATHENA_GLUE_SOURCE_DIR}/${group}.xml")
  list(APPEND ATHENA_GLUE_OUTPUTS
    "${ATHENA_GLUE_OUTPUT_DIR}/glue_${group}.cpp")
endforeach()
set(ATHENA_GLUE_RESOURCE
    "${ATHENA_SOURCE_DIR}/ATHENA/progs/prog/glue-symbols.scm")
list(APPEND ATHENA_GLUE_OUTPUTS
  "${ATHENA_GLUE_OUTPUT_DIR}/glue-symbols.scm"
  "${ATHENA_GLUE_OUTPUT_DIR}/glue-auto-doc.en.tm"
  "${ATHENA_GLUE_RESOURCE}")

set(ATHENA_GLUE_COMMAND
  "${Python3_EXECUTABLE}" "${ATHENA_GLUE_SOURCE_DIR}/generate-glue.py"
  --output-dir "${ATHENA_GLUE_OUTPUT_DIR}" ${ATHENA_GLUE_INPUTS})
# The generated runtime module must exist before the Scheme source glob runs.
# Otherwise a clean checkout triggers another configure/full rebuild as soon
# as the first build creates it. The build rule below still tracks every input.
execute_process(COMMAND ${ATHENA_GLUE_COMMAND}
  RESULT_VARIABLE ATHENA_GLUE_RESULT ERROR_VARIABLE ATHENA_GLUE_ERROR)
if(NOT ATHENA_GLUE_RESULT EQUAL 0)
  message(FATAL_ERROR "ATHENA glue generation failed: ${ATHENA_GLUE_ERROR}")
endif()
configure_file("${ATHENA_GLUE_OUTPUT_DIR}/glue-symbols.scm"
               "${ATHENA_GLUE_RESOURCE}" COPYONLY)

add_custom_command(
  OUTPUT ${ATHENA_GLUE_OUTPUTS}
  COMMAND ${ATHENA_GLUE_COMMAND}
  COMMAND "${CMAKE_COMMAND}" -E make_directory
          "${ATHENA_SOURCE_DIR}/ATHENA/progs/prog"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different
          "${ATHENA_GLUE_OUTPUT_DIR}/glue-symbols.scm" "${ATHENA_GLUE_RESOURCE}"
  COMMAND "${CMAKE_COMMAND}" -E touch ${ATHENA_GLUE_OUTPUTS}
  DEPENDS ${ATHENA_GLUE_INPUTS}
          "${ATHENA_GLUE_SOURCE_DIR}/generate-glue.py"
  COMMENT "Generating C++ and Scheme bindings from ATHENA XML interfaces"
  VERBATIM)
add_custom_target(athena_glue DEPENDS ${ATHENA_GLUE_OUTPUTS})
set_property(TARGET athena_glue PROPERTY ATHENA_GLUE_INTERFACES "${ATHENA_GLUE_INPUTS}")
