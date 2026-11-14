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
# (zlib and the system libraries as linker flags); the ExternalProject target is openssl_${SUFFIX}. Needs perl and make, as any OpenSSL source build does.
#
# use_openssl(SOURCE BINARY SUFFIX PIC)
#
# What the database drivers call: the system OpenSSL when find_openssl() found one and
# BUILD_OPENSSL is OFF, else build_openssl(). Sets the same three variables plus
# OPENSSL_DEPENDS_${SUFFIX}, the target to add to DEPENDS (empty for a system OpenSSL).
include(${CMAKE_CURRENT_LIST_DIR}/FindOpenSSL.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/FindZlib.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/BuildZlib.cmake)

set(OPENSSL_GIT_REPOSITORY "https://github.com/openssl/openssl.git" CACHE STRING "OpenSSL git repository")
set(OPENSSL_GIT_TAG "openssl-3.5.9" CACHE STRING "OpenSSL git tag to build")
option(BUILD_OPENSSL "Build OpenSSL from git into third_party/openssl instead of using the system one" OFF)
option(OPENSSL_ZLIB "Build OpenSSL with zlib compression support" ON)

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

  # one clone for both variants, skipped when third_party/openssl is already checked out
  if(NOT TARGET openssl_source)
    if(EXISTS "${OPENSSL_SOURCE_DIR}/Configure")
      add_custom_target(openssl_source)
    else()
      # CMP0097 NEW makes the empty GIT_SUBMODULES below mean "no submodules"
      cmake_policy(PUSH)
      if(POLICY CMP0097)
        cmake_policy(SET CMP0097 NEW)
      endif()
      ExternalProject_Add(
        openssl_source
        GIT_REPOSITORY ${OPENSSL_GIT_REPOSITORY}
        GIT_TAG ${OPENSSL_GIT_TAG}
        GIT_SHALLOW TRUE
        GIT_SUBMODULES "" # the submodules only hold test suites
        SOURCE_DIR ${OPENSSL_SOURCE_DIR}
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
      cmake_policy(POP)
    endif()
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

  cmake_host_system_information(RESULT OPENSSL_JOBS QUERY NUMBER_OF_LOGICAL_CORES)

  ExternalProject_Add(
    openssl_${SUFFIX}
    DEPENDS ${OPENSSL_DEPS_${SUFFIX}}
    SOURCE_DIR ${OPENSSL_SOURCE_DIR}
    BINARY_DIR ${BINARY}/openssl-${SUFFIX}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CONFIGURE_COMMAND
      ${CMAKE_COMMAND} -E env "CC=${CMAKE_C_COMPILER}" <SOURCE_DIR>/config --prefix=${OPENSSL_ROOT_DIR_${SUFFIX}}
      --openssldir=${OPENSSL_ROOT_DIR_${SUFFIX}}/ssl --libdir=lib ${OPENSSL_CONFIGURE_ARGS_${SUFFIX}}
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

macro(use_openssl SOURCE BINARY SUFFIX PIC)
  if(NOT DEFINED OPENSSL_FOUND)
    find_openssl()
  endif()

  if(BUILD_OPENSSL OR NOT OPENSSL_FOUND)
    # several drivers share one build per SUFFIX
    if(NOT TARGET openssl_${SUFFIX})
      build_openssl(${SOURCE} ${BINARY} ${SUFFIX} ${PIC})
    endif()
    set(OPENSSL_DEPENDS_${SUFFIX} openssl_${SUFFIX})
  else()
    set(OPENSSL_ROOT_DIR_${SUFFIX} "")
    set(OPENSSL_INCLUDE_DIR_${SUFFIX} ${OPENSSL_INCLUDE_DIR})
    set(OPENSSL_LIBRARIES_${SUFFIX} ${OPENSSL_LIBRARIES})
    set(OPENSSL_DEPENDS_${SUFFIX} "")
  endif()
endmacro()
