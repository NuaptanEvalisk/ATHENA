set(ATHENA_GLUE_SOURCE_DIR "${ATHENA_SOURCE_DIR}/src/Scheme/Glue")
set(ATHENA_GLUE_OUTPUT_DIR "${ATHENA_BINARY_DIR}/generated/athena-glue")
set(ATHENA_GLUE_INPUTS)
set(ATHENA_GLUE_OUTPUTS)
foreach(group basic editor server native)
  list(APPEND ATHENA_GLUE_INPUTS "${ATHENA_GLUE_SOURCE_DIR}/${group}.xml")
  list(APPEND ATHENA_GLUE_OUTPUTS
    "${ATHENA_GLUE_OUTPUT_DIR}/glue_${group}.cpp")
endforeach()
list(APPEND ATHENA_GLUE_OUTPUTS
  "${ATHENA_GLUE_OUTPUT_DIR}/glue-auto-doc.en.tm")

set(ATHENA_GLUE_COMMAND
  "${Python3_EXECUTABLE}" "${ATHENA_GLUE_SOURCE_DIR}/generate-glue.py"
  --output-dir "${ATHENA_GLUE_OUTPUT_DIR}" ${ATHENA_GLUE_INPUTS})
execute_process(COMMAND ${ATHENA_GLUE_COMMAND}
  RESULT_VARIABLE ATHENA_GLUE_RESULT ERROR_VARIABLE ATHENA_GLUE_ERROR)
if(NOT ATHENA_GLUE_RESULT EQUAL 0)
  message(FATAL_ERROR "ATHENA glue generation failed: ${ATHENA_GLUE_ERROR}")
endif()
# Remove the obsolete generated Scheme completion inventory from older builds.
file(REMOVE "${ATHENA_SOURCE_DIR}/ATHENA/progs/prog/glue-symbols.scm")
file(REMOVE "${ATHENA_GLUE_OUTPUT_DIR}/glue-symbols.scm")

add_custom_command(
  OUTPUT ${ATHENA_GLUE_OUTPUTS}
  COMMAND ${ATHENA_GLUE_COMMAND}
  COMMAND "${CMAKE_COMMAND}" -E touch ${ATHENA_GLUE_OUTPUTS}
  DEPENDS ${ATHENA_GLUE_INPUTS}
          "${ATHENA_GLUE_SOURCE_DIR}/generate-glue.py"
  COMMENT "Generating C++ bindings and API documentation from ATHENA XML interfaces"
  VERBATIM)
add_custom_target(athena_glue DEPENDS ${ATHENA_GLUE_OUTPUTS})
set_property(TARGET athena_glue PROPERTY ATHENA_GLUE_INTERFACES "${ATHENA_GLUE_INPUTS}")
