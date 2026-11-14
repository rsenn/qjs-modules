# build_libmariadb(SOURCE BINARY SUFFIX PIC)
#
# Builds MariaDB Connector/C from its git repository (third_party/mariadb-connector-c, cloned
# on first use) as a static library, once per SUFFIX ("shared"/"static") with its own PIC
# setting. Only the client library target (mariadbclient) is built: no tests, no tools, no
# dynamic plugins, no curl; the zlib bundled with the connector is used.
#
# Sets LIBMARIADB_INCLUDE_DIR_${SUFFIX} and LIBMARIADB_LIBRARIES_${SUFFIX} (the archive, then
# the system libraries it needs); the ExternalProject target is libmariadb_${SUFFIX}.
# Used when BUILD_MYSQL is ON. TLS comes from use_openssl() (BuildOpenSSL.cmake): the connector
# has no build without a TLS library (WITH_SSL=OFF is rejected by its CMakeLists.txt).
set(LIBMARIADB_GIT_REPOSITORY "https://github.com/mariadb-corporation/mariadb-connector-c.git"
    CACHE STRING "MariaDB Connector/C git repository")
set(LIBMARIADB_GIT_TAG "v3.4.9" CACHE STRING "MariaDB Connector/C git tag to build")

include(${CMAKE_CURRENT_LIST_DIR}/BuildOpenSSL.cmake)

macro(build_libmariadb SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building mariadb-connector-c from git (${SUFFIX}, PIC=${PIC})")

  set(LIBMARIADB_SOURCE_DIR "${SOURCE}/third_party/mariadb-connector-c")
  set(LIBMARIADB_BINARY_DIR_${SUFFIX} "${BINARY}/libmariadb-${SUFFIX}")
  # mariadb_version.h is generated into the build tree, the rest of the headers stay in the source
  set(LIBMARIADB_INCLUDE_DIR_${SUFFIX} "${LIBMARIADB_SOURCE_DIR}/include" "${LIBMARIADB_BINARY_DIR_${SUFFIX}}/include")
  set(LIBMARIADB_LIBRARY_FILE_${SUFFIX}
      "${LIBMARIADB_BINARY_DIR_${SUFFIX}}/libmariadb/${CMAKE_STATIC_LIBRARY_PREFIX}mariadbclient${CMAKE_STATIC_LIBRARY_SUFFIX}"
  )

  # one clone for both variants, skipped when third_party/mariadb-connector-c is already checked out
  if(NOT TARGET libmariadb_source)
    if(EXISTS "${LIBMARIADB_SOURCE_DIR}/CMakeLists.txt")
      add_custom_target(libmariadb_source)
    else()
      ExternalProject_Add(
        libmariadb_source
        GIT_REPOSITORY ${LIBMARIADB_GIT_REPOSITORY}
        GIT_TAG ${LIBMARIADB_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${LIBMARIADB_SOURCE_DIR}
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
    endif()
  endif()

  set(LIBMARIADB_ARGS_${SUFFIX}
      "-DWITH_UNIT_TESTS:BOOL=OFF"
      "-DWITH_CURL:BOOL=OFF"
      "-DWITH_EXTERNAL_ZLIB:BOOL=OFF"
      "-DWITH_MYSQLCOMPAT:BOOL=OFF"
      "-DWITH_SIGNCODE:BOOL=OFF"
      "-DCLIENT_PLUGIN_DIALOG:STRING=OFF"
      "-DCLIENT_PLUGIN_MYSQL_CLEAR_PASSWORD:STRING=OFF"
      "-DCLIENT_PLUGIN_AUTH_GSSAPI_CLIENT:STRING=OFF"
      "-DCLIENT_PLUGIN_REMOTE_IO:STRING=OFF"
      "-DCLIENT_PLUGIN_PVIO_NPIPE:STRING=OFF"
      "-DCLIENT_PLUGIN_PVIO_SHMEM:STRING=OFF"
      "-DCLIENT_PLUGIN_SHA256_PASSWORD:STRING=STATIC"
      "-DCLIENT_PLUGIN_CACHING_SHA2_PASSWORD:STRING=STATIC"
      "-DWITH_SSL:STRING=OPENSSL"
      "-DCLIENT_PLUGIN_ED25519:STRING=OFF"
      "-DCLIENT_PLUGIN_COMPRESS:STRING=OFF"
      "-DCLIENT_PLUGIN_ZSTD:STRING=OFF"
      "-DCLIENT_PLUGIN_PVIO_SOCKET:STRING=STATIC"
      "-DCLIENT_PLUGIN_MYSQL_NATIVE_PASSWORD:STRING=STATIC"
      "-DCLIENT_PLUGIN_MYSQL_OLD_PASSWORD:STRING=OFF")

  use_openssl(${SOURCE} ${BINARY} ${SUFFIX} ${PIC})
  set(LIBMARIADB_SYSTEM_LIBS_${SUFFIX} ${OPENSSL_LIBRARIES_${SUFFIX}} ${CMAKE_DL_LIBS} m pthread)

  # a built OpenSSL is a static prefix the connector's find_package(OpenSSL) has to be pointed at
  if(OPENSSL_ROOT_DIR_${SUFFIX})
    list(APPEND LIBMARIADB_ARGS_${SUFFIX} "-DOPENSSL_ROOT_DIR:PATH=${OPENSSL_ROOT_DIR_${SUFFIX}}"
         "-DOPENSSL_USE_STATIC_LIBS:BOOL=ON")
  endif()

  ExternalProject_Add(
    libmariadb_${SUFFIX}
    DEPENDS libmariadb_source ${OPENSSL_DEPENDS_${SUFFIX}}
    SOURCE_DIR ${LIBMARIADB_SOURCE_DIR}
    BINARY_DIR ${LIBMARIADB_BINARY_DIR_${SUFFIX}}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_CACHE_ARGS
      ${LIBMARIADB_ARGS_${SUFFIX}}
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=OFF"
    BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target mariadbclient
    INSTALL_COMMAND ""
    BUILD_BYPRODUCTS "${LIBMARIADB_LIBRARY_FILE_${SUFFIX}}")

  add_library(LibMariaDB::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibMariaDB::${SUFFIX} libmariadb_${SUFFIX})
  set_target_properties(LibMariaDB::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBMARIADB_LIBRARY_FILE_${SUFFIX}}")

  # an archive carries no dependencies
  set(LIBMARIADB_LIBRARIES_${SUFFIX} LibMariaDB::${SUFFIX} ${LIBMARIADB_SYSTEM_LIBS_${SUFFIX}})
endmacro()
