# build_lzo2(BINARY SUFFIX PIC)
#
# Downloads and builds lzo2 as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets LZO2_INCLUDE_DIR_${SUFFIX} and
# LZO2_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is lzo2_${SUFFIX}.
macro(build_lzo2 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building lzo2 from source (${SUFFIX}, PIC=${PIC})")

  set(LZO2_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LZO2_INCLUDE_DIR_${SUFFIX} "${LZO2_PREFIX_${SUFFIX}}/include")
  set(LZO2_LIBRARY_FILE_${SUFFIX} "${LZO2_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}lzo2${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # lzo's test programs call clock(), which WASI only emulates
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(LZO2_WASI_ARGS "-DCMAKE_C_FLAGS:STRING=-w -D_WASI_EMULATED_PROCESS_CLOCKS"
                       "-DCMAKE_EXE_LINKER_FLAGS:STRING=-lwasi-emulated-process-clocks")
  else()
    set(LZO2_WASI_ARGS "-DCMAKE_C_FLAGS:STRING=-w")
  endif()

  ExternalProject_Add(
    lzo2_${SUFFIX}
    URL https://www.oberhumer.com/opensource/lzo/download/lzo-2.10.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/lzo2-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${LZO2_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      ${LZO2_WASI_ARGS}
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DENABLE_STATIC:BOOL=ON"
      "-DENABLE_SHARED:BOOL=OFF"
      ${EMSCRIPTEN_MODULE_PATH_ARGS}
    BUILD_BYPRODUCTS "${LZO2_LIBRARY_FILE_${SUFFIX}}")
endmacro(build_lzo2)
