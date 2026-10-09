# build_bzip2(BINARY SUFFIX PIC)
#
# Downloads and builds bzip2 as a static library into ${BINARY}/deps-${SUFFIX}, once per SUFFIX
# ("shared"/"static") with its own PIC setting. Sets BZIP2_INCLUDE_DIR_${SUFFIX} and
# BZIP2_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is bzip2_${SUFFIX}.
#
# bzip2 has no CMake build of its own: cmake/bzip2.CMakeLists.txt is copied over the extracted tree's
# CMakeLists.txt, and the project is then configured like zlib and the others (toolchain file, compiler,
# sysroot and PIC through CMAKE_CACHE_ARGS, so a cross toolchain such as Emscripten supplies its own
# ar/ranlib instead of a bare "emar" being looked up relative to the build directory).
set(BZIP2_CMAKELISTS "${CMAKE_CURRENT_LIST_DIR}/bzip2.CMakeLists.txt")

macro(build_bzip2 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building bzip2 from source (${SUFFIX}, PIC=${PIC})")

  set(BZIP2_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(BZIP2_INCLUDE_DIR_${SUFFIX} "${BZIP2_PREFIX_${SUFFIX}}/include")
  set(BZIP2_LIBRARY_FILE_${SUFFIX}
      "${BZIP2_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}bz2${CMAKE_STATIC_LIBRARY_SUFFIX}")

  ExternalProject_Add(
    bzip2_${SUFFIX}
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/bzip2-${SUFFIX}
    PATCH_COMMAND ${CMAKE_COMMAND} -E copy "${BZIP2_CMAKELISTS}" <SOURCE_DIR>/CMakeLists.txt
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${BZIP2_PREFIX_${SUFFIX}}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    BUILD_BYPRODUCTS "${BZIP2_LIBRARY_FILE_${SUFFIX}}")
endmacro()
