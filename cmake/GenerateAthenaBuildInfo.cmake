# Generate the About dialog's build identity without forcing CMake to
# reconfigure, or changing the global tm_configure.hpp, after every commit.

set (git_commit "")
set (git_branch "")
set (git_tag "")
set (git_dirty 0)

find_program (git_executable git)
if (git_executable AND EXISTS "${ATHENA_SOURCE_DIR}/.git")
  execute_process (
    COMMAND "${git_executable}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${ATHENA_SOURCE_DIR}"
    OUTPUT_VARIABLE git_commit
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process (
    COMMAND "${git_executable}" branch --show-current
    WORKING_DIRECTORY "${ATHENA_SOURCE_DIR}"
    OUTPUT_VARIABLE git_branch
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process (
    COMMAND "${git_executable}" describe --tags --exact-match HEAD
    WORKING_DIRECTORY "${ATHENA_SOURCE_DIR}"
    OUTPUT_VARIABLE git_tag
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  execute_process (
    COMMAND "${git_executable}" status --porcelain --untracked-files=no
    WORKING_DIRECTORY "${ATHENA_SOURCE_DIR}"
    OUTPUT_VARIABLE git_status
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
  if (NOT git_status STREQUAL "")
    set (git_dirty 1)
  endif ()
endif ()

set (development_build 0)
string (TOLOWER "${ATHENA_BUILD_TYPE}" build_type_lower)
if (build_type_lower STREQUAL "debug" OR git_dirty)
  set (development_build 1)
elseif (NOT git_commit STREQUAL "" AND
        NOT git_tag STREQUAL "v${ATHENA_APP_VERSION}" AND
        NOT git_tag STREQUAL "v${ATHENA_APP_VERSION}.0")
  set (development_build 1)
endif ()

foreach (value git_commit git_branch ATHENA_BUILD_TYPE ATHENA_COMPILER)
  string (REPLACE "\\" "\\\\" ${value} "${${value}}")
  string (REPLACE "\"" "\\\"" ${value} "${${value}}")
endforeach ()

set (content "/******************************************************************************\n")
string (APPEND content "* MODULE     : athena_build_info.hpp\n")
string (APPEND content "* DESCRIPTION: Generated build identity for diagnostic user interfaces\n")
string (APPEND content "*******************************************************************************/\n\n")
string (APPEND content "#ifndef ATHENA_BUILD_INFO_HPP\n#define ATHENA_BUILD_INFO_HPP\n\n")
string (APPEND content "#define ATHENA_BUILD_TYPE \"${ATHENA_BUILD_TYPE}\"\n")
string (APPEND content "#define ATHENA_COMPILER \"${ATHENA_COMPILER}\"\n")
string (APPEND content "#define ATHENA_GIT_COMMIT \"${git_commit}\"\n")
string (APPEND content "#define ATHENA_GIT_BRANCH \"${git_branch}\"\n")
string (APPEND content "#define ATHENA_GIT_DIRTY ${git_dirty}\n")
string (APPEND content "#define ATHENA_DEVELOPMENT_BUILD ${development_build}\n\n")
string (APPEND content "#endif // defined ATHENA_BUILD_INFO_HPP\n")

get_filename_component (output_dir "${ATHENA_OUTPUT}" DIRECTORY)
file (MAKE_DIRECTORY "${output_dir}")
set (temporary "${ATHENA_OUTPUT}.tmp")
file (WRITE "${temporary}" "${content}")
execute_process (COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                 "${temporary}" "${ATHENA_OUTPUT}")
file (REMOVE "${temporary}")
