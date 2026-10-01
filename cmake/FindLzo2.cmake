macro(find_lzo2)
  include(CheckLibraryExists)
  check_library_exists(lzo2 lzo_init "${LZO2_LIBRARY_DIR}" HAVE_LZO2)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  if(LZO2_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LZO2_INCLUDE_DIR}")
  endif(LZO2_INCLUDE_DIR)

  include(CheckIncludeFile)
  check_include_file(lzo/lzo1x.h HAVE_LZO_LZO1X_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LZO2 AND HAVE_LZO_LZO1X_H)
    add_definitions(-DHAVE_LZO2=1)
    if(NOT DEFINED LZO2_LIBRARY)
      set(LZO2_LIBRARY lzo2 CACHE STRING "LZO2 library")
    endif(NOT DEFINED LZO2_LIBRARY)
  endif(HAVE_LZO2 AND HAVE_LZO_LZO1X_H)

  set(LZO2_LIBRARIES ${LZO2_LIBRARY})

  message(STATUS "\tLZO2 library: ${LZO2_LIBRARY}")
  message(STATUS "\tLZO2 include dir: ${LZO2_INCLUDE_DIR}")

  if(LZO2_LIBRARY)
    set(LZO2_FOUND TRUE)
  else()
    set(LZO2_FOUND FALSE)
  endif()  
endmacro(find_lzo2)