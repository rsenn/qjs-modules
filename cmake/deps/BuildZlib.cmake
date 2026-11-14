include(${CMAKE_CURRENT_LIST_DIR}/FindZlib.cmake)

# use_zlib(BINARY SUFFIX PIC)
#
# What every script that links zlib calls: the system zlib when find_zlib() found one, else
# build_zlib(), built once per SUFFIX however many scripts ask. Sets ZLIB_INCLUDE_DIR_${SUFFIX}
# (empty: the compiler's default), ZLIB_LIBRARIES_${SUFFIX} and ZLIB_DEPENDS_${SUFFIX}, the
# target to add to DEPENDS (empty for a system zlib). A built zlib also has
# ZLIB_LIBRARY_FILE_${SUFFIX}, the path of its archive.
#
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
  # zlib names the static library "z" only where cmake sets UNIX; WASI gets "zlibstatic"
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(ZLIB_LIBRARY_NAME zlibstatic)
  else()
    set(ZLIB_LIBRARY_NAME z)
  endif()
  set(ZLIB_LIBRARY_FILE_${SUFFIX}
      "${ZLIB_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${ZLIB_LIBRARY_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

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
      "-DZLIB_BUILD_EXAMPLES:BOOL=OFF"
    BUILD_BYPRODUCTS "${ZLIB_LIBRARY_FILE_${SUFFIX}}")
endmacro()

macro(use_zlib BINARY SUFFIX PIC)
  if(NOT DEFINED ZLIB_FOUND)
    find_zlib()
  endif()

  if(ZLIB_FOUND)
    set(ZLIB_INCLUDE_DIR_${SUFFIX} "${ZLIB_INCLUDE_DIR}")
    set(ZLIB_LIBRARIES_${SUFFIX} ${ZLIB_LIBRARIES})
    set(ZLIB_DEPENDS_${SUFFIX} "")
  else()
    if(NOT TARGET zlib_${SUFFIX})
      build_zlib(${BINARY} ${SUFFIX} ${PIC})
    endif()
    set(ZLIB_LIBRARIES_${SUFFIX} ${ZLIB_LIBRARY_FILE_${SUFFIX}})
    set(ZLIB_DEPENDS_${SUFFIX} zlib_${SUFFIX})
  endif()
endmacro()
