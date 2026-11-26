include(${CMAKE_CURRENT_LIST_DIR}/FindZlib.cmake)

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

  # dietlibc cannot link a shared object: build and install the static library only
  set(ZLIB_STATIC_ONLY_ARGS "")
  if(DIET OR HAVE_DIETLIBC)
    set(ZLIB_STATIC_ONLY_ARGS
        BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target zlibstatic
        INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${ZLIB_PREFIX_${SUFFIX}}/include ${ZLIB_PREFIX_${SUFFIX}}/lib
        COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/zlib.h <BINARY_DIR>/zconf.h ${ZLIB_PREFIX_${SUFFIX}}/include
        COMMAND ${CMAKE_COMMAND} -E copy <BINARY_DIR>/${CMAKE_STATIC_LIBRARY_PREFIX}${ZLIB_LIBRARY_NAME}${CMAKE_STATIC_LIBRARY_SUFFIX}
                ${ZLIB_PREFIX_${SUFFIX}}/lib)
  endif()

  toolchain_cache_args(_DEPS_CROSS_ARGS)

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
      ${_DEPS_CROSS_ARGS}
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DZLIB_BUILD_EXAMPLES:BOOL=OFF"
    ${ZLIB_STATIC_ONLY_ARGS}
    BUILD_BYPRODUCTS "${ZLIB_LIBRARY_FILE_${SUFFIX}}")
endmacro()

# Registration with Deps.cmake: the installed zlib (-DZLIB_FOUND=FALSE ignores it), else build_zlib().
include(${CMAKE_CURRENT_LIST_DIR}/../Deps.cmake)

function(_zlib_find name suffix)
  if(DEFINED ZLIB_FOUND AND NOT ZLIB_FOUND)
    return()
  endif()

  find_zlib()

  if(ZLIB_FOUND)
    find_library(_zlib_library NAMES ${ZLIB_LIBRARY})
    if(NOT _zlib_library)
      set(_zlib_library "-l${ZLIB_LIBRARY}")
    endif()
    dep_provide(zlib ${suffix} SOURCE system ORIGIN "${_zlib_library}" INCLUDE_DIRS ${ZLIB_INCLUDE_DIR}
                LIBRARIES "${_zlib_library}")
  endif()
endfunction()

function(_zlib_build name suffix pic binary source)
  build_zlib(${binary} ${suffix} ${pic})

  dep_provide(zlib ${suffix} SOURCE built ORIGIN "${ZLIB_LIBRARY_FILE_${suffix}}" PREFIX "${binary}/deps-${suffix}"
              INCLUDE_DIRS "${ZLIB_INCLUDE_DIR_${suffix}}" LIBRARIES "${ZLIB_LIBRARY_FILE_${suffix}}"
              TARGETS zlib_${suffix})
endfunction()

dep_declare(zlib DESCRIPTION zlib FIND _zlib_find BUILD _zlib_build)

# use_zlib(BINARY SUFFIX PIC)
#
# Resolves zlib for one SUFFIX through dep_resolve() and leaves the variables the scripts
# that link zlib read: ZLIB_FOUND (an installed copy), ZLIB_INCLUDE_DIR_${SUFFIX} (empty:
# the compiler's default), ZLIB_LIBRARIES_${SUFFIX}, ZLIB_DEPENDS_${SUFFIX} (the target to
# add to DEPENDS, empty for an installed copy) and, for a built one, ZLIB_LIBRARY_FILE_${SUFFIX}.
macro(use_zlib BINARY SUFFIX PIC)
  dep_resolve(zlib SUFFIX ${SUFFIX} PIC ${PIC} BINARY ${BINARY} SOURCE ${CMAKE_CURRENT_SOURCE_DIR})

  dep_get(zlib ${SUFFIX} SOURCE _UZ_SOURCE)
  dep_get(zlib ${SUFFIX} INCLUDE_DIRS ZLIB_INCLUDE_DIR_${SUFFIX})
  dep_get(zlib ${SUFFIX} LIBRARIES ZLIB_LIBRARIES_${SUFFIX})

  if("${_UZ_SOURCE}" STREQUAL "system")
    set(ZLIB_FOUND TRUE)
    set(ZLIB_LIBRARIES ${ZLIB_LIBRARIES_${SUFFIX}})
    set(ZLIB_DEPENDS_${SUFFIX} "")
  else()
    set(ZLIB_FOUND FALSE)
    set(ZLIB_LIBRARY_FILE_${SUFFIX} ${ZLIB_LIBRARIES_${SUFFIX}})
    set(ZLIB_DEPENDS_${SUFFIX} zlib_${SUFFIX})
  endif()
endmacro()
