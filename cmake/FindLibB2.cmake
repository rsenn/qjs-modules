macro(find_libb2)
  include(CheckLibraryExists)
  check_library_exists(b2 blake2b_init "${LIBB2_LIBRARY_DIR}" HAVE_LIBB2)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(LIBB2_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBB2_INCLUDE_DIR}")
  endif(LIBB2_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(blake2.h HAVE_BLAKE2_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LIBB2 AND HAVE_BLAKE2_H)
    add_definitions(-DHAVE_LIBB2=1)
    if(NOT DEFINED LIBB2_LIBRARY)
      set(LIBB2_LIBRARY b2 CACHE STRING "libb2 library")
    endif(NOT DEFINED LIBB2_LIBRARY)
  endif(HAVE_LIBB2 AND HAVE_BLAKE2_H)

  set(LIBB2_LIBRARIES ${LIBB2_LIBRARY})

  message(STATUS "\tLibB2 library: ${LIBB2_LIBRARY}")
  message(STATUS "\tLibB2 include dir: ${LIBB2_INCLUDE_DIR}")

  if(LIBB2_LIBRARY)
    set(LIBB2_FOUND TRUE)
  else()
    set(LIBB2_FOUND FALSE)
  endif()
    
 endmacro(find_libb2)