# build_libressl(BINARY SUFFIX PIC)
#
# Builds LibreSSL (native CMake build) from its release tarball as static libraries, once per
# SUFFIX ("shared"/"static") with its own PIC setting. It is reported under the dependency name
# openssl: the headers are <openssl/...>, the libraries libssl and libcrypto, as with OpenSSL.
# OpenSSL itself is never built here: an installed OpenSSL or LibreSSL is found through
# FindOpenSSL.cmake, and only when there is none is LibreSSL built. Scripts call use_openssl().
#
# Sets OPENSSL_ROOT_DIR_${SUFFIX}, OPENSSL_INCLUDE_DIR_${SUFFIX}, OPENSSL_LIBRARIES_${SUFFIX} and
# OPENSSL_LINK_FLAGS_${SUFFIX}; the ExternalProject target is openssl_${SUFFIX}.
include(${CMAKE_CURRENT_LIST_DIR}/../Deps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindOpenSSL.cmake)

set(LIBRESSL_VERSION "4.3.3" CACHE STRING "LibreSSL release to build")
set(LIBRESSL_URL "https://ftp.openbsd.org/pub/OpenBSD/LibreSSL/libressl-${LIBRESSL_VERSION}.tar.gz" CACHE STRING
    "LibreSSL release tarball")

macro(build_libressl BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building libressl ${LIBRESSL_VERSION} (${SUFFIX}, PIC=${PIC})")

  set(OPENSSL_ROOT_DIR_${SUFFIX} "${BINARY}/deps-openssl-${SUFFIX}")
  set(OPENSSL_INCLUDE_DIR_${SUFFIX} "${OPENSSL_ROOT_DIR_${SUFFIX}}/include")
  foreach(lib ssl crypto)
    set(OPENSSL_${lib}_FILE_${SUFFIX}
        "${OPENSSL_ROOT_DIR_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  endforeach(lib)

  # LibreSSL picks its assembly and crypto_arch.h from the processor; a toolchain file often
  # leaves it unset, so it comes from the compiler's triplet (x86_64-w64-mingw32 -> x86_64)
  set(_LSSL_PROC "${CMAKE_SYSTEM_PROCESSOR}")
  if(NOT _LSSL_PROC)
    toolchain_triplet(_LSSL_TRIPLET)
    string(REGEX REPLACE "-.*" "" _LSSL_PROC "${_LSSL_TRIPLET}")
  endif()

  ExternalProject_Add(
    openssl_${SUFFIX}
    URL ${LIBRESSL_URL}
    DOWNLOAD_DIR ${BINARY}/downloads-openssl
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BINARY_DIR ${BINARY}/openssl-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${OPENSSL_ROOT_DIR_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSTEM_NAME:STRING=${CMAKE_SYSTEM_NAME}"
      "-DCMAKE_SYSTEM_PROCESSOR:STRING=${_LSSL_PROC}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DBUILD_SHARED_LIBS:BOOL=OFF"
      "-DLIBRESSL_APPS:BOOL=OFF"
      "-DLIBRESSL_TESTS:BOOL=OFF"
    BUILD_BYPRODUCTS "${OPENSSL_ssl_FILE_${SUFFIX}}" "${OPENSSL_crypto_FILE_${SUFFIX}}")

  # what libcrypto needs from the system (LibreSSL's libcompat is inside it)
  set(OPENSSL_SYSTEM_LIBS_${SUFFIX} ${CMAKE_DL_LIBS})
  if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND OPENSSL_SYSTEM_LIBS_${SUFFIX} ws2_32 bcrypt)
  else()
    list(APPEND OPENSSL_SYSTEM_LIBS_${SUFFIX} pthread)
  endif()

  set(OPENSSL_LINK_FLAGS_${SUFFIX} "")
  foreach(lib ${OPENSSL_SYSTEM_LIBS_${SUFFIX}})
    string(APPEND OPENSSL_LINK_FLAGS_${SUFFIX} " -l${lib}")
  endforeach(lib)

  # link order: libssl needs libcrypto, libcrypto needs the system libraries
  set(OPENSSL_LIBRARIES_${SUFFIX} "${OPENSSL_ssl_FILE_${SUFFIX}}" "${OPENSSL_crypto_FILE_${SUFFIX}}"
                                  ${OPENSSL_SYSTEM_LIBS_${SUFFIX}})
endmacro()

# Registration with Deps.cmake: the installed OpenSSL/LibreSSL, else build_libressl().
function(_openssl_find name suffix)
  find_openssl()

  if(OPENSSL_FOUND)
    dep_provide(openssl ${suffix} SOURCE system ORIGIN "${OPENSSL_SSL_LIBRARY}" INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR}
                LIBRARIES ${OPENSSL_LIBRARIES})
  endif()
endfunction()

function(_openssl_build name suffix pic binary source)
  build_libressl(${binary} ${suffix} ${pic})

  dep_provide(openssl ${suffix} SOURCE built ORIGIN "libressl ${LIBRESSL_VERSION}: ${OPENSSL_ROOT_DIR_${suffix}}"
              PREFIX "${OPENSSL_ROOT_DIR_${suffix}}" INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR_${suffix}}"
              LIBRARIES ${OPENSSL_LIBRARIES_${suffix}} LINK_FLAGS "${OPENSSL_LINK_FLAGS_${suffix}}"
              TARGETS openssl_${suffix})
endfunction()

dep_declare(openssl DESCRIPTION OpenSSL FIND _openssl_find BUILD _openssl_build)

# use_openssl(SOURCE BINARY SUFFIX PIC)
#
# Resolves OpenSSL for one SUFFIX through dep_resolve() and leaves OPENSSL_FOUND,
# OPENSSL_ROOT_DIR_${SUFFIX} (empty for an installed copy), OPENSSL_INCLUDE_DIR_${SUFFIX},
# OPENSSL_LIBRARIES_${SUFFIX}, OPENSSL_LINK_FLAGS_${SUFFIX} (built: the system libraries as
# linker flags) and OPENSSL_DEPENDS_${SUFFIX} (the target to add to DEPENDS).
macro(use_openssl SOURCE BINARY SUFFIX PIC)
  dep_resolve(openssl SUFFIX ${SUFFIX} PIC ${PIC} BINARY ${BINARY} SOURCE ${SOURCE})

  dep_get(openssl ${SUFFIX} SOURCE _UO_SOURCE)
  dep_get(openssl ${SUFFIX} PREFIX OPENSSL_ROOT_DIR_${SUFFIX})
  dep_get(openssl ${SUFFIX} INCLUDE_DIRS OPENSSL_INCLUDE_DIR_${SUFFIX})
  dep_get(openssl ${SUFFIX} LIBRARIES OPENSSL_LIBRARIES_${SUFFIX})
  dep_get(openssl ${SUFFIX} LINK_FLAGS OPENSSL_LINK_FLAGS_${SUFFIX})

  if("${_UO_SOURCE}" STREQUAL "built")
    set(OPENSSL_DEPENDS_${SUFFIX} openssl_${SUFFIX})
  else()
    set(OPENSSL_DEPENDS_${SUFFIX} "")
  endif()
endmacro()
