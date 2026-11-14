macro(find_libmagic)

  include(CheckLibraryExists)
  check_library_exists(magic magic_open "${LIBMAGIC_LIBRARY_DIR}" HAVE_LIBMAGIC)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")

  # a sysroot keeps its headers outside the compiler's own search path (sys-root/mingw/include)
  if(NOT LIBMAGIC_INCLUDE_DIR)
    find_path(LIBMAGIC_INCLUDE_DIR magic.h)
  endif(NOT LIBMAGIC_INCLUDE_DIR)

  if(LIBMAGIC_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBMAGIC_INCLUDE_DIR}")
  endif(LIBMAGIC_INCLUDE_DIR)

  check_include_file(magic.h HAVE_MAGIC_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LIBMAGIC AND HAVE_MAGIC_H)
    if(NOT DEFINED LIBMAGIC_LIBRARY)
      set(LIBMAGIC_LIBRARY magic CACHE STRING "libmagic library")
    endif(NOT DEFINED LIBMAGIC_LIBRARY)
    set(magic_LIBRARIES ${LIBMAGIC_LIBRARY})
    set(magic_MODULE magic)
  else(HAVE_LIBMAGIC AND HAVE_MAGIC_H)
    set(magic_MODULE)
  endif(HAVE_LIBMAGIC AND HAVE_MAGIC_H)

  if(EXISTS /usr/share/file/magic)
    set(LIBMAGIC_DB /usr/share/file/magic)
  endif(EXISTS /usr/share/file/magic)

  message_table("libmagic" "library" "${LIBMAGIC_LIBRARY}" "module" "${magic_MODULE}" "database" "${LIBMAGIC_DB}")

endmacro()
