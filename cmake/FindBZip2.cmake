macro(find_bzip2)
  include(CheckLibraryExists)
  check_library_exists(bz2 BZ2_bzCompressInit "${BZIP2_LIBRARY_DIR}" HAVE_BZIP2)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(BZIP2_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${BZIP2_INCLUDE_DIR}")
  endif(BZIP2_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(bzlib.h HAVE_BZLIB_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_BZIP2 AND HAVE_BZLIB_H)
    add_definitions(-DHAVE_BZIP2=1)
    if(NOT DEFINED BZIP2_LIBRARY)
      set(BZIP2_LIBRARY bz2 CACHE STRING "bzip2 library")
    endif(NOT DEFINED BZIP2_LIBRARY)
  endif(HAVE_BZIP2 AND HAVE_BZLIB_H)

  set(BZIP2_LIBRARIES ${BZIP2_LIBRARY})
  
  if(BZIP2_LIBRARY)
    message(STATUS "\tBZip2 library: ${BZIP2_LIBRARY}")
    message(STATUS "\tBZip2 include dir: ${BZIP2_INCLUDE_DIR}")

    set(BZIP2_FOUND TRUE)
  else()
    set(BZIP2_FOUND FALSE)
  endif()
  endmacro(find_bzip2)
