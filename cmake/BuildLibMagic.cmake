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
  set(LIBMAGIC_LIBRARY_FILE_${SUFFIX} "${LIBMAGIC_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}magic${CMAKE_STATIC_LIBRARY_SUFFIX}")
  set(LIBMAGIC_DB_${SUFFIX} "${LIBMAGIC_PREFIX_${SUFFIX}}/share/misc/magic.mgc")

  if(${PIC})
    set(LIBMAGIC_PIC_FLAG_${SUFFIX} --with-pic)
  else()
    set(LIBMAGIC_PIC_FLAG_${SUFFIX} --without-pic)
  endif()

  ExternalProject_Add(
    libmagic_${SUFFIX}
    URL https://astron.com/pub/file/file-5.46.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads-${SUFFIX}
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${LIBMAGIC_PREFIX_${SUFFIX}} --libdir=${LIBMAGIC_PREFIX_${SUFFIX}}/lib
                      --enable-static --disable-shared --disable-libseccomp --disable-zlib --disable-bzlib
                      --disable-xzlib --disable-zstdlib --disable-lzlib ${LIBMAGIC_PIC_FLAG_${SUFFIX}}
                      "CC=${CMAKE_C_COMPILER}" "CFLAGS=-O2 -w"
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${LIBMAGIC_LIBRARY_FILE_${SUFFIX}}" "${LIBMAGIC_DB_${SUFFIX}}")

  add_library(LibMagic::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibMagic::${SUFFIX} libmagic_${SUFFIX})
  set_target_properties(LibMagic::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBMAGIC_LIBRARY_FILE_${SUFFIX}}")
  set(LibMagic_LIBRARIES_${SUFFIX} LibMagic::${SUFFIX})
endmacro(build_libmagic)
