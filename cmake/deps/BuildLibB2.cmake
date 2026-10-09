# build_libb2(BINARY SUFFIX PIC)
#
# Downloads and builds libb2 as a static library into ${BINARY}/deps-${SUFFIX}, once per SUFFIX
# ("shared"/"static") with its own PIC setting. Sets LIBB2_INCLUDE_DIR_${SUFFIX} and
# LIBB2_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is libb2_${SUFFIX}.
#
# libb2 has only an autotools build: cmake/libb2.CMakeLists.txt is copied over the extracted tree's
# CMakeLists.txt, and the project is then configured like zlib and the others (toolchain file, compiler,
# sysroot and PIC through CMAKE_CACHE_ARGS, so a cross toolchain such as Emscripten supplies its own
# ar/ranlib instead of a bare "emar" being looked up relative to the build directory).
set(LIBB2_CMAKELISTS "${CMAKE_CURRENT_LIST_DIR}/libb2.CMakeLists.txt")

macro(build_libb2 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building libb2 from source (${SUFFIX}, PIC=${PIC})")

  set(LIBB2_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LIBB2_INCLUDE_DIR_${SUFFIX} "${LIBB2_PREFIX_${SUFFIX}}/include")
  set(LIBB2_LIBRARY_FILE_${SUFFIX}
      "${LIBB2_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}b2${CMAKE_STATIC_LIBRARY_SUFFIX}")

  ExternalProject_Add(
    libb2_${SUFFIX}
    URL https://github.com/BLAKE2/libb2/releases/download/v0.98.1/libb2-0.98.1.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/libb2-${SUFFIX}
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy "${LIBB2_CMAKELISTS}" <SOURCE_DIR>/CMakeLists.txt
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${LIBB2_PREFIX_${SUFFIX}}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    BUILD_BYPRODUCTS "${LIBB2_LIBRARY_FILE_${SUFFIX}}")
endmacro()
