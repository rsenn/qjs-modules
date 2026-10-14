# FindQuickJS.cmake: quickjs for qjs-* projects - an installation, a built source
# tree, or one BuildQuickJS.cmake builds.
#
#   find_quickjs([<prefix>])   sets QUICKJS_PREFIX, QUICKJS_INCLUDE_DIR(S), QUICKJS_LIBRARY
#                              (+ _DIR, _STATIC_LIBRARY, _SHARED_LIBRARY), QJS, QJSC
#   configure_quickjs()        QUICKJS_C_MODULE_DIR, QUICKJS_JS_MODULE_DIR, QUICKJS_MODULE_PATH
#
# where quickjs comes from, first match wins:
#   1. a parent project that builds quickjs: QUICKJS_BUILD_ROOT is set (or TARGET quickjs)
#   2. BUILD_QUICKJS=ON, or AUTO and no installation found: BuildQuickJS.cmake builds it
#   3. an installation: <prefix>/include/quickjs/quickjs.h, found or given as <prefix>
#
# from a built source tree (1, 2) headers and libraries come from the build tree and
# QUICKJS_INSTALL_LIBRARY_DIR is the directory they have after install (use it for rpaths).

set(_QUICKJS_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include("${_QUICKJS_CMAKE_DIR}/BuildQuickJS.cmake" OPTIONAL)

#
# quickjs_installed <OUTPUT-VARIABLE> [<prefix>]
#
# Whether an installed quickjs.h exists in the places find_quickjs() searches.
#
function(quickjs_installed OUTVAR)
  set(_prefix "${ARGV1}")
  if(NOT _prefix)
    set(_prefix "${QUICKJS_PREFIX}")
  endif()

  unset(_QUICKJS_PROBE_H CACHE)
  find_file(_QUICKJS_PROBE_H quickjs.h
            PATHS "${_prefix}/include/quickjs" "${_prefix}/include" "${CMAKE_INSTALL_PREFIX}/include/quickjs"
                  "/usr/local/include/quickjs" "/usr/include/quickjs" "${QUICKJS_ROOT}/include/quickjs"
                  "${QuickJS_DIR}/include/quickjs")

  if(_QUICKJS_PROBE_H)
    set(${OUTVAR} TRUE PARENT_SCOPE)
  else()
    set(${OUTVAR} FALSE PARENT_SCOPE)
  endif()

  unset(_QUICKJS_PROBE_H CACHE)
endfunction()

#
# find_quickjs
#
# Builds quickjs when asked to (see the top of this file), then looks it up.
#
macro(find_quickjs)
  string(TOUPPER "${BUILD_QUICKJS}" _qjs_mode)

  if(QUICKJS_BUILD_ROOT OR TARGET quickjs)
    set(_qjs_mode OFF)
  elseif("${_qjs_mode}" STREQUAL "AUTO")
    quickjs_installed(_qjs_have ${ARGN})

    if(_qjs_have)
      set(_qjs_mode OFF)
    else()
      set(_qjs_mode ON)
    endif()
  endif()

  if(_qjs_mode AND NOT "${_qjs_mode}" STREQUAL "OFF")
    if(NOT COMMAND build_quickjs)
      message(FATAL_ERROR "BUILD_QUICKJS needs BuildQuickJS.cmake next to FindQuickJS.cmake")
    endif()

    build_quickjs()
  endif()

  _find_quickjs(${ARGN})
endmacro()

macro(_find_quickjs)
  include(CheckIncludeFile)

  if(ARGN)
    if(EXISTS "${ARGN}")
      set(QUICKJS_PREFIX "${ARGN}")
    endif(EXISTS "${ARGN}")
  endif(ARGN)

  if(NOT QUICKJS_PREFIX)
    find_file(QUICKJS_H quickjs.h
              PATHS "${CMAKE_INSTALL_PREFIX}/include/quickjs" "/usr/local/include/quickjs" "/usr/include/quickjs"
                    "${QUICKJS_ROOT}/include/quickjs" "${QuickJS_DIR}/include/quickjs")

    if(QUICKJS_H)
      string(REGEX REPLACE "/include.*" "" QUICKJS_PREFIX "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_PREFIX)

  if(NOT QUICKJS_PREFIX)
    set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "QuickJS install directory")
  endif(NOT QUICKJS_PREFIX)

  if(NOT "${CMAKE_SYSROOT}" STREQUAL "")
    string(REPLACE "${CMAKE_SYSROOT}" "" QUICKJS_INSTALL_DIR "${QUICKJS_PREFIX}")
  endif(NOT "${CMAKE_SYSROOT}" STREQUAL "")

  if("${QUICKJS_INSTALL_DIR}" STREQUAL "")
    set(QUICKJS_INSTALL_DIR "${QUICKJS_PREFIX}")
  endif("${QUICKJS_INSTALL_DIR}" STREQUAL "")

  set(QUICKJS_INSTALL_PREFIX "${QUICKJS_INSTALL_DIR}" CACHE PATH "QuickJS installation prefix")

  if(CACHE{CMAKE_BUILD_TYPE})
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
  endif(CACHE{CMAKE_BUILD_TYPE})

  set(CMAKE_REQUIRED_QUIET TRUE)

  if(EXISTS "${QUICKJS_PREFIX}/include/quickjs/quickjs.h")
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include/quickjs")
  else(EXISTS "${QUICKJS_PREFIX}/include/quickjs/quickjs.h")

    if(EXISTS "${QUICKJS_PREFIX}/include/quickjs.h")
      set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include")
    else(EXISTS "${QUICKJS_PREFIX}/include/quickjs.h")

      if(EXISTS "${QUICKJS_PREFIX}/quickjs.h")
        set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}")
      endif(EXISTS "${QUICKJS_PREFIX}/quickjs.h")
    endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs.h")
  endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs/quickjs.h")

  if(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "")
    set(CMAKE_INSTALL_LIBDIR "lib")
  endif(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "")

  if(NOT QUICKJS_INCLUDE_DIR)
    if(QUICKJS_LIBRARY_DIR)
      string(REGEX REPLACE "/lib.*" "/include/quickjs" QUICKJS_INCLUDE_DIR "${QUICKJS_LIBRARY_DIR}")
    endif(QUICKJS_LIBRARY_DIR)
  endif(NOT QUICKJS_INCLUDE_DIR)

  if(NOT QUICKJS_INCLUDE_DIR)
    if(QUICKJS_H)
      string(REGEX REPLACE "/include.*" "/include/quickjs" QUICKJS_INCLUDE_DIR "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_INCLUDE_DIR)

  if(QUICKJS_INCLUDE_DIR)
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}" CACHE PATH "QuickJS include directory")
    link_directories(${QUICKJS_INCLUDE_DIR})
  endif(QUICKJS_INCLUDE_DIR)

  if(NOT QUICKJS_LIBRARY_DIR)
    if(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
      set(QUICKJS_LIBRARY_DIR "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}" CACHE PATH "QuickJS library directory")
    endif(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  endif(NOT QUICKJS_LIBRARY_DIR)

  if(NOT QUICKJS_LIBRARY_DIR)
    if(QUICKJS_H)
      string(REGEX REPLACE "/include.*" "/${CMAKE_INSTALL_LIBDIR}" QUICKJS_LIBRARY_DIR "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_LIBRARY_DIR)

  if(QUICKJS_LIBRARY_DIR)
    set(QUICKJS_LIBRARY_DIR "${QUICKJS_LIBRARY_DIR}" CACHE PATH "QuickJS library directory")
    link_directories(${QUICKJS_LIBRARY_DIR})
  endif(QUICKJS_LIBRARY_DIR)

  if(NOT QUICKJS_LIBRARY)
    foreach(LIBNAME libquickjs.a libquickjs.so libquickjs.dll.a)
      if(EXISTS "${QUICKJS_LIBRARY_DIR}/${LIBNAME}")
        set(QUICKJS_LIBRARY "${QUICKJS_LIBRARY_DIR}/${LIBNAME}")
      endif(EXISTS "${QUICKJS_LIBRARY_DIR}/${LIBNAME}")
    endforeach(LIBNAME libquickjs.dll.a libquickjs.a libquickjs.so)
  endif(NOT QUICKJS_LIBRARY)

  if(QUICKJS_BUILD_ROOT)
    # a built tree: the library directory quickjs gets once installed (parent project
    # or BuildQuickJS.cmake) is what an installed binary needs in its rpath
    if(NOT QUICKJS_INSTALL_LIBRARY_DIR AND QUICKJS_LIBRARY_DIR)
      if(IS_ABSOLUTE "${QUICKJS_LIBRARY_DIR}")
        set(QUICKJS_INSTALL_LIBRARY_DIR "${QUICKJS_LIBRARY_DIR}")
      else()
        set(QUICKJS_INSTALL_LIBRARY_DIR "${QUICKJS_PREFIX}/${QUICKJS_LIBRARY_DIR}")
      endif()
    endif()

    link_directories(${QUICKJS_BUILD_ROOT})
    set(QUICKJS_LIBRARY quickjs)

    # a library built by BuildQuickJS.cmake: the full path gives the build tree an rpath
    if(NOT TARGET quickjs)
      foreach(LIBNAME libquickjs.so libquickjs.dylib libquickjs.dll.a libquickjs.a)
        if(EXISTS "${QUICKJS_BUILD_ROOT}/${LIBNAME}")
          set(QUICKJS_LIBRARY "${QUICKJS_BUILD_ROOT}/${LIBNAME}")
          break()
        endif()
      endforeach()
    endif()

    set(QUICKJS_LIBRARY_DIR "${QUICKJS_BUILD_ROOT}")
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_SOURCES_ROOT};${QUICKJS_BUILD_ROOT}")
  else(QUICKJS_BUILD_ROOT)
    link_directories(${QUICKJS_LIBRARY_DIR})

    if(NOT QUICKJS_INSTALL_LIBRARY_DIR)
      set(QUICKJS_INSTALL_LIBRARY_DIR "${QUICKJS_LIBRARY_DIR}")
    endif()
  endif(QUICKJS_BUILD_ROOT)

  set(CMAKE_LIBRARY_PATH "${QUICKJS_LIBRARY_DIR}")

  if(NOT QUICKJS_LIBRARY)
    find_library(LIBQUICKJS quickjs)

    if(EXISTS "${LIBQUICKJS}")
      set(QUICKJS_LIBRARY "${LIBQUICKJS}")
    endif(EXISTS "${LIBQUICKJS}")
  endif(NOT QUICKJS_LIBRARY)

  if(NOT QUICKJS_LIBRARY)
    if(NOT PKG_CONFIG_FOUND)
      include(FindPkgConfig)
    endif(NOT PKG_CONFIG_FOUND)
    pkg_search_module(QUICKJS REQUIRED quickjs)
  endif(NOT QUICKJS_LIBRARY)

  string(REGEX REPLACE "\\.so.*$" ".a" TMP "${QUICKJS_LIBRARY}")

  if(EXISTS "${TMP}")
    set(QUICKJS_STATIC_LIBRARY "${TMP}" CACHE PATH "QuickJS static library (.a)")
  endif()

  unset(TMP)
  set(QUICKJS_SHARED_LIBRARY "${QUICKJS_LIBRARY}" CACHE PATH "QuickJS shared library (.so/.dll/.dylib)")

  unset(QUICKJS_LIBRARY)
  unset(QUICKJS_LIBRARY CACHE)

  if(LINK_STATIC)
    set(QUICKJS_LIBRARY "${QUICKJS_STATIC_LIBRARY}" CACHE PATH "QuickJS library")
  else(LINK_STATIC)
    set(QUICKJS_LIBRARY "${QUICKJS_SHARED_LIBRARY}" CACHE PATH "QuickJS library")
  endif(LINK_STATIC)

  if(NOT QUICKJS_INCLUDE_DIRS)
    set(QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIR}")
  endif()

  set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIRS}")

  check_include_file(quickjs.h HAVE_QUICKJS_H)
  check_include_file(quickjs-config.h HAVE_QUICKJS_CONFIG_H)

  if(HAVE_QUICKJS_CONFIG_H)
    add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
  endif(HAVE_QUICKJS_CONFIG_H)

  include_directories(${QUICKJS_INCLUDE_DIR})

  find_program(QJS qjs PATHS "${QUICKJS_BUILD_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/.." "${QUICKJS_PREFIX}/bin" ENV PATH NO_DEFAULT_PATH)
  find_program(QJSC qjsc PATHS "${QUICKJS_BUILD_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/.." "${QUICKJS_PREFIX}/bin" ENV PATH NO_DEFAULT_PATH)

  # the module shell, for projects that run their tests with it; qjs-modules builds its own
  if(NOT DEFINED QUICKJS_FIND_QJSM)
    set(QUICKJS_FIND_QJSM ON)
  endif(NOT DEFINED QUICKJS_FIND_QJSM)
  if(QUICKJS_FIND_QJSM)
    find_program(QJSM qjsm PATHS "${QUICKJS_BUILD_ROOT}" "${CMAKE_CURRENT_BINARY_DIR}/.." "${QUICKJS_PREFIX}/bin" ENV PATH NO_DEFAULT_PATH)
  endif(QUICKJS_FIND_QJSM)

  set(CUTILS_H ${CMAKE_CURRENT_SOURCE_DIR}/../cutils.h)
  set(QUICKJS_H ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h)
endmacro(_find_quickjs)

macro(configure_quickjs)
  if(NOT QUICKJS_PREFIX)
    message(ERROR "Must have QUICKJS_PREFIX")
    #set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "QuickJS install directory")
  endif(NOT QUICKJS_PREFIX)

  if(NOT CMAKE_INSTALL_LIBDIR)
    set(CMAKE_INSTALL_LIBDIR "lib")
  endif(NOT CMAKE_INSTALL_LIBDIR)

  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE HOST_SYSTEM_NAME
                  OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(NOT QUICKJS_C_MODULE_DIR)
    set(QUICKJS_C_MODULE_DIR "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}/quickjs")
  endif(NOT QUICKJS_C_MODULE_DIR)

  if(NOT QUICKJS_JS_MODULE_DIR)
    set(QUICKJS_JS_MODULE_DIR "${QUICKJS_PREFIX}/lib/quickjs")
  endif(NOT QUICKJS_JS_MODULE_DIR)

  set(QUICKJS_C_MODULE_DIR "${QUICKJS_C_MODULE_DIR}" CACHE PATH "QuickJS native C modules directory")
  set(QUICKJS_JS_MODULE_DIR "${QUICKJS_JS_MODULE_DIR}" CACHE PATH "QuickJS JavaScript modules directory")

  if(NOT QUICKJS_CONFIGURATION_SHOWN)
    configure_quickjs_module_path()

    message_table("QuickJS"
      "interpreter" "${QJS}"
      "compiler" "${QJSC}"
      "library" "${QUICKJS_LIBRARY}"
      "install prefix" "${QUICKJS_PREFIX}"
      "include dir" "${QUICKJS_INCLUDE_DIR}"
      "library dir" "${QUICKJS_LIBRARY_DIR}"
      "C modules" "${QUICKJS_C_MODULE_DIR}"
      "JS modules" "${QUICKJS_JS_MODULE_DIR}"
      "module path" "${QUICKJS_MODULE_PATH}")
    set(QUICKJS_CONFIGURATION_SHOWN TRUE)
  else()
    configure_quickjs_module_path()
  endif(NOT QUICKJS_CONFIGURATION_SHOWN)
endmacro()

macro(configure_quickjs_module_path)
  set(MODULE_PATH "")

  # a multiarch library dir already ends in the system name
  if(NOT "${SYSTEM_NAME}" STREQUAL "" AND NOT "${QUICKJS_LIBRARY_DIR}" MATCHES "/${SYSTEM_NAME}$")
    set_add(MODULE_PATH "${QUICKJS_LIBRARY_DIR}/${SYSTEM_NAME}/quickjs")
  endif()

  set_add(MODULE_PATH "${QUICKJS_C_MODULE_DIR}" "${QUICKJS_JS_MODULE_DIR}")

  if(NOT WIN32)
    string(REPLACE ":" ";" MODULE_PATH "${MODULE_PATH}")
  endif(NOT WIN32)

  set(QUICKJS_MODULE_PATH "${MODULE_PATH}" CACHE PATH "QuickJS modules search path")
endmacro()

# message_table(), set_add() ... come from Functions.cmake, next to this file or one level up
foreach(_dir "${_QUICKJS_CMAKE_DIR}" "${_QUICKJS_CMAKE_DIR}/..")
  if(NOT COMMAND message_table AND EXISTS "${_dir}/Functions.cmake")
    include("${_dir}/Functions.cmake")
  endif()
  if(NOT COMMAND addprefix AND EXISTS "${_dir}/Compat.cmake")
    include("${_dir}/Compat.cmake")
  endif()
endforeach()
