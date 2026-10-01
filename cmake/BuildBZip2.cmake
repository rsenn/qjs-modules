# build_bzip2(BINARY SUFFIX PIC)
#
# Downloads and builds bzip2 as a static library into ${BINARY}/deps-${SUFFIX}, once per
# SUFFIX ("shared"/"static") with its own PIC setting. Sets BZIP2_INCLUDE_DIR_${SUFFIX} and
# BZIP2_LIBRARY_FILE_${SUFFIX}; the ExternalProject target is bzip2_${SUFFIX}.
#
# bzip2 1.0.8 ships only a Makefile, hence the in-source make invocation.
macro(build_bzip2 BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building bzip2 from source (${SUFFIX}, PIC=${PIC})")

  set(BZIP2_PREFIX_${SUFFIX} "${BINARY}/deps-${SUFFIX}")
  set(BZIP2_INCLUDE_DIR_${SUFFIX} "${BZIP2_PREFIX_${SUFFIX}}/include")
  set(BZIP2_LIBRARY_FILE_${SUFFIX} "${BZIP2_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}bz2${CMAKE_STATIC_LIBRARY_SUFFIX}")

  set(BZIP2_CFLAGS_${SUFFIX} "-O2 -w -D_FILE_OFFSET_BITS=64")
  if(PIC)
    string(APPEND BZIP2_CFLAGS_${SUFFIX} " -fPIC")
  endif(PIC)

  set(BZIP2_MAKE_VARS_${SUFFIX} "CC=${CMAKE_C_COMPILER}" "AR=${CMAKE_AR}" "RANLIB=${CMAKE_RANLIB}"
                                "CFLAGS=${BZIP2_CFLAGS_${SUFFIX}}")

  ExternalProject_Add(
    bzip2_${SUFFIX}
    URL https://sourceware.org/pub/bzip2/bzip2-1.0.8.tar.gz
    DOWNLOAD_DIR ${BINARY}/downloads
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    BUILD_IN_SOURCE TRUE
    CONFIGURE_COMMAND ""
    BUILD_COMMAND make libbz2.a ${BZIP2_MAKE_VARS_${SUFFIX}}
    INSTALL_COMMAND make install PREFIX=${BZIP2_PREFIX_${SUFFIX}} ${BZIP2_MAKE_VARS_${SUFFIX}}
    BUILD_BYPRODUCTS "${BZIP2_LIBRARY_FILE_${SUFFIX}}")
endmacro(build_bzip2)
