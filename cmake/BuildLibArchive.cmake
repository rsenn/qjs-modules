# Emscripten: CMake's check_type_size() reads the sizes back out of a compiled .js file and gets 7 for every
# type; the sub-builds of lzo2 and libarchive use cmake/emscripten/CheckTypeSize.cmake instead.
if(EMSCRIPTEN)
  set(EMSCRIPTEN_MODULE_PATH_ARGS "-DCMAKE_MODULE_PATH:PATH=${CMAKE_CURRENT_LIST_DIR}/emscripten")
else()
  set(EMSCRIPTEN_MODULE_PATH_ARGS "")
endif()

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

# one switch per codec; off drops the codec, its library and the builtins that need it
# (the C side sees LIBARCHIVE_NO_<CODEC>)
foreach(codec GZIP BZIP2 LZ4 LZMA ZSTD LZO)
  option(LIBARCHIVE_${codec} "Build libarchive with ${codec} support" ON)
endforeach()

# build_libarchive(SOURCE BINARY SUFFIX PIC)
#
# Builds the vendored libarchive submodule once per SUFFIX ("shared"/"static"), each
# with its own PIC setting -- see BuildLibSerialPort.cmake for why a single non-PIC
# build isn't safe to link into both a shared qjs-archive (.so) and a static
# qjs-archive-static (.a) module.
macro(build_libarchive SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building libarchive from source (${SUFFIX}, PIC=${PIC})")

  if(LIBARCHIVE_BZIP2 AND NOT DEFINED BZIP2_FOUND)
    find_bzip2()
  endif()

  if(NOT DEFINED LIBB2_FOUND)
    find_libb2()
  endif()

  if(LIBARCHIVE_LZMA AND NOT DEFINED LIBLZMA_FOUND)
    find_liblzma()
  endif()

  if(LIBARCHIVE_LZ4 AND NOT DEFINED LZ4_FOUND)
    find_liblz4()
  endif()

  if(LIBARCHIVE_LZO AND NOT DEFINED LZO2_FOUND)
    find_lzo2()
  endif()

  if(LIBARCHIVE_GZIP AND NOT DEFINED ZLIB_FOUND)
    find_zlib()
  endif()

  if(LIBARCHIVE_ZSTD AND NOT DEFINED ZSTD_FOUND)
    find_zstd()
  endif()

  set(LIBARCHIVE_DEPS_${SUFFIX} "")
  set(LIBARCHIVE_DEP_ARGS_${SUFFIX} "")

  if(NOT LIBARCHIVE_BZIP2)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_BZip2:BOOL=OFF")
  elseif(NOT BZIP2_FOUND)
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

  if(NOT LIBARCHIVE_LZMA)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_LZMA:BOOL=OFF")
  elseif(NOT LIBLZMA_FOUND)
    build_liblzma(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} liblzma_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLIBLZMA_INCLUDE_DIR:PATH=${LIBLZMA_INCLUDE_DIR_${SUFFIX}}"
         "-DLIBLZMA_LIBRARY_RELEASE:FILEPATH=${LIBLZMA_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBARCHIVE_LZ4)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_LZ4:BOOL=OFF")
  elseif(NOT LZ4_FOUND)
    build_liblz4(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} lz4_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLZ4_INCLUDE_DIR:PATH=${LZ4_INCLUDE_DIR_${SUFFIX}}"
         "-DLZ4_LIBRARY:FILEPATH=${LZ4_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBARCHIVE_LZO)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_LZO:BOOL=OFF")
  elseif(NOT LZO2_FOUND)
    build_lzo2(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} lzo2_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DLZO2_INCLUDE_DIR:PATH=${LZO2_INCLUDE_DIR_${SUFFIX}}"
         "-DLZO2_LIBRARY:FILEPATH=${LZO2_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBARCHIVE_GZIP)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_ZLIB:BOOL=OFF")
  elseif(NOT ZLIB_FOUND)
    build_zlib(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} zlib_${SUFFIX})
    # zlib installs a shared copy next to the static one, so FindZLIB must be told which to pick.
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DZLIB_INCLUDE_DIR:PATH=${ZLIB_INCLUDE_DIR_${SUFFIX}}"
         "-DZLIB_LIBRARY:FILEPATH=${ZLIB_LIBRARY_FILE_${SUFFIX}}"
         "-DZLIB_LIBRARY_RELEASE:FILEPATH=${ZLIB_LIBRARY_FILE_${SUFFIX}}")
  endif()

  if(NOT LIBARCHIVE_ZSTD)
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_ZSTD:BOOL=OFF")
  elseif(NOT ZSTD_FOUND)
    build_zstd(${BINARY} ${SUFFIX} ${PIC})
    list(APPEND LIBARCHIVE_DEPS_${SUFFIX} zstd_${SUFFIX})
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DZSTD_INCLUDE_DIR:PATH=${ZSTD_INCLUDE_DIR_${SUFFIX}}"
         "-DZSTD_LIBRARY:FILEPATH=${ZSTD_LIBRARY_FILE_${SUFFIX}}")
  endif()

  set(LIBARCHIVE_C_FLAGS "-w")

  # Host-detected libs resolve to glibc headers via -I/usr/include, which breaks libarchive's configure checks under musl-gcc.
  if(CMAKE_C_COMPILER MATCHES "musl" OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    list(APPEND LIBARCHIVE_DEP_ARGS_${SUFFIX} "-DENABLE_OPENSSL:BOOL=OFF" "-DENABLE_EXPAT:BOOL=OFF"
         "-DENABLE_LIBXML2:BOOL=OFF" "-DENABLE_ICONV:BOOL=OFF" "-DENABLE_PCREPOSIX:BOOL=OFF")
  endif()

  # WASI has no ACLs, extended attributes or host crypto, and no tools are built
  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    list(
      APPEND
      LIBARCHIVE_DEP_ARGS_${SUFFIX}
      "-DENABLE_ACL:BOOL=OFF"
      "-DENABLE_XATTR:BOOL=OFF"
      "-DENABLE_NETTLE:BOOL=OFF"
      "-DENABLE_MBEDTLS:BOOL=OFF"
      "-DENABLE_PCRE2POSIX:BOOL=OFF"
      "-DENABLE_TAR:BOOL=OFF"
      "-DENABLE_CPIO:BOOL=OFF"
      "-DENABLE_CAT:BOOL=OFF"
      "-DENABLE_UNZIP:BOOL=OFF"
      # src/wasi/wasi_compat.h supplies these, so the link-only probes cannot see them
      "-DHAVE_FCHDIR:INTERNAL=1"
      "-DHAVE_GETPWUID_R:INTERNAL=1"
      "-DHAVE_GETGRGID_R:INTERNAL=1")
    set(LIBARCHIVE_C_FLAGS
        "-w -I${SOURCE}/src/wasi -include ${SOURCE}/src/wasi/wasi_compat.h -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS -D_WASI_EMULATED_MMAN"
    )
    list(
      APPEND
      LIBARCHIVE_DEP_ARGS_${SUFFIX}
      "-DCMAKE_EXE_LINKER_FLAGS:STRING=-lwasi-emulated-signal -lwasi-emulated-getpid -lwasi-emulated-process-clocks -lwasi-emulated-mman"
    )
  endif()

  ExternalProject_Add(
    libarchive_${SUFFIX}
    SOURCE_DIR ${SOURCE}/third_party/libarchive
    BINARY_DIR ${BINARY}/libarchive-${SUFFIX}
    DEPENDS ${LIBARCHIVE_DEPS_${SUFFIX}}
    CMAKE_CACHE_ARGS
      ${LIBARCHIVE_DEP_ARGS_${SUFFIX}}
      "-DENABLE_TEST:BOOL=OFF"
      ${EMSCRIPTEN_MODULE_PATH_ARGS}
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_C_FLAGS:STRING=${LIBARCHIVE_C_FLAGS}"
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
