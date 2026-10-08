macro(find_zlib)
  include(CheckLibraryExists)
  check_library_exists(z deflate "${ZLIB_LIBRARY_DIR}" HAVE_ZLIB)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(ZLIB_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${ZLIB_INCLUDE_DIR}")
  endif(ZLIB_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(zlib.h HAVE_ZLIB_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_ZLIB AND HAVE_ZLIB_H)
    add_definitions(-DHAVE_ZLIB=1)
    if(NOT DEFINED ZLIB_LIBRARY)
      set(ZLIB_LIBRARY z CACHE STRING "zlib library")
    endif(NOT DEFINED ZLIB_LIBRARY)
  endif(HAVE_ZLIB AND HAVE_ZLIB_H)

  set(ZLIB_LIBRARIES ${ZLIB_LIBRARY})

  if(ZLIB_LIBRARY)
    message(STATUS "\tZlib library: ${ZLIB_LIBRARY}")
    message(STATUS "\tZlib include dir: ${ZLIB_INCLUDE_DIR}")
    
    set(ZLIB_FOUND TRUE)
  else()
    set(ZLIB_FOUND FALSE)
  endif()
endmacro(find_zlib)