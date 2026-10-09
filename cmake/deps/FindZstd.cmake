macro(find_zstd)
  include(CheckLibraryExists)
  check_library_exists(zstd ZSTD_compress "${ZSTD_LIBRARY_DIR}" HAVE_ZSTD)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(ZSTD_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${ZSTD_INCLUDE_DIR}")
  endif(ZSTD_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(zstd.h HAVE_ZSTD_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_ZSTD AND HAVE_ZSTD_H)
    add_definitions(-DHAVE_ZSTD=1)
    if(NOT DEFINED ZSTD_LIBRARY)
      set(ZSTD_LIBRARY zstd CACHE STRING "zstd library")
    endif(NOT DEFINED ZSTD_LIBRARY)
  endif(HAVE_ZSTD AND HAVE_ZSTD_H)

  set(ZSTD_LIBRARIES ${ZSTD_LIBRARY})

  if(ZSTD_LIBRARY)
    message(STATUS "\tZstd library: ${ZSTD_LIBRARY}")
    message(STATUS "\tZstd include dir: ${ZSTD_INCLUDE_DIR}")

    set(ZSTD_FOUND TRUE)
  else()
    set(ZSTD_FOUND FALSE)
  endif()
endmacro()