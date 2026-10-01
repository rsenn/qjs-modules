macro(find_liblzma)
  include(CheckLibraryExists)
  check_library_exists(lzma lzma_code "${LIBLZMA_LIBRARY_DIR}" HAVE_LIBLZMA)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(LIBLZMA_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBLZMA_INCLUDE_DIR}")
  endif(LIBLZMA_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(lzma.h HAVE_LZMA_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LIBLZMA AND HAVE_LZMA_H)
    add_definitions(-DHAVE_LIBLZMA=1)
    if(NOT DEFINED LIBLZMA_LIBRARY)
      set(LIBLZMA_LIBRARY lzma CACHE STRING "liblzma library")
    endif(NOT DEFINED LIBLZMA_LIBRARY)
  endif(HAVE_LIBLZMA AND HAVE_LZMA_H)

  set(LIBLZMA_LIBRARIES ${LIBLZMA_LIBRARY})

  message(STATUS "\tLibLzma library: ${LIBLZMA_LIBRARY}")
  message(STATUS "\tLibLzma include dir: ${LIBLZMA_INCLUDE_DIR}")

  if(LIBLZMA_LIBRARY)
    set(LIBLZMA_FOUND TRUE)
  else()
    set(LIBLZMA_FOUND FALSE)
  endif()
endmacro(find_liblzma)