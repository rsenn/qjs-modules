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

  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel
                                               RelWithDebInfo)

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

  if(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
    set(QUICKJS_LIBRARY_DIR "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
        CACHE PATH "QuickJS library directory")
  endif(EXISTS "${QUICKJS_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

  set(QUICKJS_INCLUDE_DIR "${QUICKJS_INCLUDE_DIR}"
      CACHE PATH "QuickJS include directory")
  set(QUICKJS_LIBRARY_DIR "${QUICKJS_LIBRARY_DIR}"
      CACHE PATH "QuickJS library directory")

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

  set(MODULE_PATH "${QUICKJS_C_MODULE_DIR}")
  if(NOT "${QUICKJS_C_MODULE_DIR}" STREQUAL "${QUICKJS_JS_MODULE_DIR}")
    set(MODULE_PATH "${MODULE_PATH}:${QUICKJS_JS_MODULE_DIR}")
  endif(NOT "${QUICKJS_C_MODULE_DIR}" STREQUAL "${QUICKJS_JS_MODULE_DIR}")

  string(REPLACE ";" ":" MODULE_PATH "${MODULE_PATH}")
  set(QUICKJS_MODULE_PATH "${MODULE_PATH}" CACHE PATH
                                                 "QuickJS modules search path")

  message(STATUS "QuickJS configuration")
  message(STATUS "\tinterpreter: ${QJS}")
  message(STATUS "\tcompiler: ${QJSC}")
  message(STATUS "\tinstall directory: ${QUICKJS_PREFIX}")
  message(STATUS "\tlibrary directory: ${QUICKJS_LIBRARY_DIR}")
  message(STATUS "\tinclude directory: ${QUICKJS_INCLUDE_DIR}")
  #message(STATUS "\tC module directory: ${QUICKJS_C_MODULE_DIR}")
  message(STATUS "\tmodule search path: ${QUICKJS_MODULE_PATH}")

endmacro(configure_quickjs)

if(NOT QUICKJS_JS_MODULE_DIR)
  set(QUICKJS_JS_MODULE_DIR "${QUICKJS_PREFIX}/lib/quickjs"
      CACHE PATH "QuickJS JS module directory")
endif(NOT QUICKJS_JS_MODULED_DIR)

set(MODULES_DIR "${CMAKE_BINARY_DIR}/modules")
file(MAKE_DIRECTORY "${MODULES_DIR}")
