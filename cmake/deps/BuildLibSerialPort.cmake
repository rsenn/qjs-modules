# build_libserialport(SOURCE BINARY SUFFIX PIC)
#
# Builds the vendored libserialport submodule once per SUFFIX ("shared"/"static"),
# each with its own PIC setting -- a shared qjs-serial (.so) module needs a PIC copy of
# libserialport to link against; a static qjs-serial-static (.a) one doesn't. Building
# both from the same non-PIC-by-default single archive (the old, single-build approach)
# risks relocation errors when that archive ends up linked into the .so.
macro(build_libserialport SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building libserialport from source (${SUFFIX}, PIC=${PIC})")

  set(_sp_flags "${MODULE_COMPILE_FLAGS}")
  set(_sp_msys_args "")
  if(MSYS OR CYGWIN OR CMAKE_SYSTEM_NAME MATCHES "MSYS|CYGWIN" OR CMAKE_C_COMPILER_TARGET MATCHES "msys|cygwin" OR CMAKE_C_COMPILER MATCHES "msys")
    # POSIX build, no windows.c: see libserialport-msys.cmake
    set(_sp_msys_args "-DCMAKE_PROJECT_libserialport_INCLUDE:FILEPATH=${SOURCE}/cmake/deps/libserialport-msys.cmake")
  endif()

  toolchain_cache_args(_sp_cross)

  # its CMakeLists writes config.h into the source directory: two builds configuring at once race
  # on that file, so each one waits for the one defined before it
  set(_sp_after)
  if(LIBSERIALPORT_PREVIOUS_TARGET)
    set(_sp_after DEPENDS ${LIBSERIALPORT_PREVIOUS_TARGET})
  endif()
  set(LIBSERIALPORT_PREVIOUS_TARGET libserialport_${SUFFIX})

  ExternalProject_Add(
    libserialport_${SUFFIX}
    ${_sp_after}
    SOURCE_DIR ${SOURCE}/third_party/libserialport
    BINARY_DIR ${BINARY}/libserialport-${SUFFIX}
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_LIBDIR:PATH=${CMAKE_INSTALL_LIBDIR}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=${_sp_flags}"
      ${_sp_msys_args}
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      ${_sp_cross}
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DNO_PUBLIC_API:BOOL=TRUE"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
    CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE" "-DNO_PUBLIC_API:BOOL=TRUE"
    INSTALL_COMMAND cmake -E echo "Skipping install step.")

  ExternalProject_Get_Property(libserialport_${SUFFIX} BINARY_DIR)

  set(LIBSERIALPORT_LIBRARY_DIR_${SUFFIX} "${BINARY_DIR}" CACHE PATH "libserialport ${SUFFIX} library directory" FORCE)
  set(LIBSERIALPORT_LIBRARY_FILE_${SUFFIX}
      "${BINARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}serialport${CMAKE_STATIC_LIBRARY_SUFFIX}")

  add_library(Serial::Port::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(Serial::Port::${SUFFIX} libserialport_${SUFFIX})
  set_target_properties(Serial::Port::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${LIBSERIALPORT_LIBRARY_FILE_${SUFFIX}}"
                                                           IMPORTED_IMPLIB "${LIBSERIALPORT_LIBRARY_FILE_${SUFFIX}}")

  set(LIBSERIALPORT_LIBRARY_${SUFFIX} Serial::Port::${SUFFIX})
endmacro()
