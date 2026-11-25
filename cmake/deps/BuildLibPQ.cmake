# build_libpq(SOURCE BINARY SUFFIX PIC)
#
# Builds the PostgreSQL client library from the PostgreSQL git repository (third_party/postgres,
# cloned on first use, shallow) as a static library, once per SUFFIX ("shared"/"static") with
# its own PIC setting. Only what libpq needs is compiled: the generated headers, src/port,
# src/common and src/interfaces/libpq; no server, no psql, no readline, no zlib, no ICU.
#
# Sets LIBPQ_INCLUDE_DIR_${SUFFIX} and LIBPQ_LIBRARIES_${SUFFIX} (libpq, libpgcommon_shlib, libpgport_shlib,
# then the system libraries they need); the ExternalProject target is libpq_${SUFFIX}.
# Needs the autoconf build prerequisites of a PostgreSQL checkout: make, bison, flex, perl.
# Used when BUILD_LIBPQ is ON. TLS (LIBPQ_SSL, default ON) comes from use_openssl() in
# BuildLibreSSL.cmake: the system OpenSSL/LibreSSL, or a LibreSSL built from its release tarball.
set(LIBPQ_GIT_REPOSITORY "https://github.com/postgres/postgres.git" CACHE STRING "PostgreSQL git repository")
set(LIBPQ_GIT_TAG "REL_17_12" CACHE STRING "PostgreSQL git tag to build")
option(LIBPQ_SSL "Build libpq with TLS support (OpenSSL)" ON)
include(${CMAKE_CURRENT_LIST_DIR}/BuildLibreSSL.cmake)

macro(build_libpq SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building libpq from git (${SUFFIX}, PIC=${PIC})")

  find_program(MAKE_PROGRAM NAMES gmake make)
  if(NOT MAKE_PROGRAM)
    message(FATAL_ERROR "building libpq needs make")
  endif()

  set(LIBPQ_SOURCE_DIR "${SOURCE}/third_party/postgres")
  set(LIBPQ_PREFIX_${SUFFIX} "${BINARY}/deps-libpq-${SUFFIX}")
  set(LIBPQ_INCLUDE_DIR_${SUFFIX} "${LIBPQ_PREFIX_${SUFFIX}}/include")
  set(LIBPQ_LIBRARY_FILE_${SUFFIX}
      "${LIBPQ_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pq${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # one clone for both variants, skipped when third_party/postgres is already checked out
  if(NOT TARGET libpq_source)
    if(EXISTS "${LIBPQ_SOURCE_DIR}/configure")
      add_custom_target(libpq_source)
    else()
      ExternalProject_Add(
        libpq_source
        GIT_REPOSITORY ${LIBPQ_GIT_REPOSITORY}
        GIT_TAG ${LIBPQ_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${LIBPQ_SOURCE_DIR}
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
    endif()
  endif()

  set(LIBPQ_CFLAGS_${SUFFIX} "-O2 -w")
  if(DIET OR HAVE_DIETLIBC)
    # gcc's cpuid.h has a parameter named like dietlibc's __leaf macro, u_char and u_int need _BSD_SOURCE
    string(APPEND LIBPQ_CFLAGS_${SUFFIX} " -D_GNU_SOURCE -D_BSD_SOURCE -I${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/diet")
  endif()
  if(${PIC})
    string(APPEND LIBPQ_CFLAGS_${SUFFIX} " -fPIC")
  endif()

  # cross compiling: configure must be told the target triplet or it tries to run the
  # compiled test programs
  set(LIBPQ_HOST_FLAG_${SUFFIX})
  if(CMAKE_CROSSCOMPILING)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE LIBPQ_TRIPLET
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(LIBPQ_TRIPLET)
      set(LIBPQ_HOST_FLAG_${SUFFIX} --host=${LIBPQ_TRIPLET})
    endif()
  endif()

  set(LIBPQ_SYSTEM_LIBS_${SUFFIX} pthread m)

  set(LIBPQ_DEPS_${SUFFIX} libpq_source)

  if(LIBPQ_SSL)
    use_openssl(${SOURCE} ${BINARY} ${SUFFIX} ${PIC})
    set(LIBPQ_SSL_FLAG_${SUFFIX} --with-ssl=openssl)
    list(APPEND LIBPQ_SYSTEM_LIBS_${SUFFIX} ${OPENSSL_LIBRARIES_${SUFFIX}})
    list(APPEND LIBPQ_DEPS_${SUFFIX} ${OPENSSL_DEPENDS_${SUFFIX}})

    # a built OpenSSL is a static prefix configure has to be pointed at
    if(OPENSSL_ROOT_DIR_${SUFFIX})
      list(APPEND LIBPQ_SSL_FLAG_${SUFFIX} --with-includes=${OPENSSL_INCLUDE_DIR_${SUFFIX}}
           --with-libraries=${OPENSSL_ROOT_DIR_${SUFFIX}}/lib "LIBS=${OPENSSL_LINK_FLAGS_${SUFFIX}}")
    elseif(CMAKE_CROSSCOMPILING AND OPENSSL_LIBRARIES_${SUFFIX})
      # a cross toolchain's own OpenSSL (e.g. a MinGW sys-root) lies outside the linker's default search path
      list(GET OPENSSL_LIBRARIES_${SUFFIX} 0 _LP_SSL)
      get_filename_component(_LP_LIBDIR "${_LP_SSL}" DIRECTORY)
      list(APPEND LIBPQ_SSL_FLAG_${SUFFIX} --with-includes=${OPENSSL_INCLUDE_DIR_${SUFFIX}}
           --with-libraries=${_LP_LIBDIR})
    elseif(OPENSSL_LIBRARIES_${SUFFIX})
      # system OpenSSL (not cross-compiling): still need to point configure at the includes and libraries
      list(GET OPENSSL_LIBRARIES_${SUFFIX} 0 _LP_SSL)
      get_filename_component(_LP_LIBDIR "${_LP_SSL}" DIRECTORY)
      list(APPEND LIBPQ_SSL_FLAG_${SUFFIX} --with-includes=${OPENSSL_INCLUDE_DIR_${SUFFIX}}
           --with-libraries=${_LP_LIBDIR})
    endif()
  else()
    set(LIBPQ_SSL_FLAG_${SUFFIX} "") # SSL is off unless --with-ssl is given
  endif()

  # libpq.a does not contain src/port and src/common: the _shlib variants are installed beside it
  # (only they export the public pg_char_to_encoding and friends libpq.a calls)
  ExternalProject_Add(
    libpq_${SUFFIX}
    DEPENDS ${LIBPQ_DEPS_${SUFFIX}}
    SOURCE_DIR ${LIBPQ_SOURCE_DIR}
    BINARY_DIR ${BINARY}/libpq-${SUFFIX}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    # config.sub from older autotools does not know dietlibc and newer OS names
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/PatchConfigSub.cmake
    CONFIGURE_COMMAND
      <SOURCE_DIR>/configure --prefix=${LIBPQ_PREFIX_${SUFFIX}} --without-readline --without-zlib --without-icu
      --without-gssapi --without-ldap --without-pam --without-systemd --without-selinux --without-libxml
      --without-libxslt --without-lz4 --without-zstd --without-perl --without-python --without-tcl
      ${LIBPQ_SSL_FLAG_${SUFFIX}} ${LIBPQ_HOST_FLAG_${SUFFIX}} "CC=${CMAKE_C_COMPILER}" "CFLAGS=${LIBPQ_CFLAGS_${SUFFIX}}"
    BUILD_COMMAND ${MAKE_PROGRAM} -C src/backend generated-headers
    COMMAND ${MAKE_PROGRAM} -C src/port
    COMMAND ${MAKE_PROGRAM} -C src/common
    COMMAND ${MAKE_PROGRAM} -C src/interfaces/libpq all-static-lib
    INSTALL_COMMAND ${MAKE_PROGRAM} -C src/include install
    COMMAND ${MAKE_PROGRAM} -C src/port install
    COMMAND ${MAKE_PROGRAM} -C src/common install
    COMMAND ${MAKE_PROGRAM} -C src/interfaces/libpq install-lib-static
    # install-lib-static leaves out the public headers
    COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/src/interfaces/libpq/libpq-fe.h
            <SOURCE_DIR>/src/interfaces/libpq/libpq-events.h ${LIBPQ_PREFIX_${SUFFIX}}/include/
    BUILD_BYPRODUCTS "${LIBPQ_LIBRARY_FILE_${SUFFIX}}")

  add_library(LibPQ::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibPQ::${SUFFIX} libpq_${SUFFIX})
  set_target_properties(LibPQ::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBPQ_LIBRARY_FILE_${SUFFIX}}")

  add_library(LibPQ::common_${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibPQ::common_${SUFFIX} libpq_${SUFFIX})
  set_target_properties(
    LibPQ::common_${SUFFIX}
    PROPERTIES IMPORTED_LOCATION
               "${LIBPQ_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pgcommon_shlib${CMAKE_STATIC_LIBRARY_SUFFIX}")

  add_library(LibPQ::port_${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibPQ::port_${SUFFIX} libpq_${SUFFIX})
  set_target_properties(
    LibPQ::port_${SUFFIX}
    PROPERTIES IMPORTED_LOCATION
               "${LIBPQ_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pgport_shlib${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # link order: libpq needs pgcommon_shlib, pgcommon_shlib needs pgport_shlib
  set(LIBPQ_LIBRARIES_${SUFFIX} LibPQ::${SUFFIX} LibPQ::common_${SUFFIX} LibPQ::port_${SUFFIX}
                                ${LIBPQ_SYSTEM_LIBS_${SUFFIX}})
endmacro()
