macro(find_quickjs)
  include(CheckIncludeFile)

  if(ARGN)
    if(EXISTS "${ARGN}")
      set(QUICKJS_PREFIX "${ARGN}")
    endif(EXISTS "${ARGN}")
  endif(ARGN)

  if(NOT QUICKJS_PREFIX)
    find_file(
      QUICKJS_H quickjs.h
      PATHS "${CMAKE_INSTALL_PREFIX}/inclue/quickjs"
            "/usr/local/include/quickjs" "/usr/include/quickjs"
            "${QUICKJS_ROOT}/include/quickjs" "${QuickJS_DIR}/include/quickjs")

    if(QUICKJS_H)
      message("QuickJS header: ${QUICKJS_H}")
      string(REGEX REPLACE "/include.*" "" QUICKJS_PREFIX "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_PREFIX)

  if(NOT QUICKJS_PREFIX)
    set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}"
        CACHE PATH "QuickJS install directory")
  endif(NOT QUICKJS_PREFIX)

  if(NOT "${CMAKE_SYSROOT}" STREQUAL "")
    string(REPLACE "${CMAKE_SYSROOT}" "" QUICKJS_INSTALL_DIR
                   "${QUICKJS_PREFIX}")
  endif(NOT "${CMAKE_SYSROOT}" STREQUAL "")

  if("${QUICKJS_INSTALL_DIR}" STREQUAL "")
    set(QUICKJS_INSTALL_DIR "${QUICKJS_PREFIX}")
  endif("${QUICKJS_INSTALL_DIR}" STREQUAL "")

  set(QUICKJS_INSTALL_PREFIX "${QUICKJS_INSTALL_DIR}"
      CACHE PATH "QuickJS installation prefix")

  #dump(QUICKJS_INSTALL_PREFIX)

  # set(CMAKE_INSTALL_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "Install directory")

  if(CACHE{CMAKE_BUILD_TYPE})
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release
                                                 MinSizeRel RelWithDebInfo)
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

  #  if(NOT EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs.h")
  #    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs.h")
  #      set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/..")
  #    else(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs.h")
  #      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
  #        file(RELATIVE_PATH QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/..")
  #        set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
  #      else(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
  #        if(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
  #          set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include/quickjs")
  #        endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
  #      endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
  #    endif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
  #  endif(NOT EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs.h")
  if(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "")
    set(CMAKE_INSTALL_LIBDIR "lib")
  endif(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "")

  if(NOT QUICKJS_INCLUDE_DIR)
    if(QUICKJS_LIBRARY_DIR)
      string(REGEX REPLACE "/lib.*" "/include/quickjs" QUICKJS_INCLUDE_DIR
                           "${QUICKJS_LIBRARY_DIR}")
    endif(QUICKJS_LIBRARY_DIR)
  endif(NOT QUICKJS_INCLUDE_DIR)

  if(NOT QUICKJS_INCLUDE_DIR)
    if(QUICKJS_H)
      string(REGEX REPLACE "/include.*" "/include/quickjs" QUICKJS_INCLUDE_DIR
                           "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_INCLUDE_DIR)

  if(QUICKJS_INCLUDE_DIR)
    message("QuickJS include dir: ${QUICKJS_INCLUDE_DIR}")
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}"
        CACHE PATH "QuickJS include directory")
    link_directories(${QUICKJS_INCLUDE_DIR})
  endif(QUICKJS_INCLUDE_DIR)

  if(NOT QUICKJS_LIBRARY_DIR)
    if(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
      set(QUICKJS_LIBRARY_DIR "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
          CACHE PATH "QuickJS library directory")
    endif(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
  endif(NOT QUICKJS_LIBRARY_DIR)

  if(NOT QUICKJS_LIBRARY_DIR)
    if(QUICKJS_H)
      string(REGEX REPLACE "/include.*" "/${CMAKE_INSTALL_LIBDIR}"
                           QUICKJS_LIBRARY_DIR "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_LIBRARY_DIR)

  if(QUICKJS_LIBRARY_DIR)
    message("QuickJS library dir: ${QUICKJS_LIBRARY_DIR}")
    set(QUICKJS_LIBRARY_DIR "${QUICKJS_LIBRARY_DIR}"
        CACHE PATH "QuickJS library directory")
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
    link_directories(${QUICKJS_BUILD_ROOT})
    set(QUICKJS_LIBRARY quickjs)
    set(QUICKJS_LIBRARY_DIR "${QUICKJS_BUILD_ROOT}")
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_SOURCES_ROOT};${QUICKJS_BUILD_ROOT}")
  else(QUICKJS_BUILD_ROOT)
    link_directories(${QUICKJS_LIBRARY_DIR})
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
    message(STATUS "QUICKJS_LIBRARY: ${QUICKJS_LIBRARY} ")
  endif(NOT QUICKJS_LIBRARY)

  if(NOT QUICKJS_INCLUDE_DIRS)
    set(QUICKJS_INCLUDE_DIRS "${QUICKJS_INCLUDE_DIR}")
  endif()

  set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIRS}")

  check_include_file(quickjs.h HAVE_QUICKJS_H)
  check_include_file(quickjs-config.h HAVE_QUICKJS_CONFIG_H)

  if(HAVE_QUICKJS_CONFIG_H)
    # dump(HAVE_QUICKJS_CONFIG_H)
    add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
  endif(HAVE_QUICKJS_CONFIG_H)

  if(NOT HAVE_QUICKJS_H)
    #message(FATAL_ERROR "QuickJS headers not found in ${QUICKJS_INCLUDE_DIR}")
  endif(NOT HAVE_QUICKJS_H)

  include_directories(${QUICKJS_INCLUDE_DIR})

  if(HAVE_QUICKJS_CONFIG_H)
    add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
  endif(HAVE_QUICKJS_CONFIG_H)

  find_program(QJS qjs PATHS "${CMAKE_CURRENT_BINARY_DIR}/.."
                             "${QUICKJS_PREFIX}/bin" ENV PATH NO_DEFAULT_PATH)
  find_program(QJSC qjsc PATHS "${CMAKE_CURRENT_BINARY_DIR}/.."
                               "${QUICKJS_PREFIX}/bin" ENV PATH NO_DEFAULT_PATH)

  set(CUTILS_H ${CMAKE_CURRENT_SOURCE_DIR}/../cutils.h)
  set(QUICKJS_H ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h)
endmacro(find_quickjs)

macro(configure_quickjs)

  if(NOT QUICKJS_PREFIX)
    set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}"
        CACHE PATH "QuickJS install directory")
  endif(NOT QUICKJS_PREFIX)

  if(CMAKE_INSTALL_LIBDIR)
    if(NOT QUICKJS_C_MODULE_DIR)
      set(QUICKJS_C_MODULE_DIR
          "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}/quickjs")
    endif(NOT QUICKJS_C_MODULE_DIR)
  endif(CMAKE_INSTALL_LIBDIR)

  if(NOT QUICKJS_JS_MODULE_DIR)
    set(QUICKJS_JS_MODULE_DIR "${QUICKJS_PREFIX}/lib/quickjs")
  endif(NOT QUICKJS_JS_MODULE_DIR)

  set(QUICKJS_C_MODULE_DIR "${QUICKJS_C_MODULE_DIR}"
      CACHE PATH "QuickJS native C modules directory")
  set(QUICKJS_JS_MODULE_DIR "${QUICKJS_JS_MODULE_DIR}"
      CACHE PATH "QuickJS JavaScript modules directory")

  #variable_watch(QUICKJS_C_MODULE_DIR configure_quickjs_module_path)
  #variable_watch(QUICKJS_JS_MODULE_DIR configure_quickjs_module_path)

  if(NOT QUICKJS_CONFIGURATION_SHOWN)
    message(STATUS "QuickJS configuration")
    message(STATUS "\tinterpreter: ${QJS}")
    message(STATUS "\tcompiler: ${QJSC}")
    message(STATUS "\tlibrary: ${QUICKJS_LIBRARY}")
    message(STATUS "\tinstall directory: ${QUICKJS_PREFIX}")
    message(STATUS "\tlibrary directory: ${QUICKJS_LIBRARY_DIR}")
    message(STATUS "\tinclude directory: ${QUICKJS_INCLUDE_DIR}")
    message(STATUS "\tC module directory: ${QUICKJS_C_MODULE_DIR}")
    message(STATUS "\tJS module directory: ${QUICKJS_JS_MODULE_DIR}")
    set(QUICKJS_CONFIGURATION_SHOWN TRUE)
  endif(NOT QUICKJS_CONFIGURATION_SHOWN)

  configure_quickjs_module_path()
endmacro(configure_quickjs)

macro(configure_quickjs_module_path)
  set(MODULE_PATH "")
  add_unique(MODULE_PATH "${QUICKJS_C_MODULE_DIR}" "${QUICKJS_JS_MODULE_DIR}")

  if(NOT WIN32)
    string(REPLACE ":" ";" MODULE_PATH "${MODULE_PATH}")
  endif(NOT WIN32)

  set(QUICKJS_MODULE_PATH "${MODULE_PATH}" CACHE PATH
                                                 "QuickJS modules search path")

  message(STATUS "\tmodule search path: ${QUICKJS_MODULE_PATH}")
endmacro(configure_quickjs_module_path)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
