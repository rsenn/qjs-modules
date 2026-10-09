# build_zstd(BINARY SUFFIX PIC)
#
# Downloads and builds zstd as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets ZSTD_INCLUDE_DIR_${SUFFIX} and
# ZSTD_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is zstd_${SUFFIX}.
macro(build_zstd BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building zstd from source (${SUFFIX}, PIC=${PIC})")

  set(ZSTD_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(ZSTD_INCLUDE_DIR_${SUFFIX} "${ZSTD_PREFIX_${SUFFIX}}/include")
  set(ZSTD_LIBRARY_FILE_${SUFFIX} "${ZSTD_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}zstd${CMAKE_STATIC_LIBRARY_SUFFIX}")

  ExternalProject_Add(
    zstd_${SUFFIX}
    URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR build/cmake
    BINARY_DIR ${BINARY}/zstd-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${ZSTD_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DZSTD_BUILD_STATIC:BOOL=ON"
      "-DZSTD_BUILD_SHARED:BOOL=OFF"
      "-DZSTD_BUILD_PROGRAMS:BOOL=OFF"
      "-DZSTD_BUILD_TESTS:BOOL=OFF"
      "-DZSTD_BUILD_CONTRIB:BOOL=OFF"
    BUILD_BYPRODUCTS "${ZSTD_LIBRARY_FILE_${SUFFIX}}")
endmacro()
