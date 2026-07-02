set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

if(NOT DEFINED ATHENA_WIN64_PREFIX)
  if(DEFINED ENV{ATHENA_WIN64_PREFIX})
    file(TO_CMAKE_PATH "$ENV{ATHENA_WIN64_PREFIX}" ATHENA_WIN64_PREFIX)
  else()
    message(FATAL_ERROR
      "Set ATHENA_WIN64_PREFIX to the Windows target dependency prefix")
  endif()
endif()

set(CMAKE_C_COMPILER x86_64-w64-mingw32-gcc)
set(CMAKE_CXX_COMPILER x86_64-w64-mingw32-g++)
set(CMAKE_RC_COMPILER x86_64-w64-mingw32-windres)

set(CMAKE_C_FLAGS_INIT "-Drandom=rand -Dsrandom=srand -DGNUTLS_STATIC -include ${ATHENA_WIN64_PREFIX}/include/athena-win64-compat.h")
set(CMAKE_CXX_FLAGS_INIT "-Drandom=rand -Dsrandom=srand -DGNUTLS_STATIC -include ${ATHENA_WIN64_PREFIX}/include/athena-win64-compat.h -fpermissive")
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "-static-libstdc++ -static-libgcc")
set(CMAKE_CXX_STANDARD_LIBRARIES_INIT
  "-Wl,--start-group ${ATHENA_WIN64_PREFIX}/lib/ggml-cpu.a ${ATHENA_WIN64_PREFIX}/lib/libhogweed.a ${ATHENA_WIN64_PREFIX}/lib/libnettle.a ${ATHENA_WIN64_PREFIX}/lib/libtasn1.a ${ATHENA_WIN64_PREFIX}/lib/libgmp.a -latomic -lcrypt32 -lncrypt -lbcrypt -ladvapi32 -ldbghelp -lucrt -Wl,--end-group")
set(CMAKE_C_STANDARD_LIBRARIES_INIT
  "-Wl,--start-group ${ATHENA_WIN64_PREFIX}/lib/libhogweed.a ${ATHENA_WIN64_PREFIX}/lib/libnettle.a ${ATHENA_WIN64_PREFIX}/lib/libtasn1.a ${ATHENA_WIN64_PREFIX}/lib/libgmp.a -latomic -lcrypt32 -lncrypt -lbcrypt -ladvapi32 -ldbghelp -lucrt -Wl,--end-group")

set(CMAKE_FIND_ROOT_PATH "${ATHENA_WIN64_PREFIX}")
set(CMAKE_PREFIX_PATH "${ATHENA_WIN64_PREFIX}")

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

set(CMAKE_INSTALL_PREFIX "${ATHENA_WIN64_PREFIX}/athena-install" CACHE PATH
  "ATHENA Windows staging install prefix")
