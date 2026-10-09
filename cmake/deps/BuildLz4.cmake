# build_liblz4(BINARY SUFFIX PIC)
#
# Downloads and builds lz4 as a static library into ${BINARY}/deps-${SUFFIX}, once per SUFFIX
# ("shared"/"static") with its own PIC setting. Sets LZ4_INCLUDE_DIR_${SUFFIX} and
# LZ4_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is lz4_${SUFFIX}.
macro(build_liblz4 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building lz4 from source (${SUFFIX}, PIC=${PIC})")

  set(LZ4_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LZ4_INCLUDE_DIR_${SUFFIX} "${LZ4_PREFIX_${SUFFIX}}/include")
  set(LZ4_LIBRARY_FILE_${SUFFIX} "${LZ4_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}lz4${CMAKE_STATIC_LIBRARY_SUFFIX}")

  ExternalProject_Add(
    lz4_${SUFFIX}
    URL https://github.com/lz4/lz4/releases/download/v1.10.0/lz4-1.10.0.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR build/cmake
    BINARY_DIR ${BINARY}/lz4-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${LZ4_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DLZ4_BUILD_CLI:BOOL=OFF"
      "-DLZ4_BUILD_LEGACY_LZ4C:BOOL=OFF"
    BUILD_BYPRODUCTS "${LZ4_LIBRARY_FILE_${SUFFIX}}")
endmacro()
