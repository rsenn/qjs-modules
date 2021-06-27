macro(find_quickjs)
  include(CheckIncludeFile)

  if(ARGN)
    if(EXISTS "${ARGN}")
      set(QUICKJS_PREFIX "${ARGN}")
    endif(EXISTS "${ARGN}")
  endif(ARGN)

  if(NOT QUICKJS_PREFIX)
    if(EXISTS "${CMAKE_INSTALL_PREFIX}/include/quickjs")
      set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "QuickJS install directory")
    endif(EXISTS "${CMAKE_INSTALL_PREFIX}/include/quickjs")
    if(EXISTS "${CMAKE_INSTALL_PREFIX}/lib/quickjs")
      set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "QuickJS install directory")
    endif(EXISTS "${CMAKE_INSTALL_PREFIX}/lib/quickjs")
  endif(NOT QUICKJS_PREFIX)

  if(NOT QUICKJS_PREFIX)
    find_file(
      QUICKJS_H quickjs.h
      PATHS "${CMAKE_INSTALL_PREFIX}/include/quickjs" "/usr/include/quickjs"
            "/usr/local/include/quickjs" "${QUICKJS_ROOT}/include/quickjs"
            "${QuickJS_DIR}/include/quickjs")

    if(QUICKJS_H)
      message("QuickJS header: ${QUICKJS_H}")
      string(REGEX REPLACE "/include.*" "" QUICKJS_PREFIX "${QUICKJS_H}")
    endif(QUICKJS_H)
  endif(NOT QUICKJS_PREFIX)

  # set(CMAKE_INSTALL_PREFIX "${QUICKJS_PREFIX}" CACHE PATH "Install directory" FORCE)

  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

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

  if(NOT EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs.h")
    if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs.h")
      set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}/..")
    else(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs.h")
      if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
        file(RELATIVE_PATH QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_BINARY_DIR}"
             "${CMAKE_CURRENT_SOURCE_DIR}/..")
        set(QUICKJS_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
      else(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
        if(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
          set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include/quickjs")
        endif(EXISTS "${QUICKJS_PREFIX}/include/quickjs")
      endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
    endif(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
  endif(EXISTS "${QUICKJS_INCLUDE_DIR}/quickjs-config.h")

  if(EXISTS "${QUICKJS_PREFIX}/lib")
    set(QUICKJS_LIBRARY_DIR "${QUICKJS_PREFIX}/lib" CACHE PATH "QuickJS library directory")
  endif(EXISTS "${QUICKJS_PREFIX}/lib/quickjs")

  if(EXISTS "${CMAKE_BINARY_DIR}/quickjs")
    set(QUICKJS_LIBRARY_DIR "${CMAKE_BINARY_DIR}/quickjs" CACHE PATH "QuickJS library directory")
  endif(EXISTS "${CMAKE_BINARY_DIR}/quickjs")
  #[[
  if(NOT QUICKJS_INCLUDE_DIR)
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_PREFIX}/include" CACHE STRING "QuickJS include dirs")
  endif(NOT QUICKJS_INCLUDE_DIR)]]
  if(EXISTS "${QUICKJS_LIBRARY_DIR}/quickjs")
    set(QUICKJS_MODULE_DIR "${QUICKJS_LIBRARY_DIR}/quickjs")
  endif(EXISTS "${QUICKJS_LIBRARY_DIR}/quickjs")

  set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_INCLUDE_DIR}")

  check_include_file(quickjs.h HAVE_QUICKJS_H)
  check_include_file(quickjs-config.h HAVE_QUICKJS_CONFIG_H)

  if(HAVE_QUICKJS_CONFIG_H)
    # dump(HAVE_QUICKJS_CONFIG_H)
    add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
  endif(HAVE_QUICKJS_CONFIG_H)

  if(NOT HAVE_QUICKJS_H)
    message(FATAL_ERROR "QuickJS headers not found in ${QUICKJS_INCLUDE_DIR}")
  endif(NOT HAVE_QUICKJS_H)

  include_directories(${QUICKJS_INCLUDE_DIR})

  if(HAVE_QUICKJS_CONFIG_H)
    add_definitions(-DHAVE_QUICKJS_CONFIG_H=1)
  endif(HAVE_QUICKJS_CONFIG_H)

  find_program(QJS qjs PATHS "${CMAKE_CURRENT_BINARY_DIR}/.." "${QUICKJS_PREFIX}/bin" ENV PATH
               NO_DEFAULT_PATH)
  find_program(QJSC qjsc PATHS "${CMAKE_CURRENT_BINARY_DIR}/.." "${QUICKJS_PREFIX}/bin" ENV PATH
               NO_DEFAULT_PATH)

  message(STATUS "QuickJS interpreter: ${QJS}")
  message(STATUS "QuickJS compiler: ${QJSC}")
  message("QuickJS install directory: ${QUICKJS_PREFIX}")
  message("QuickJS library directory: ${QUICKJS_LIBRARY_DIR}")
  message("QuickJS include directory: ${QUICKJS_INCLUDE_DIR}")
  message("QuickJS module directory: ${QUICKJS_MODULE_DIR}")

  set(CUTILS_H ${CMAKE_CURRENT_SOURCE_DIR}/../cutils.h)
  set(QUICKJS_H ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h)
endmacro(find_quickjs)

function(config_module TARGET_NAME)
  if(QUICKJS_LIBRARY_DIR)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
  endif(QUICKJS_LIBRARY_DIR)
  if(QUICKJS_MODULE_DEPENDENCIES)
    target_link_libraries(${TARGET_NAME} ${QUICKJS_MODULE_DEPENDENCIES})
  endif(QUICKJS_MODULE_DEPENDENCIES)
  if(QUICKJS_MODULE_CFLAGS)
    target_compile_options(${TARGET_NAME} PRIVATE "${QUICKJS_MODULE_CFLAGS}")
  endif(QUICKJS_MODULE_CFLAGS)
endfunction(config_module TARGET_NAME)

function(make_module FNAME)
  message(STATUS "Building QuickJS module: ${FNAME}")
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(REGEX REPLACE "-" "_" VNAME "${FNAME}")
  string(TOUPPER "${FNAME}" UUNAME)
  string(REGEX REPLACE "-" "_" UNAME "${UUNAME}")

  set(TARGET_NAME qjs-${NAME})

  if(ARGN)
    set(SOURCES ${ARGN})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES})
  endif(ARGN)

  # dump(VNAME ${VNAME}_SOURCES)
  add_library(${TARGET_NAME} SHARED ${SOURCES})
  add_library(${TARGET_NAME}-static STATIC ${SOURCES})

  set_target_properties(
    ${TARGET_NAME}
    PROPERTIES
      PREFIX ""
      RPATH "${OPENCV_LIBRARY_DIRS}:${QUICKJS_PREFIX}/lib:${QUICKJS_PREFIX}/lib/quickjs"
      OUTPUT_NAME "${VNAME}"
      BUILD_RPATH
      "${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_CURRENT_BINARY_DIR}:${CMAKE_CURRENT_BINARY_DIR}/quickjs:${CMAKE_CURRENT_BINARY_DIR}/quickjs"
      COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")
  set_target_properties(${TARGET_NAME}-static PROPERTIES OUTPUT_NAME "${VNAME}" COMPILE_FLAGS "")
  target_compile_definitions(${TARGET_NAME} PRIVATE JS_SHARED_LIBRARY=1 JS_${UNAME}_MODULE=1
                                                    CONFIG_PREFIX="${QUICKJS_PREFIX}")
  target_compile_definitions(${TARGET_NAME}-static PRIVATE JS_${UNAME}_MODULE=1
                                                           CONFIG_PREFIX="${QUICKJS_PREFIX}")
  install(TARGETS ${TARGET_NAME} DESTINATION lib/quickjs
          PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ
                      WORLD_EXECUTE)
  # install(TARGETS ${TARGET_NAME}-static DESTINATION lib/quickjs)

  config_module(${TARGET_NAME})
endfunction()
function(compile_module SOURCE)
  basename(BASE "${SOURCE}" .js)
  message(STATUS "Compile QuickJS module '${BASE}.c' from '${SOURCE}'")

  if(ARGN)
    set(OUTPUT_FILE ${ARGN})
  else(ARGN)
    set(OUTPUT_FILE ${BASE}.c)
  endif(ARGN)
  add_custom_command(
    OUTPUT "${OUTPUT_FILE}"
    COMMAND qjsc -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
    DEPENDS ${QJSC_DEPS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler" SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}
    DEPENDS qjs-inspect qjs-misc)

endfunction(compile_module SOURCE)
