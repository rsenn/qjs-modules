# build_cyaml(SOURCE BINARY SUFFIX PIC)
#
# Builds CYAML from its git repository (third_party/cyaml, cloned on first use) as a static
# library, once per SUFFIX ("shared"/"static") with its own PIC setting. Tests, tools, the
# shared library and LTO are off: an LTO archive cannot be linked into a module .so by another
# compiler version.
#
# Sets CYAML_INCLUDE_DIR_${SUFFIX} and CYAML_LIBRARIES_${SUFFIX} (the archive, then libm);
# the ExternalProject target is cyaml_${SUFFIX}. Used when BUILD_CYAML is ON.
set(CYAML_GIT_REPOSITORY "https://github.com/andrewmd5/cyaml.git" CACHE STRING "CYAML git repository")
set(CYAML_GIT_TAG "main" CACHE STRING "CYAML git tag or branch to build")

macro(build_cyaml SOURCE BINARY SUFFIX PIC)
  include(ExternalProject)

  message(STATUS "Building cyaml from git (${SUFFIX}, PIC=${PIC})")

  set(CYAML_SOURCE_DIR "${SOURCE}/third_party/cyaml")
  set(CYAML_PREFIX_${SUFFIX} "${BINARY}/deps-cyaml-${SUFFIX}")
  set(CYAML_INCLUDE_DIR_${SUFFIX} "${CYAML_PREFIX_${SUFFIX}}/include")
  set(CYAML_LIBRARY_FILE_${SUFFIX}
      "${CYAML_PREFIX_${SUFFIX}}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}cyaml${CMAKE_STATIC_LIBRARY_SUFFIX}")

  # one clone for both variants, skipped when third_party/cyaml is already checked out
  if(NOT TARGET cyaml_source)
    if(EXISTS "${CYAML_SOURCE_DIR}/CMakeLists.txt")
      add_custom_target(cyaml_source)
    else()
      ExternalProject_Add(
        cyaml_source
        GIT_REPOSITORY ${CYAML_GIT_REPOSITORY}
        GIT_TAG ${CYAML_GIT_TAG}
        GIT_SHALLOW TRUE
        SOURCE_DIR ${CYAML_SOURCE_DIR}
        UPDATE_COMMAND ""
        CONFIGURE_COMMAND ""
        BUILD_COMMAND ""
        INSTALL_COMMAND "")
    endif()
  endif()

  # dietlibc has no NAN, INFINITY, locale_t or strtod_l(); -w keeps the library's -Werror quiet
  set(CYAML_C_FLAGS "")
  if(DIET OR HAVE_DIETLIBC)
    set(CYAML_C_FLAGS "-w -D_GNU_SOURCE -include ${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/diet-compat.h")
  endif()

  toolchain_cache_args(_DEPS_CROSS_ARGS)

  ExternalProject_Add(
    cyaml_${SUFFIX}
    DEPENDS cyaml_source
    SOURCE_DIR ${CYAML_SOURCE_DIR}
    BINARY_DIR ${BINARY}/cyaml-${SUFFIX}
    DOWNLOAD_COMMAND ""
    UPDATE_COMMAND ""
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_PREFIX:PATH=${CYAML_PREFIX_${SUFFIX}}"
      "-DCMAKE_INSTALL_LIBDIR:PATH=lib"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      ${_DEPS_CROSS_ARGS}
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DCMAKE_C_FLAGS:STRING=${CYAML_C_FLAGS}"
      "-DCMAKE_POSITION_INDEPENDENT_CODE:BOOL=${PIC}"
      "-DCYAML_BUILD_STATIC:BOOL=ON"
      "-DCYAML_BUILD_SHARED:BOOL=OFF"
      "-DCYAML_BUILD_TESTS:BOOL=OFF"
      "-DCYAML_ENABLE_LTO:BOOL=OFF"
    BUILD_BYPRODUCTS "${CYAML_LIBRARY_FILE_${SUFFIX}}")

  add_library(CYAML::${SUFFIX} STATIC IMPORTED GLOBAL)
  add_dependencies(CYAML::${SUFFIX} cyaml_${SUFFIX})
  set_target_properties(CYAML::${SUFFIX} PROPERTIES IMPORTED_LOCATION "${CYAML_LIBRARY_FILE_${SUFFIX}}")

  # an archive carries no dependencies: the float formatting needs libm
  set(CYAML_LIBRARIES_${SUFFIX} CYAML::${SUFFIX} m)
endmacro()
