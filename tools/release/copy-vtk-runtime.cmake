if (NOT DEFINED ATHENA_EXECUTABLE OR ATHENA_EXECUTABLE STREQUAL "")
  message (FATAL_ERROR "ATHENA_EXECUTABLE is required")
endif ()
if (NOT DEFINED ATHENA_RUNTIME_LIB_DIR OR ATHENA_RUNTIME_LIB_DIR STREQUAL "")
  message (FATAL_ERROR "ATHENA_RUNTIME_LIB_DIR is required")
endif ()

set (_athena_search_dirs)
foreach (_index RANGE 0 7)
  if (DEFINED ATHENA_SEARCH_DIR_${_index} AND
      NOT ATHENA_SEARCH_DIR_${_index} STREQUAL "")
    list (APPEND _athena_search_dirs "${ATHENA_SEARCH_DIR_${_index}}")
  endif ()
endforeach ()

file (MAKE_DIRECTORY "${ATHENA_RUNTIME_LIB_DIR}")
file (GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${ATHENA_EXECUTABLE}"
  DIRECTORIES ${_athena_search_dirs}
  PRE_INCLUDE_REGEXES "^libvtk.*\\.so(\\..*)?$"
  PRE_EXCLUDE_REGEXES ".*"
  RESOLVED_DEPENDENCIES_VAR _athena_resolved
  UNRESOLVED_DEPENDENCIES_VAR _athena_unresolved)

set (_athena_vtk_count 0)
foreach (_dependency IN LISTS _athena_resolved)
  get_filename_component (_name "${_dependency}" NAME)
  if (_name MATCHES "^libvtk.*\\.so(\\..*)?$")
    file (COPY_FILE "${_dependency}"
      "${ATHENA_RUNTIME_LIB_DIR}/${_name}" ONLY_IF_DIFFERENT)
    math (EXPR _athena_vtk_count "${_athena_vtk_count} + 1")
  endif ()
endforeach ()

foreach (_dependency IN LISTS _athena_unresolved)
  if (_dependency MATCHES "^libvtk.*\\.so(\\..*)?$")
    message (FATAL_ERROR "Unresolved VTK runtime dependency: ${_dependency}")
  endif ()
endforeach ()

if (_athena_vtk_count EQUAL 0)
  message (FATAL_ERROR
    "No VTK runtime dependencies were discovered for ${ATHENA_EXECUTABLE}")
endif ()

message (STATUS
  "Copied ${_athena_vtk_count} VTK runtime libraries to ${ATHENA_RUNTIME_LIB_DIR}")
