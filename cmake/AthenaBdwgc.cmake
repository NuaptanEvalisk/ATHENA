include(ExternalProject)

set(ATHENA_BDWGC_SOURCE_DIR
  "${ATHENA_SOURCE_DIR}/3rdparty/athena-bdwgc")
set(ATHENA_BDWGC_BUILD_DIR
  "${ATHENA_BINARY_DIR}/athena-bdwgc-build")
set(ATHENA_BDWGC_PREFIX
  "${ATHENA_BINARY_DIR}/athena-bdwgc-runtime")
set(ATHENA_BDWGC_LIBRARY
  "${ATHENA_BDWGC_PREFIX}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}gc${CMAKE_STATIC_LIBRARY_SUFFIX}")
set(ATHENA_BDWGC_PKGCONFIG_DIR
  "${ATHENA_BDWGC_PREFIX}/lib/pkgconfig")

set(ATHENA_BDWGC_C_FLAGS
  "-O3 -g -fPIC -fno-omit-frame-pointer ${ATHENA_CPU_COMPILE_FLAGS} -funroll-loops -flto=thin -DGC_USE_ENTIRE_HEAP")
set(ATHENA_BDWGC_LINK_FLAGS
  "-flto=thin -fuse-ld=lld -Wl,--thinlto-cache-dir=${ATHENA_BDWGC_BUILD_DIR}/lto-cache")
set(ATHENA_BDWGC_TOOLCHAIN_FINGERPRINT
  "${ATHENA_BINARY_DIR}/athena-bdwgc-toolchain.txt")
athena_write_build_fingerprint(
  "${ATHENA_BDWGC_TOOLCHAIN_FINGERPRINT}"
  "CC=${CMAKE_C_COMPILER}\nCFLAGS=${ATHENA_BDWGC_C_FLAGS}\nLDFLAGS=${ATHENA_BDWGC_LINK_FLAGS}\n")

set(ATHENA_BDWGC_CMAKE_ARGS
  "-DCMAKE_INSTALL_PREFIX:PATH=${ATHENA_BDWGC_PREFIX}"
  "-DCMAKE_INSTALL_LIBDIR:STRING=lib"
  "-DCMAKE_BUILD_TYPE:STRING=RelWithDebInfo"
  "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
  "-DCMAKE_C_FLAGS:STRING=${ATHENA_BDWGC_C_FLAGS}"
  "-DCMAKE_EXE_LINKER_FLAGS:STRING=${ATHENA_BDWGC_LINK_FLAGS}"
  "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=ON"
  "-DBUILD_SHARED_LIBS:BOOL=OFF"
  "-Dbuild_cord:BOOL=OFF"
  "-Dbuild_tests:BOOL=OFF"
  "-Denable_docs:BOOL=OFF"
  "-Denable_threads:BOOL=ON"
  "-Denable_parallel_mark:BOOL=ON"
  "-Denable_thread_local_alloc:BOOL=ON"
  "-Denable_threads_discovery:BOOL=ON"
  "-Denable_cplusplus:BOOL=OFF"
  "-Denable_throw_bad_alloc_library:BOOL=OFF"
  "-Denable_gcj_support:BOOL=OFF"
  "-Denable_large_config:BOOL=ON"
  "-Denable_mmap:BOOL=ON"
  "-Denable_munmap:BOOL=OFF"
  "-Dinstall_headers:BOOL=ON")

if(CMAKE_C_COMPILER_AR)
  list(APPEND ATHENA_BDWGC_CMAKE_ARGS
    "-DCMAKE_AR:FILEPATH=${CMAKE_C_COMPILER_AR}")
endif()
if(CMAKE_C_COMPILER_RANLIB)
  list(APPEND ATHENA_BDWGC_CMAKE_ARGS
    "-DCMAKE_RANLIB:FILEPATH=${CMAKE_C_COMPILER_RANLIB}")
endif()

file(GLOB_RECURSE ATHENA_BDWGC_SOURCES CONFIGURE_DEPENDS
  "${ATHENA_BDWGC_SOURCE_DIR}/*.c"
  "${ATHENA_BDWGC_SOURCE_DIR}/*.h"
  "${ATHENA_BDWGC_SOURCE_DIR}/CMakeLists.txt")

ExternalProject_Add(athena_bdwgc_runtime
  SOURCE_DIR "${ATHENA_BDWGC_SOURCE_DIR}"
  BINARY_DIR "${ATHENA_BDWGC_BUILD_DIR}"
  INSTALL_DIR "${ATHENA_BDWGC_PREFIX}"
  DOWNLOAD_COMMAND ""
  UPDATE_COMMAND ""
  PATCH_COMMAND ""
  CMAKE_ARGS ${ATHENA_BDWGC_CMAKE_ARGS}
  BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR> --parallel 20
  INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
  BUILD_BYPRODUCTS "${ATHENA_BDWGC_LIBRARY}"
  USES_TERMINAL_BUILD TRUE
  USES_TERMINAL_INSTALL TRUE)

ExternalProject_Add_Step(athena_bdwgc_runtime toolchain_changes
  COMMAND "${CMAKE_COMMAND}" -E true
  DEPENDERS configure
  DEPENDS "${ATHENA_BDWGC_TOOLCHAIN_FINGERPRINT}"
  COMMENT "Checking the private ATHENA BDW-GC toolchain configuration")

ExternalProject_Add_Step(athena_bdwgc_runtime source_changes
  COMMAND "${CMAKE_COMMAND}" -E true
  DEPENDEES configure
  DEPENDERS build
  DEPENDS ${ATHENA_BDWGC_SOURCES}
  COMMENT "Checking the private ATHENA BDW-GC sources")
