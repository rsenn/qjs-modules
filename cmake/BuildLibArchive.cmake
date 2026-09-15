# build_libarchive(SOURCE BINARY SUFFIX PIC)
#
# Builds the vendored libarchive submodule once per SUFFIX ("shared"/"static"), each
# with its own PIC setting -- see BuildLibSerialPort.cmake for why a single non-PIC
# build isn't safe to link into both a shared qjs-archive (.so) and a static
# qjs-archive-static (.a) module.
macro(build_libarchive SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message("-- Building libarchive from source (${SUFFIX}, PIC=${PIC})")

  ExternalProject_Add(
    libarchive_${SUFFIX}
    SOURCE_DIR ${SOURCE}/third_party/libarchive
    BINARY_DIR ${BINARY}/libarchive-${SUFFIX}
    CMAKE_CACHE_ARGS
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
      "${BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}archive${CMAKE_STATIC_LIBRARY_SUFFIX}")

  add_library(LibArchive::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(LibArchive::${SUFFIX} libarchive_${SUFFIX})
  set_target_properties(LibArchive::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBARCHIVE_LIBRARY_FILE_${SUFFIX}}")

  set(LibArchive_LIBRARIES_${SUFFIX} LibArchive::${SUFFIX})
  set(LibArchive_INCLUDE_DIRS_${SUFFIX} "${SOURCE}/third_party/libarchive/libarchive")
endmacro(build_libarchive)
