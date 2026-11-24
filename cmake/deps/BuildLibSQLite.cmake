# build_libsqlite(SOURCE BINARY SUFFIX PIC)
#
# Builds the SQLite library from its git repository (third_party/sqlite, cloned on first use)
# as a static library, once per SUFFIX ("shared"/"static") with its own PIC setting. Only the
# amalgamation sqlite3.c is compiled: no shell, no tcl extension, no readline.
#
# Sets LIBSQLITE_INCLUDE_DIR_${SUFFIX} and LIBSQLITE_LIBRARIES_${SUFFIX} (the archive, then
# the system libraries it needs); the ExternalProject target is libsqlite_${SUFFIX}.
# Needs tclsh to generate the amalgamation. Used when BUILD_LIBSQLITE is ON.
set(LIBSQLITE_GIT_REPOSITORY "https://github.com/sqlite/sqlite.git" CACHE STRING "SQLite git repository")
set(LIBSQLITE_GIT_TAG "version-3.50.4" CACHE STRING "SQLite git tag to build")

macro(build_libsqlite SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building sqlite from git (${SUFFIX}, PIC=${PIC})")

  set(LIBSQLITE_SOURCE_DIR "${SOURCE}/third_party/sqlite")
  set(LIBSQLITE_PREFIX_${SUFFIX} "${BINARY}/deps-sqlite-${SUFFIX}")
  set(LIBSQLITE_INCLUDE_DIR_${SUFFIX} "${LIBSQLITE_PREFIX_${SUFFIX}}/include")
  set(LIBSQLITE_LIBRARY_FILE_${SUFFIX}
      "${LIBSQLITE_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}sqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # one clone for both variants, skipped when third_party/sqlite is already checked out
  if(NOT TARGET libsqlite_source)
    if(EXISTS "${LIBSQLITE_SOURCE_DIR}/configure")
      add_custom_target(libsqlite_source)
    else()
      ExternalProject_Add(
        libsqlite_source
        GIT_REPOSITORY ${LIBSQLITE_GIT_REPOSITORY}
        GIT_TAG ${LIBSQLITE_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${LIBSQLITE_SOURCE_DIR}
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
    endif()
  endif()

  find_program(MAKE_PROGRAM NAMES gmake make)
  if(NOT MAKE_PROGRAM)
    message(FATAL_ERROR "building SQLite needs make")
  endif()

  find_program(TCLSH_EXECUTABLE NAMES tclsh tclsh8.6 tclsh9.0)
  if(NOT TCLSH_EXECUTABLE)
    message(FATAL_ERROR "BUILD_LIBSQLITE needs tclsh to generate the sqlite3.c amalgamation")
  endif()

  set(LIBSQLITE_CFLAGS_${SUFFIX} "-O2 -w")
  if(${PIC})
    string(APPEND LIBSQLITE_CFLAGS_${SUFFIX} " -fPIC")
  endif()

  ExternalProject_Add(
    libsqlite_${SUFFIX}
    DEPENDS libsqlite_source
    SOURCE_DIR ${LIBSQLITE_SOURCE_DIR}
    BINARY_DIR ${BINARY}/libsqlite-${SUFFIX}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    # config.sub from older autotools does not know dietlibc and newer OS names
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/PatchConfigSub.cmake
    CONFIGURE_COMMAND <SOURCE_DIR>/configure "TCLSH_CMD=${TCLSH_EXECUTABLE}" --disable-readline "CC=${CMAKE_C_COMPILER}"
    BUILD_COMMAND ${MAKE_PROGRAM} sqlite3.c
    COMMAND sh -c "\"${CMAKE_C_COMPILER}\" ${LIBSQLITE_CFLAGS_${SUFFIX}} -c sqlite3.c -o sqlite3.o"
    COMMAND "${CMAKE_AR}" rcs ${CMAKE_STATIC_LIBRARY_PREFIX}sqlite3${CMAKE_STATIC_LIBRARY_SUFFIX} sqlite3.o
    INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBSQLITE_PREFIX_${SUFFIX}}/include ${LIBSQLITE_PREFIX_${SUFFIX}}/lib
    COMMAND ${CMAKE_COMMAND} -E copy sqlite3.h ${LIBSQLITE_PREFIX_${SUFFIX}}/include/
    COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_STATIC_LIBRARY_PREFIX}sqlite3${CMAKE_STATIC_LIBRARY_SUFFIX}
            ${LIBSQLITE_PREFIX_${SUFFIX}}/lib/
    BUILD_BYPRODUCTS "${LIBSQLITE_LIBRARY_FILE_${SUFFIX}}")

  add_library(LibSQLite::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibSQLite::${SUFFIX} libsqlite_${SUFFIX})
  set_target_properties(LibSQLite::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBSQLITE_LIBRARY_FILE_${SUFFIX}}")

  # an archive carries no dependencies: the amalgamation needs these at link time
  set(LIBSQLITE_LIBRARIES_${SUFFIX} LibSQLite::${SUFFIX} ${CMAKE_DL_LIBS} m pthread)
endmacro()
