set (ATHENA_RUNTIME_INSTALL_DESTINATION share/ATHENA)
if (WIN32)
  set (ATHENA_RUNTIME_INSTALL_DESTINATION "${CMAKE_INSTALL_PREFIX}")
endif ()

install (DIRECTORY . DESTINATION "${ATHENA_RUNTIME_INSTALL_DESTINATION}"
  FILE_PERMISSIONS OWNER_READ GROUP_READ WORLD_READ
  PATTERN "include" EXCLUDE
  PATTERN "CMakeLists.txt" EXCLUDE
  REGEX "/(progs|lib/(athena-scheme|athena-guile|guile)|share/guile)(/|$)" EXCLUDE
  REGEX "/lib(athena-)?guile[^/]*$" EXCLUDE
  REGEX "/libgc([.-][^/]*)?$" EXCLUDE)

if (TARGET athena_math_icons)
  install (DIRECTORY "${CMAKE_BINARY_DIR}/generated/math-icons-v3/"
    DESTINATION "${ATHENA_RUNTIME_INSTALL_DESTINATION}/misc/images/math-icons-v3")
endif ()
