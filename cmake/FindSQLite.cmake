macro(find_sqlite)
  pkg_check_modules(LIBSQLITE sqlite3)

  if(pkgcfg_lib_LIBSQLITE_sqlite3)
    set(LIBSQLITE_LIBRARY "${pkgcfg_lib_LIBSQLITE_sqlite3}" CACHE STRING "SQLite library")
    unset(pkgcfg_lib_LIBSQLITE_sqlite3 CACHE)
  else(pkgcfg_lib_LIBSQLITE_sqlite3)
    check_library_exists(sqlite3 sqlite3_open "${LIBSQLITE_LIBRARY_DIR}" HAVE_LIBSQLITE3)

    if(HAVE_LIBSQLITE3)
      set(LIBSQLITE_LIBRARY sqlite3 CACHE STRING "SQLite library")
    endif(HAVE_LIBSQLITE3)
  endif(pkgcfg_lib_LIBSQLITE_sqlite3)

  if(LIBSQLITE_LIBRARY)
    list(APPEND QUICKJS_MODULES sqlite)
    set(sqlite_LIBRARIES ${LIBSQLITE_LIBRARY})

    if("${LIBSQLITE_LIBRARY}" MATCHES "[/\\]")
      string(REGEX REPLACE "/lib.*" "/include" LIBSQLITE_INCLUDE_DIR "${LIBSQLITE_LIBRARY}")
    else("${LIBSQLITE_LIBRARY}" MATCHES "[/\\]")
      set(LIBSQLITE_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    endif("${LIBSQLITE_LIBRARY}" MATCHES "[/\\]")

    if(NOT EXISTS "${LIBSQLITE_INCLUDE_DIR}/sqlite3.h")
      if(EXISTS "${LIBSQLITE_INCLUDE_DIR}/sqlite3" AND EXISTS "${LIBSQLITE_INCLUDE_DIR}/sqlite3/sqlite3.h")
        set(LIBSQLITE_INCLUDE_DIR "${LIBSQLITE_INCLUDE_DIR}/sqlite3")
      endif()
    endif()

    set(LIBSQLITE_INCLUDE_DIR "${LIBSQLITE_INCLUDE_DIR}" CACHE PATH "SQLite include directory")

    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBSQLITE_INCLUDE_DIR}")
    check_include_def(sqlite3.h)

    if(HAVE_SQLITE3_H OR EXISTS "${LIBSQLITE_INCLUDE_DIR}")
      include_directories(${LIBSQLITE_INCLUDE_DIR})
    endif()
    unset(pkgcfg_lib_LIBSQLITE_sqlite3 CACHE)
  endif(LIBSQLITE_LIBRARY)
endmacro(find_sqlite)
