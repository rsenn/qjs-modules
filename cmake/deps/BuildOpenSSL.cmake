# build_openssl(BINARY SUFFIX PIC)
#
# Builds OpenSSL from its newest release tarball (OpenSSL has no CMake build: its own perl Configure and make,
# in the source tree) as static libraries, once per SUFFIX ("shared"/"static") with its own PIC setting.
# Same contract as build_libressl() in BuildLibreSSL.cmake, which picks one of the two through TLS_LIBRARY.
#
# Sets OPENSSL_ROOT_DIR_${SUFFIX}, OPENSSL_INCLUDE_DIR_${SUFFIX}, OPENSSL_LIBRARIES_${SUFFIX} and
# OPENSSL_LINK_FLAGS_${SUFFIX}; the ExternalProject target is openssl_${SUFFIX}.
set(OPENSSL_VERSION "4.1.0" CACHE STRING "OpenSSL release to build")
set(OPENSSL_URL "https://github.com/openssl/openssl/releases/download/openssl-${OPENSSL_VERSION}/openssl-${OPENSSL_VERSION}.tar.gz"
    CACHE STRING "OpenSSL release tarball (the newest release, https://github.com/openssl/openssl/releases)")

macro(build_openssl BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building openssl ${OPENSSL_VERSION} (${SUFFIX}, PIC=${PIC})")

  set(OPENSSL_ROOT_DIR_${SUFFIX} "${BINARY}/deps-openssl-${SUFFIX}")
  set(OPENSSL_INCLUDE_DIR_${SUFFIX} "${OPENSSL_ROOT_DIR_${SUFFIX}}/include")
  foreach(lib ssl crypto)
    set(OPENSSL_${lib}_FILE_${SUFFIX}
        "${OPENSSL_ROOT_DIR_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  endforeach(lib)

  include(ProcessorCount)
  ProcessorCount(_OSSL_JOBS)
  if(NOT _OSSL_JOBS)
    set(_OSSL_JOBS 1)
  endif()

  set(_OSSL_FLAGS no-shared no-tests no-docs no-apps no-dso)
  if(${PIC})
    list(APPEND _OSSL_FLAGS -fPIC)
  endif()
  if(DIET OR HAVE_DIETLIBC)
    # dietlibc has getrandom() but no syscall(): seed from /dev/urandom
    list(APPEND _OSSL_FLAGS --with-rand-seed=devrandom no-threads no-secure-memory no-async no-ml-kem no-ml-dsa no-slh-dsa
         "-include" "${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/diet-compat.h")
  endif()

  toolchain_cc(_SSL_CC)

  ExternalProject_Add(
    openssl_${SUFFIX}
    URL ${OPENSSL_URL}
    # one directory per suffix: the shared and static builds download in parallel
    DOWNLOAD_DIR ${BINARY}/downloads-openssl-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ${CMAKE_COMMAND} -E env "CC=${_SSL_CC}" <SOURCE_DIR>/Configure
      --prefix=${OPENSSL_ROOT_DIR_${SUFFIX}} --libdir=lib ${_OSSL_FLAGS}
    # --trace prints every command and -j runs them in parallel: OpenSSL's Makefile is silent and serial
    # MAKEFLAGS= : the outer CMake makefile runs make -s, which a nested make inherits and stays silent
    BUILD_COMMAND ${DEPS_MAKE} -j${_OSSL_JOBS} --trace build_libs
    USES_TERMINAL_CONFIGURE TRUE
    USES_TERMINAL_BUILD TRUE
    INSTALL_COMMAND make install_dev
    BUILD_BYPRODUCTS "${OPENSSL_ssl_FILE_${SUFFIX}}" "${OPENSSL_crypto_FILE_${SUFFIX}}")

  set(OPENSSL_SYSTEM_LIBS_${SUFFIX})
  if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    list(APPEND OPENSSL_SYSTEM_LIBS_${SUFFIX} ws2_32 crypt32 bcrypt)
  elseif(NOT DIET AND NOT HAVE_DIETLIBC)
    list(APPEND OPENSSL_SYSTEM_LIBS_${SUFFIX} ${CMAKE_DL_LIBS})
    if(NOT ANDROID)
      list(APPEND OPENSSL_SYSTEM_LIBS_${SUFFIX} pthread)
    endif()
  endif()

  set(OPENSSL_LINK_FLAGS_${SUFFIX} "")
  foreach(lib ${OPENSSL_SYSTEM_LIBS_${SUFFIX}})
    string(APPEND OPENSSL_LINK_FLAGS_${SUFFIX} " -l${lib}")
  endforeach(lib)

  set(OPENSSL_LIBRARIES_${SUFFIX} "${OPENSSL_ssl_FILE_${SUFFIX}}" "${OPENSSL_crypto_FILE_${SUFFIX}}"
                                  ${OPENSSL_SYSTEM_LIBS_${SUFFIX}})
endmacro()
