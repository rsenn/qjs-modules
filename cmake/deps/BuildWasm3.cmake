# build_wasm3(BINARY SUFFIX PIC)
#
# Builds third_party/wasm3 as a static library into ${BINARY}/deps-wasm3-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets WASM3_LIBRARIES_${SUFFIX}; the
# ExternalProject target is wasm3_${SUFFIX}. Headers are in third_party/wasm3/source.
macro(build_wasm3 BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building wasm3 from third_party (${SUFFIX}, PIC=${PIC})")

  set(WASM3_BINARY_DIR_${SUFFIX} "${BINARY}/deps-wasm3-${SUFFIX}")
  set(WASM3_LIBRARY_FILE_${SUFFIX}
      "${WASM3_BINARY_DIR_${SUFFIX}}/source/${CMAKE_STATIC_LIBRARY_PREFIX}m3${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # dietlibc has no float math (copysignf, ceilf, ...) and no pthread_getattr_np
  set(WASM3_C_FLAGS "-w")
  set(WASM3_STD_LIBS "")
  if(DIET OR HAVE_DIETLIBC)
    set(WASM3_STD_LIBS "-lm")
    set(WASM3_C_FLAGS "-w -fno-builtin -Dd_m3HasThreadStackProbe=0 -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/diet-compat.h")
  endif()

  toolchain_cache_args(_WASM3_CROSS)

  ExternalProject_Add(
    wasm3_${SUFFIX}
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3
    BINARY_DIR ${WASM3_BINARY_DIR_${SUFFIX}}
    CMAKE_CACHE_ARGS
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}" ${_WASM3_CROSS} "-DCMAKE_C_FLAGS:STRING=${WASM3_C_FLAGS}" "-DCMAKE_BUILD_TYPE:STRING=Release"
      "-DCMAKE_C_STANDARD_LIBRARIES:STRING=${WASM3_STD_LIBS}" "-DBUILD_WASI:STRING=none" "-DBUILD_NATIVE:BOOL=OFF" "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    INSTALL_COMMAND ""
    BUILD_ALWAYS ON # a changed third_party/wasm3 source must rebuild the library
    BUILD_BYPRODUCTS "${WASM3_LIBRARY_FILE_${SUFFIX}}")

  add_library(Wasm3::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(Wasm3::${SUFFIX} wasm3_${SUFFIX})
  set_target_properties(Wasm3::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${WASM3_LIBRARY_FILE_${SUFFIX}}")
  set(WASM3_LIBRARIES_${SUFFIX} Wasm3::${SUFFIX})
endmacro()
