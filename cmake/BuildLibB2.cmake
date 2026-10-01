# build_libb2(BINARY SUFFIX PIC)
#
# Downloads and builds libb2 as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets LIBB2_INCLUDE_DIR_${SUFFIX} and
# LIBB2_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is libb2_${SUFFIX}.
#
# libb2 is autotools-only, hence the in-source configure/make invocation.
macro(build_libb2 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building libb2 from source (${SUFFIX}, PIC=${PIC})")

  set(LIBB2_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(LIBB2_INCLUDE_DIR_${SUFFIX} "${LIBB2_PREFIX_${SUFFIX}}/include")
  set(LIBB2_LIBRARY_FILE_${SUFFIX} "${LIBB2_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}b2${CMAKE_STATIC_LIBRARY_SUFFIX}")

  if(PIC)
    set(LIBB2_PIC_FLAG_${SUFFIX} --with-pic)
  else(PIC)
    set(LIBB2_PIC_FLAG_${SUFFIX} --without-pic)
  endif(PIC)

  ExternalProject_Add(
    libb2_${SUFFIX}
    URL https://github.com/BLAKE2/libb2/releases/download/v0.98.1/libb2-0.98.1.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND <SOURCE_DIR>/configure --prefix=${LIBB2_PREFIX_${SUFFIX}} --libdir=${LIBB2_PREFIX_${SUFFIX}}/lib
                      --enable-static --disable-shared ${LIBB2_PIC_FLAG_${SUFFIX}} "CC=${CMAKE_C_COMPILER}" "CFLAGS=-w"
    BUILD_COMMAND make
    INSTALL_COMMAND make install
    BUILD_BYPRODUCTS "${LIBB2_LIBRARY_FILE_${SUFFIX}}")
endmacro(build_libb2)
