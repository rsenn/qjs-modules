# build_sqlite(BINARY SUFFIX PIC)
#
# Downloads and builds SQLite as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets SQLITE_INCLUDE_DIR_${SUFFIX} and
# SQLITE_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is sqlite_${SUFFIX}.
#
# Used when find_sqlite() found no usable sqlite3 for the target compiler. Built from the
# autoconf tarball, hence the in-source configure/make invocation.
macro(build_sqlite BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building sqlite from source (${SUFFIX}, PIC=${PIC})")

  set(SQLITE_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(SQLITE_INCLUDE_DIR_${SUFFIX} "${SQLITE_PREFIX_${SUFFIX}}/include")
  set(SQLITE_LIBRARY_FILE_${SUFFIX} "${SQLITE_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}sqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}")

  set(SQLITE_CFLAGS_${SUFFIX} "-O2 -w")
  if(${PIC})
    string(APPEND SQLITE_CFLAGS_${SUFFIX} " -fPIC")
  endif()

  ExternalProject_Add(
    sqlite_${SUFFIX}
    URL https://www.sqlite.org/2025/sqlite-autoconf-3500400.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${SQLITE_PREFIX_${SUFFIX}} --libdir=${SQLITE_PREFIX_${SUFFIX}}/lib
                      --enable-static --disable-shared --disable-readline
                      "CC=${CMAKE_C_COMPILER}" "CFLAGS=${SQLITE_CFLAGS_${SUFFIX}}"
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${SQLITE_LIBRARY_FILE_${SUFFIX}}")

  add_library(SQLite::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(SQLite::${SUFFIX} sqlite_${SUFFIX})
  set_target_properties(SQLite::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${SQLITE_LIBRARY_FILE_${SUFFIX}}")
  set(SQLite_LIBRARIES_${SUFFIX} SQLite::${SUFFIX})
endmacro()
