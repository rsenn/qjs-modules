include(cmake/FindBZip2.cmake)
include(cmake/FindLibB2.cmake)
include(cmake/FindLibLZMA.cmake)
include(cmake/FindLz4.cmake)
include(cmake/FindLzo2.cmake)
include(cmake/FindZlib.cmake)
include(cmake/FindZstd.cmake)

include(cmake/BuildBZip2.cmake)
include(cmake/BuildLibB2.cmake)
include(cmake/BuildLibLZMA.cmake)
include(cmake/BuildLz4.cmake)
include(cmake/BuildLzo2.cmake)
include(cmake/BuildZlib.cmake)
include(cmake/BuildZstd.cmake)

# build_libarchive(SOURCE BINARY SUFFIX PIC)
#
# Builds the vendored libarchive submodule once per SUFFIX ("shared"/"static"), each
# with its own PIC setting -- see BuildLibSerialPort.cmake for why a single non-PIC
# build isn't safe to link into both a shared qjs-archive (.so) and a static
# qjs-archive-static (.a) module.
macro(build_libarchive SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building libarchive from source (${SUFFIX}, PIC=${PIC})")

  if(NOT DEFINED BZIP2_FOUND)
    find_bzip2()
  endif()

  if(NOT DEFINED LIBB2_FOUND)
    find_libb2()
  endif()

  if(NOT DEFINED LIBLZMA_FOUND)
    find_liblzma()
  endif()

  if(NOT DEFINED LZ4_FOUND)
    find_liblz4()
  endif()

  if(NOT DEFINED LZO2_FOUND)
    find_lzo2()
  endif()

  if(NOT DEFINED ZLIB_FOUND)
    find_zlib()
  endif()

  if(NOT DEFINED ZSTD_FOUND)
    find_zstd()
  endif()

  set(LIBARCHIVE_DEPS_${SUFFIX} "")
  set(LIBARCHIVE_DEP_ARGS_${SUFFIX} "")

  if(NOT BZIP2_FOUND)
    build_bzip2(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} bzip2_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DBZIP2_INCLUDE_DIR:PATH=${BZIP2_INCLUDE_DIR_${SUFFIX}}"
         "-DBZIP2_LIBRARY_RELEASE:FILEPATH=${BZIP2_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBB2_FOUND)
    build_libb2(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} libb2_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLIBB2_INCLUDE_DIR:PATH=${LIBB2_INCLUDE_DIR_${SUFFIX}}"
         "-DLIBB2_LIBRARY:FILEPATH=${LIBB2_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBLZMA_FOUND)
    build_liblzma(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} liblzma_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLIBLZMA_INCLUDE_DIR:PATH=${LIBLZMA_INCLUDE_DIR_${SUFFIX}}"
         "-DLIBLZMA_LIBRARY_RELEASE:FILEPATH=${LIBLZMA_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LZ4_FOUND)
    build_liblz4(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} lz4_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLZ4_INCLUDE_DIR:PATH=${LZ4_INCLUDE_DIR_${SUFFIX}}"
         "-DLZ4_LIBRARY:FILEPATH=${LZ4_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LZO2_FOUND)
    build_lzo2(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} lzo2_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLZO2_INCLUDE_DIR:PATH=${LZO2_INCLUDE_DIR_${SUFFIX}}"
         "-DLZO2_LIBRARY:FILEPATH=${LZO2_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT ZLIB_FOUND)
    build_zlib(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} zlib_${SUFFIX})
    # zlib installs a shared copy next to the static one, so FindZLIB must be told which to pick.
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DZLIB_INCLUDE_DIR:PATH=${ZLIB_INCLUDE_DIR_${SUFFIX}}"
         "-DZLIB_LIBRARY:FILEPATH=${ZLIB_LIBRARY_FILE_${SUFFIX}}"
         "-DZLIB_LIBRARY_RELEASE:FILEPATH=${ZLIB_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT ZSTD_FOUND)
    build_zstd(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} zstd_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DZSTD_INCLUDE_DIR:PATH=${ZSTD_INCLUDE_DIR_${SUFFIX}}"
         "-DZSTD_LIBRARY:FILEPATH=${ZSTD_LIBRARY_FILE_${SUFFIX}}")
  endif()

  # Host-detected libs resolve to glibc headers via -I/usr/include, which breaks libarchive's configure checks under musl-gcc.
  if(CMAKE_C_COMPILER MATCHES "musl")
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_OPENSSL:BOOL=OFF" "-DENABLE_EXPAT:BOOL=OFF"
         "-DENABLE_LIBXML2:BOOL=OFF" "-DENABLE_ICONV:BOOL=OFF" "-DENABLE_PCREPOSIX:BOOL=OFF")
  endif()

  ExternalProject_Add(
    libarchive_${SUFFIX}
    SOURCE_DIR ${SOURCE}/third_party/libarchive
    BINARY_DIR ${BINARY}/libarchive-${SUFFIX}
    DEPENDS ${LIBARCHIVE_DEPS_${SUFFIX}}
    CMAKE_CACHE_ARGS
      ${LIBARCHIVE_DEP_ARGS_${SUFFIX}}
      "-DENABLE_LZO:BOOL=ON"
      "-DENABLE_TEST:BOOL=OFF"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=-w"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    CMAKE_CACHE_DEFAULT_ARGS "-DENABLE_TEST:BOOL=OFF" "-DBUILD_SHARED_LIBS:BOOL=FALSE"
    INSTALL_COMMAND "")

  ExternalProject_Get_Property(libarchive_${SUFFIX} BINARY_DIR)

  set(LIBARCHIVE_LIBRARY_DIR_${SUFFIX} "${BINARY_DIR}" CACHE PATH "libarchive ${SUFFIX} library directory" FORCE)
  set(LIBARCHIVE_LIBRARY_FILE_${SUFFIX}
      "${BINARY_DIR}/libarchive/${CMAKE_STATIC_LIBRARY_PREFIX}archive${CMAKE_STATIC_LIBRARY_SUFFIX}")

  add_library(LibArchive::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibArchive::${SUFFIX} libarchive_${SUFFIX})
  set_target_properties(LibArchive::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBARCHIVE_LIBRARY_FILE_${SUFFIX}}")

  # libarchive is a static archive, so every codec it was configured with has to be linked too.
  set(LibArchive_LIBRARIES_${SUFFIX} LibArchive::${SUFFIX})
  foreach(dep BZIP2 LIBB2 LIBLZMA LZ4 LZO2 ZLIB ZSTD)
    if(${dep}_FOUND)
      list(APPEND LibArchive_LIBRARIES_${SUFFIX} ${${dep}_LIBRARIES})
    else()
      list(APPEND LibArchive_LIBRARIES_${SUFFIX} ${${dep}_LIBRARY_FILE_${SUFFIX}})
    endif()
  endforeach(dep)
  set(LibArchive_INCLUDE_DIRS_${SUFFIX} "${SOURCE}/third_party/libarchive/libarchive")
endmacro(build_libarchive)
