macro(find_liblz4)
  include(CheckLibraryExists)
  check_library_exists(lz4 LZ4_compress_default "${LZ4_LIBRARY_DIR}" HAVE_LIBLZ4)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(LZ4_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LZ4_INCLUDE_DIR}")
  endif(LZ4_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(lz4.h HAVE_LZ4_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LIBLZ4 AND HAVE_LZ4_H)
    add_definitions(-DHAVE_LIBLZ4=1)
    if(NOT DEFINED LZ4_LIBRARY)
      set(LZ4_LIBRARY lz4 CACHE STRING "LZ4 library")
    endif(NOT DEFINED LZ4_LIBRARY)
  endif(HAVE_LIBLZ4 AND HAVE_LZ4_H)

  set(LZ4_LIBRARIES ${LZ4_LIBRARY})

  if(LZ4_LIBRARY)
    message(STATUS "\tLZ4 library: ${LZ4_LIBRARY}")
    message(STATUS "\tLZ4 include dir: ${LZ4_INCLUDE_DIR}")

    set(LZ4_FOUND TRUE)
  else()
    set(LZ4_FOUND FALSE)
  endif()
endmacro()