# build_libmagic(BINARY SUFFIX PIC)
#
# Downloads and builds libmagic (from the "file" distribution) as a static library into
# ${BINARY}/deps-${SUFFIX}, once per SUFFIX ("shared"/"static") with its own PIC setting.
# Sets LIBMAGIC_INCLUDE_DIR_${SUFFIX}, LIBMAGIC_LIBRARY_FILE_${SUFFIX} and
# LIBMAGIC_DB_${SUFFIX} (the compiled magic.mgc); the ExternalProject target is
# libmagic_${SUFFIX}.
#
# Used when find_libmagic() found no usable libmagic for the target compiler. Compression
# backends are disabled so the static archive has no further library dependencies.
macro(build_libmagic BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building libmagic from source (${SUFFIX}, PIC=${PIC})")

  set(LIBMAGIC_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LIBMAGIC_INCLUDE_DIR_${SUFFIX} "${LIBMAGIC_PREFIX_${SUFFIX}}/include")
  set(LIBMAGIC_LIBRARY_FILE_${SUFFIX}
      "${LIBMAGIC_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}magic${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(LIBMAGIC_DB_${SUFFIX} "${LIBMAGIC_PREFIX_${SUFFIX}}/share/misc/magic.mgc")

  if(${PIC})
    set(LIBMAGIC_PIC_FLAG_${SUFFIX} --with-pic)
  else()
    set(LIBMAGIC_PIC_FLAG_${SUFFIX} --without-pic)
  endif()

  # cross compiling: configure must be told the target triplet or it tries to run the
  # compiled test programs. The magic.mgc compiler is the build machine's `file`, which
  # has to be the same version as the sources, so use that version when it is known.
  set(LIBMAGIC_VERSION 5.46)
  set(LIBMAGIC_HOST_FLAG_${SUFFIX})
  set(LIBMAGIC_MAKE_ARGS_${SUFFIX})
  # dietlibc: its regcomp() crashes in the target-built `file -C`, so use the host one too
  if(CMAKE_CROSSCOMPILING OR DIET OR HAVE_DIETLIBC)
    execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE LIBMAGIC_TRIPLET
                    OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
    if(LIBMAGIC_TRIPLET AND CMAKE_CROSSCOMPILING)
      set(LIBMAGIC_HOST_FLAG_${SUFFIX} --host=${LIBMAGIC_TRIPLET})
    endif()
    find_program(LIBMAGIC_HOST_FILE file)
    if(LIBMAGIC_HOST_FILE)
      execute_process(COMMAND ${LIBMAGIC_HOST_FILE} --version OUTPUT_VARIABLE LIBMAGIC_HOST_VER
                      OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
      if(LIBMAGIC_HOST_VER MATCHES "file-([0-9]+\\.[0-9]+)")
        set(LIBMAGIC_VERSION ${CMAKE_MATCH_1})
      endif()
      # a path makes the magic/Makefile skip the version check and not look for file.exe
      set(LIBMAGIC_MAKE_ARGS_${SUFFIX} FILE_COMPILE=${LIBMAGIC_HOST_FILE})
    endif()
  endif()

  # dietlibc declares wcwidth() (used by file.c) only under _XOPEN_SOURCE
  set(LIBMAGIC_CFLAGS_${SUFFIX} "-O2 -w")
  if(DIET OR HAVE_DIETLIBC)
    string(APPEND LIBMAGIC_CFLAGS_${SUFFIX} " -D_XOPEN_SOURCE")
  endif(DIET)

  # MinGW has no POSIX regex: file(1) needs libgnurx, built here as a static archive into the
  # same prefix (a single source file, no configure step)
  set(LIBMAGIC_DEPENDS_${SUFFIX})
  set(LIBMAGIC_ENV_FLAGS_${SUFFIX})
  if(MINGW OR CMAKE_SYSTEM_NAME STREQUAL "Windows")
    ExternalProject_Add(
      libgnurx_${SUFFIX}
      URL https://downloads.sourceforge.net/project/mingw/Other/UserContributed/regex/mingw-regex-2.5.1/mingw-libgnurx-2.5.1-src.tar.gz
      DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
      DOWNLOAD_EXTRACT_TIMESTAMP TRUE
      BUILD_IN_SOURCE TRUE
      CONFIGURE_COMMAND ""
      BUILD_COMMAND ${CMAKE_C_COMPILER} -O2 -w -I<SOURCE_DIR> -c <SOURCE_DIR>/regex.c -o regex.o
      COMMAND ${CMAKE_AR} rcs libgnurx.a regex.o
      INSTALL_COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBMAGIC_PREFIX_${SUFFIX}}/include ${LIBMAGIC_PREFIX_${SUFFIX}}/lib
      COMMAND ${CMAKE_COMMAND} -E copy <SOURCE_DIR>/regex.h ${LIBMAGIC_PREFIX_${SUFFIX}}/include/regex.h
      COMMAND ${CMAKE_COMMAND} -E copy libgnurx.a ${LIBMAGIC_PREFIX_${SUFFIX}}/lib/libgnurx.a
      BUILD_BYPRODUCTS "${LIBMAGIC_PREFIX_${SUFFIX}}/lib/libgnurx.a")
    set(LIBMAGIC_DEPENDS_${SUFFIX} DEPENDS libgnurx_${SUFFIX})
    set(LIBMAGIC_ENV_FLAGS_${SUFFIX} "CPPFLAGS=-I${LIBMAGIC_PREFIX_${SUFFIX}}/include"
                                     "LDFLAGS=-L${LIBMAGIC_PREFIX_${SUFFIX}}/lib")
  endif()

  ExternalProject_Add(
    libmagic_${SUFFIX}
    ${LIBMAGIC_DEPENDS_${SUFFIX}}
    URL https://astron.com/pub/file/file-${LIBMAGIC_VERSION}.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    # config.sub from older autotools does not know dietlibc and newer OS names
    PATCH_COMMAND ${CMAKE_COMMAND} -DSOURCE_DIR=<SOURCE_DIR> -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/PatchConfigSub.cmake
    CONFIGURE_COMMAND
      <SOURCE_DIR>/configure --prefix=${LIBMAGIC_PREFIX_${SUFFIX}} --libdir=${LIBMAGIC_PREFIX_${SUFFIX}}/lib
      --enable-static --disable-shared --disable-libseccomp --disable-zlib --disable-bzlib --disable-xzlib
      --disable-zstdlib --disable-lzlib ${LIBMAGIC_PIC_FLAG_${SUFFIX}} ${LIBMAGIC_HOST_FLAG_${SUFFIX}} ${LIBMAGIC_ENV_FLAGS_${SUFFIX}} "CC=${CMAKE_C_COMPILER}" "CFLAGS=${LIBMAGIC_CFLAGS_${SUFFIX}}"
    BUILD_COMMAND make ${LIBMAGIC_MAKE_ARGS_${SUFFIX}}
    INSTALL_COMMAND make ${LIBMAGIC_MAKE_ARGS_${SUFFIX}} install
    BUILD_BYPRODUCTS "${LIBMAGIC_LIBRARY_FILE_${SUFFIX}}" "${LIBMAGIC_DB_${SUFFIX}}")

  add_library(LibMagic::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibMagic::${SUFFIX} libmagic_${SUFFIX})
  set_target_properties(LibMagic::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBMAGIC_LIBRARY_FILE_${SUFFIX}}")
  if(LIBMAGIC_DEPENDS_${SUFFIX})
    set_target_properties(LibMagic::${SUFFIX} PROPERTIES INTERFACE_LINK_LIBRARIES
                          "${LIBMAGIC_PREFIX_${SUFFIX}}/lib/libgnurx.a;shlwapi")
  endif()
  set(LibMagic_LIBRARIES_${SUFFIX} LibMagic::${SUFFIX})
endmacro()
