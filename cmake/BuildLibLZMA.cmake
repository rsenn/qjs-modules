# build_liblzma(BINARY SUFFIX PIC)
#
# Downloads and builds liblzma (xz) as a static library into ${BINARY}/deps-${SUFFIX}, once
# per SUFFIX ("shared"/"static") with its own PIC setting. Sets LIBLZMA_INCLUDE_DIR_${SUFFIX}
# and LIBLZMA_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is liblzma_${SUFFIX}.
macro(build_liblzma BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building liblzma from source (${SUFFIX}, PIC=${PIC})")

  set(LIBLZMA_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LIBLZMA_INCLUDE_DIR_${SUFFIX} "${LIBLZMA_PREFIX_${SUFFIX}}/include")
  set(LIBLZMA_LIBRARY_FILE_${SUFFIX} "${LIBLZMA_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}lzma${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # WASI has no threads or signal masks: liblzma builds single-threaded
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(LIBLZMA_THREADS_ARG "-DXZ_THREADS:STRING=no")
  else()
    set(LIBLZMA_THREADS_ARG "")
  endif()

  ExternalProject_Add(
    liblzma_${SUFFIX}
    URL https://github.com/tukaani-project/xz/releases/download/v5.8.1/xz-5.8.1.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/liblzma-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${LIBLZMA_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DXZ_NLS:BOOL=OFF"
      "-DBUILD_TESTING:BOOL=OFF"
      ${LIBLZMA_THREADS_ARG}
      ${EMSCRIPTEN_MODULE_PATH_ARGS}
      "-DXZ_TOOL_XZ:BOOL=OFF"
      "-DXZ_TOOL_XZDEC:BOOL=OFF"
      "-DXZ_TOOL_LZMADEC:BOOL=OFF"
      "-DXZ_TOOL_LZMAINFO:BOOL=OFF"
      "-DXZ_TOOL_SCRIPTS:BOOL=OFF"
      "-DXZ_TOOL_SYMLINKS:BOOL=OFF"
    BUILD_BYPRODUCTS "${LIBLZMA_LIBRARY_FILE_${SUFFIX}}")
endmacro(build_liblzma)
