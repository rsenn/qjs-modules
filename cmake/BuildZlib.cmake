# build_zlib(BINARY SUFFIX PIC)
#
# Downloads and builds zlib as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets ZLIB_INCLUDE_DIR_${SUFFIX} and
# ZLIB_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is zlib_${SUFFIX}.
macro(build_zlib BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building zlib from source (${SUFFIX}, PIC=${PIC})")

  set(ZLIB_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(ZLIB_INCLUDE_DIR_${SUFFIX} "${ZLIB_PREFIX_${SUFFIX}}/include")
  set(ZLIB_LIBRARY_FILE_${SUFFIX} "${ZLIB_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}z${CMAKE_STATIC_LIBRARY_SUFFIX}")

  ExternalProject_Add(
    zlib_${SUFFIX}
    URL https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/zlib-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${ZLIB_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    BUILD_BYPRODUCTS "${ZLIB_LIBRARY_FILE_${SUFFIX}}")
endmacro(build_zlib)
