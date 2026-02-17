macro(find_mariadb)
  pkg_check_modules(MARIADB libmariadb mariadb)

  link_directories(${LIBMARIADB_LIBRARY_DIR})
  
  if(pkgcfg_lib_MARIADB_mariadb)
    message(STATUS "MariaDB found by pkg-config")
    set(LIBMARIADB_LIBRARY "${pkgcfg_lib_MARIADB_mariadb}" CACHE STRING "MariaDB client library")
    unset(pkgcfg_lib_MARIADB_mariadb CACHE)
  else(pkgcfg_lib_MARIADB_mariadb)
    list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${LIBMARIADB_LIBRARY_DIR}")
    check_library_exists(mariadb mysql_init "${LIBMARIADB_LIBRARY_DIR}" HAVE_LIBMARIADB)
    message(STATUS "MariaDB found by directory search")

    if(HAVE_LIBMARIADB)
      set(LIBMARIADB_LIBRARY mariadb CACHE STRING "MariaDB client library")
    endif(HAVE_LIBMARIADB)
  endif(pkgcfg_lib_MARIADB_mariadb)

  #string(REGEX REPLACE "/include.*" "/${CMAKE_ARCH_LIBDIR}" LIBMARIADB_LIBRARY_DIR "${LIBMARIADB_INCLUDE_DIR}")
  #set(LIBMARIADB_LIBRARY_DIR "${LIBMARIADB_LIBRARY_DIR}" CACHE PATH "MariaDB library directory")

  if(NOT LIBMARIADB_LIBRARY_DIR)
    if(LIBMARIADB_LIBRARY)
      string(REGEX REPLACE "/[^/]*\$" "" LIBMARIADB_LIBRARY_DIR "${LIBMARIADB_LIBRARY}")
    endif(LIBMARIADB_LIBRARY)
  endif(NOT LIBMARIADB_LIBRARY_DIR)

  if(NOT LIBMARIADB_LIBRARY)
    message(WARNING "No mariadb library")
  endif(NOT LIBMARIADB_LIBRARY)

  if(LIBMARIADB_LIBRARY)
    list(APPEND QUICKJS_MODULES mysql)

    unset(MARIADB_LDFLAGS)
    set(mysql_LIBRARIES ${LIBMARIADB_LIBRARY})
    set(mysql_LINK_DIRECTORIES ${LIBMARIADB_LIBRARY_DIR})

    if(NOT LIBMARIADB_INCLUDE_DIR)
      if(LIBMARIADB_LIBRARY_DIR)
        string(REGEX REPLACE "/lib.*" "/include" LIBMARIADB_INCLUDE_DIR "${ĹIBMARIADB_LIBRARY_DIR}")
      endif(LIBMARIADB_LIBRARY_DIR)
    endif(NOT LIBMARIADB_INCLUDE_DIR)

    if(NOT LIBMARIADB_INCLUDE_DIR)
      if(LIBMARIADB_LIBRARY)
        string(REGEX REPLACE "/lib.*" "/include" LIBMARIADB_INCLUDE_DIR "${ĹIBMARIADB_LIBRARY}")
      endif(LIBMARIADB_LIBRARY)
    endif(NOT LIBMARIADB_INCLUDE_DIR)

    if(LIBMARIADB_INCLUDE_DIR)
      if(NOT EXISTS "${LIBMARIADB_INCLUDE_DIR}/mysql.h")
        if(EXISTS "${LIBMARIADB_INCLUDE_DIR}/mariadb" AND EXISTS "${LIBMARIADB_INCLUDE_DIR}/mariadb/mysql.h")
          set(LIBMARIADB_INCLUDE_DIR "${LIBMARIADB_INCLUDE_DIR}/mariadb")
        endif()
      endif()

      if(EXISTS "${LIBMARIADB_INCLUDE_DIR}")
        set(LIBMARIADB_INCLUDE_DIR "${LIBMARIADB_INCLUDE_DIR}" CACHE PATH "MariaDB client include directory")
      else(EXISTS "${LIBMARIADB_INCLUDE_DIR}")
        unset(LIBMARIADB_INCLUDE_DIR)
      endif(EXISTS "${LIBMARIADB_INCLUDE_DIR}")

      message("LIBMARIADB_INCLUDE_DIR: ${LIBMARIADB_INCLUDE_DIR}")
    endif(LIBMARIADB_INCLUDE_DIR)

    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBMARIADB_INCLUDE_DIR}")
    check_include_def(mysql.h)

    if(HAVE_MYSQL_H OR EXISTS "${LIBMARIADB_INCLUDE_DIR}")
      include_directories(${LIBMARIADB_INCLUDE_DIR})
    endif()

    list(APPEND CMAKE_REQUIRED_LIBRARIES "${LIBMARIADB_LIBRARY}")
    list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${LIBMARIADB_LIBRARY_DIR}")

    check_function_exists(mysql_optionsv HAVE_MYSQL_OPTIONSV)
    check_function_exists(mysql_real_query_start HAVE_MYSQL_REAL_QUERY_START)

    if(HAVE_MYSQL_REAL_QUERY_START)
      set(LIBMARIADB_HAVE_ASYNC_FUNCTIONS TRUE)
    endif(HAVE_MYSQL_REAL_QUERY_START)

    list(REMOVE_ITEM CMAKE_REQUIRED_LIBRARIES "${LIBMARIADB_LIBRARY}")
    list(REMOVE_ITEM CMAKE_REQUIRED_LINK_DIRECTORIES "${LIBMARIADB_LIBRARY_DIR}")
  endif(LIBMARIADB_LIBRARY)
endmacro(find_mariadb)
