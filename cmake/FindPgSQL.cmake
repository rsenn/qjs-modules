macro(find_pgsql)
  pkg_check_modules(LIBPQ libpq)

  if(pkgcfg_lib_LIBPQ_pq)
    set(LIBPQ_LIBRARY "${pkgcfg_lib_LIBPQ_pq}" CACHE STRING "PostgreSQL client library")
    unset(pkgcfg_lib_LIBPQ_pq CACHE)
  else(pkgcfg_lib_LIBPQ_pq)
    check_library_exists(pq PQconnectdb "${LIBPQ_LIBRARY_DIR}" HAVE_LIBPQCLIENT)

    if(HAVE_LIBPQCLIENT)
      set(LIBPQ_LIBRARY pq CACHE STRING "PostgreSQL client library")
    endif(HAVE_LIBPQCLIENT)
  endif(pkgcfg_lib_LIBPQ_pq)

  if(LIBPQ_LIBRARY)
    list(APPEND QUICKJS_MODULES pgsql)
    set(pgsql_LIBRARIES ${LIBPQ_LIBRARY})

    if("${LIBPQ_LIBRARY}" MATCHES "[/\\]")
      string(REGEX REPLACE "/lib.*" "/include" LIBPQ_INCLUDE_DIR "${LIBPQ_LIBRARY}")
    else("${LIBPQ_LIBRARY}" MATCHES "[/\\]")
      set(LIBPQ_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    endif("${LIBPQ_LIBRARY}" MATCHES "[/\\]")

    if(NOT EXISTS "${LIBPQ_INCLUDE_DIR}/libpq-fe.h")
      if(EXISTS "${LIBPQ_INCLUDE_DIR}/postgresql" AND EXISTS "${LIBPQ_INCLUDE_DIR}/postgresql/libpq-fe.h")
        set(LIBPQ_INCLUDE_DIR "${LIBPQ_INCLUDE_DIR}/postgresql")
      endif()
    endif()

    set(LIBPQ_INCLUDE_DIR "${LIBPQ_INCLUDE_DIR}" CACHE PATH "PostgresSQL client include directory")

    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBPQ_INCLUDE_DIR}")
    check_include_def(libpq-fe.h)

    if(HAVE_LIBPQ_FE_H OR EXISTS "${LIBPQ_INCLUDE_DIR}")
      include_directories(${LIBPQ_INCLUDE_DIR})
    endif()
    unset(pkgcfg_lib_LIBPQ_pq CACHE)
  endif(LIBPQ_LIBRARY)
endmacro(find_pgsql)
