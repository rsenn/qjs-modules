# build_openssl(SOURCE BINARY SUFFIX PIC)
#
# Builds OpenSSL from its git repository (third_party/openssl, cloned on first use, shallow) as
# static libraries, once per SUFFIX ("shared"/"static") with its own PIC setting. Only the
# libraries and their headers are built: no apps, no tests, no docs, no engines, no modules.
# zlib compression support is compiled in (OPENSSL_ZLIB), against the zlib that find_zlib()
# found or, when there is none, one built by build_zlib() for the same SUFFIX (use_zlib()).
#
# Sets OPENSSL_ROOT_DIR_${SUFFIX}, OPENSSL_INCLUDE_DIR_${SUFFIX} and OPENSSL_LIBRARIES_${SUFFIX}
# (libssl, libcrypto, zlib, then the system libraries they need) and OPENSSL_LINK_FLAGS_${SUFFIX}
# (zlib and the system libraries as linker flags); the ExternalProject target is openssl_${SUFFIX}.
# Needs perl and make, as any OpenSSL source build does. Scripts call use_openssl() below.
include(${CMAKE_CURRENT_LIST_DIR}/../Deps.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindOpenSSL.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindZlib.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/BuildZlib.cmake)

set(OPENSSL_GIT_REPOSITORY "https://github.com/openssl/openssl.git" CACHE STRING "OpenSSL git repository")
set(OPENSSL_GIT_TAG "openssl-3.5.9" CACHE STRING "OpenSSL git tag to build")
option(OPENSSL_ZLIB "Build OpenSSL with zlib compression support" ON)

set(OPENSSL_CLONE_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/GitCloneOnce.cmake")

macro(build_openssl SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building openssl from git (${SUFFIX}, PIC=${PIC})")

  find_program(MAKE_PROGRAM NAMES gmake make)
  if(NOT MAKE_PROGRAM)
    message(FATAL_ERROR "building OpenSSL needs make")
  endif()

  set(OPENSSL_SOURCE_DIR "${SOURCE}/third_party/openssl")
  set(OPENSSL_ROOT_DIR_${SUFFIX} "${BINARY}/deps-openssl-${SUFFIX}")
  set(OPENSSL_INCLUDE_DIR_${SUFFIX} "${OPENSSL_ROOT_DIR_${SUFFIX}}/include")
  foreach(lib ssl crypto)
    set(OPENSSL_${lib}_FILE_${SUFFIX}
        "${OPENSSL_ROOT_DIR_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}${lib}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  endforeach(lib)

  # one clone for both variants, done at build time unless third_party/openssl has Configure
  if(NOT TARGET openssl_source)
    add_custom_target(
      openssl_source
      COMMAND ${CMAKE_COMMAND} -DDIR=${OPENSSL_SOURCE_DIR} -DMARKER=Configure -DREPO=${OPENSSL_GIT_REPOSITORY}
              -DTAG=${OPENSSL_GIT_TAG} -P ${OPENSSL_CLONE_SCRIPT}
      VERBATIM)
  endif()

  set(OPENSSL_DEPS_${SUFFIX} openssl_source)
  set(OPENSSL_CONFIGURE_ARGS_${SUFFIX} no-shared no-tests no-apps no-docs no-engine no-module no-legacy)
  set(OPENSSL_ZLIB_LIBRARIES_${SUFFIX} "")

  if(OPENSSL_ZLIB)
    use_zlib(${BINARY} ${SUFFIX} ${PIC})

    list(APPEND OPENSSL_CONFIGURE_ARGS_${SUFFIX} zlib)
    list(APPEND OPENSSL_DEPS_${SUFFIX} ${ZLIB_DEPENDS_${SUFFIX}})
    set(OPENSSL_ZLIB_LIBRARIES_${SUFFIX} ${ZLIB_LIBRARIES_${SUFFIX}})

    if(ZLIB_INCLUDE_DIR_${SUFFIX})
      list(APPEND OPENSSL_CONFIGURE_ARGS_${SUFFIX} --with-zlib-include=${ZLIB_INCLUDE_DIR_${SUFFIX}})
    endif()
    if(ZLIB_DEPENDS_${SUFFIX})
      list(APPEND OPENSSL_CONFIGURE_ARGS_${SUFFIX} --with-zlib-lib=${ZLIB_LIBRARY_FILE_${SUFFIX}})
    endif()
  endif()

  if(${PIC})
    list(APPEND OPENSSL_CONFIGURE_ARGS_${SUFFIX} -fPIC)
  endif()

  # config guesses the target from uname, so a MinGW cross build names it: Configure mingw64
  set(OPENSSL_CONFIGURE_${SUFFIX} config)
  set(OPENSSL_TARGET_ARGS_${SUFFIX} "")
  set(OPENSSL_CC_${SUFFIX} "${CMAKE_C_COMPILER}")
  if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
      set(OPENSSL_CONFIGURE_${SUFFIX} "Configure;mingw64")
    else()
      set(OPENSSL_CONFIGURE_${SUFFIX} "Configure;mingw")
    endif()
    if(CMAKE_C_COMPILER MATCHES "^(.*-)gcc(\\.exe)?$")
      get_filename_component(_OSSL_PFX "${CMAKE_MATCH_1}" NAME)
      set(OPENSSL_TARGET_ARGS_${SUFFIX} --cross-compile-prefix=${_OSSL_PFX})
      set(OPENSSL_CC_${SUFFIX} gcc) # the prefix is prepended to CC
    endif()
  endif()

  cmake_host_system_information(RESULT OPENSSL_JOBS QUERY NUMBER_OF_LOGICAL_CORES)

  ExternalProject_Add(
    openssl_${SUFFIX}
    DEPENDS ${OPENSSL_DEPS_${SUFFIX}}
    SOURCE_DIR ${OPENSSL_SOURCE_DIR}
    BINARY_DIR ${BINARY}/openssl-${SUFFIX}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env "CC=${OPENSSL_CC_${SUFFIX}}" <SOURCE_DIR>/${OPENSSL_CONFIGURE_${SUFFIX}}
      --prefix=${OPENSSL_ROOT_DIR_${SUFFIX}} --openssldir=${OPENSSL_ROOT_DIR_${SUFFIX}}/ssl --libdir=lib
      ${OPENSSL_TARGET_ARGS_${SUFFIX}} ${OPENSSL_CONFIGURE_ARGS_${SUFFIX}}
    BUILD_COMMAND ${MAKE_PROGRAM} -j${OPENSSL_JOBS} build_libs
    INSTALL_COMMAND ${MAKE_PROGRAM} install_dev
    BUILD_BYPRODUCTS "${OPENSSL_ssl_FILE_${SUFFIX}}" "${OPENSSL_crypto_FILE_${SUFFIX}}")

  add_library(OpenSSLBuild::SSL_${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(OpenSSLBuild::SSL_${SUFFIX} openssl_${SUFFIX})
  set_target_properties(OpenSSLBuild::SSL_${SUFFIX} PROPERTIES IMPORTED_LOCATION "${OPENSSL_ssl_FILE_${SUFFIX}}")

  add_library(OpenSSLBuild::Crypto_${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(OpenSSLBuild::Crypto_${SUFFIX} openssl_${SUFFIX})
  set_target_properties(OpenSSLBuild::Crypto_${SUFFIX} PROPERTIES IMPORTED_LOCATION "${OPENSSL_crypto_FILE_${SUFFIX}}")

  # the same libraries as plain linker flags, for a configure script that links against libcrypto.a
  set(OPENSSL_LINK_FLAGS_${SUFFIX} "")
  foreach(lib ${OPENSSL_ZLIB_LIBRARIES_${SUFFIX}} ${CMAKE_DL_LIBS} pthread)
    if(IS_ABSOLUTE "${lib}")
      string(APPEND OPENSSL_LINK_FLAGS_${SUFFIX} " ${lib}")
    else()
      string(APPEND OPENSSL_LINK_FLAGS_${SUFFIX} " -l${lib}")
    endif()
  endforeach(lib)

  # link order: libssl needs libcrypto, libcrypto needs zlib and the system libraries
  set(OPENSSL_LIBRARIES_${SUFFIX} OpenSSLBuild::SSL_${SUFFIX} OpenSSLBuild::Crypto_${SUFFIX}
                                  ${OPENSSL_ZLIB_LIBRARIES_${SUFFIX}} ${CMAKE_DL_LIBS} pthread)
endmacro()

# Registration with Deps.cmake: the installed OpenSSL, else build_openssl(). Built with zlib
# (OPENSSL_ZLIB), which is then resolved first.
function(_openssl_find name suffix)
  find_openssl()

  if(OPENSSL_FOUND)
    dep_provide(openssl ${suffix} SOURCE system ORIGIN "${OPENSSL_SSL_LIBRARY}" INCLUDE_DIRS ${OPENSSL_INCLUDE_DIR}
                LIBRARIES ${OPENSSL_LIBRARIES})
  endif()
endfunction()

function(_openssl_build name suffix pic binary source)
  build_openssl(${source} ${binary} ${suffix} ${pic})

  dep_provide(openssl ${suffix} SOURCE built ORIGIN "git ${OPENSSL_GIT_TAG}: ${OPENSSL_ROOT_DIR_${suffix}}"
              PREFIX "${OPENSSL_ROOT_DIR_${suffix}}" INCLUDE_DIRS "${OPENSSL_INCLUDE_DIR_${suffix}}"
              LIBRARIES ${OPENSSL_LIBRARIES_${suffix}} LINK_FLAGS "${OPENSSL_LINK_FLAGS_${suffix}}"
              TARGETS openssl_${suffix})
endfunction()

if(OPENSSL_ZLIB)
  set(OPENSSL_DEP_ZLIB zlib)
else()
  set(OPENSSL_DEP_ZLIB "")
endif()

dep_declare(openssl DESCRIPTION OpenSSL FIND _openssl_find BUILD _openssl_build DEPENDS ${OPENSSL_DEP_ZLIB})

# use_openssl(SOURCE BINARY SUFFIX PIC)
#
# Resolves OpenSSL for one SUFFIX through dep_resolve() and leaves OPENSSL_FOUND,
# OPENSSL_ROOT_DIR_${SUFFIX} (empty for an installed copy), OPENSSL_INCLUDE_DIR_${SUFFIX},
# OPENSSL_LIBRARIES_${SUFFIX}, OPENSSL_LINK_FLAGS_${SUFFIX} (built: zlib and the system
# libraries as linker flags) and OPENSSL_DEPENDS_${SUFFIX} (the target to add to DEPENDS).
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
