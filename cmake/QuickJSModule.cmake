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

function(compile_module SOURCE)
  basename(BASE "${SOURCE}" .js)
  message(STATUS "Compile QuickJS module '${BASE}.c' from '${SOURCE}'")

  set(MODULES_DIR "${CMAKE_BINARY_DIR}/modules")
  set(MODULES_DIR "${MODULES_DIR}" PARENT_SCOPE)

  if(ARGN)
    set(OUTPUT_FILE ${ARGN})
  else(ARGN)
    set(OUTPUT_FILE "${MODULES_DIR}/${BASE}.c")
  endif(ARGN)

  list(APPEND COMPILED_MODULES "${BASE}.c")
  set(COMPILED_MODULES "${COMPILED_MODULES}" PARENT_SCOPE)

  #add_custom_command(OUTPUT "${OUTPUT_FILE}" COMMAND qjsc -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" DEPENDS ${QJSC_DEPS} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler" SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE} DEPENDS qjs-inspect qjs-misc)
  add_custom_target("${BASE}.c" BYPRODUCTS "${OUTPUT_FILE}" COMMAND "${QJSC}" -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" DEPENDS ${QJSC_DEPS} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler" SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" #DEPENDS qjs-inspect qjs-misc
  )
endfunction(compile_module SOURCE)

function(make_module FNAME)
  #message(STATUS "Building QuickJS module: ${FNAME}")
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(REGEX REPLACE "-" "_" VNAME "${FNAME}")
  string(TOUPPER "${FNAME}" UUNAME)
  string(REGEX REPLACE "-" "_" UNAME "${UUNAME}")

  set(TARGET_NAME qjs-${NAME})

  if(ARGN)
    set(SOURCES ${ARGN} ${${VNAME}_SOURCES})
    set(DEPS ${ARGN} ${${VNAME}_DEPS})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES})
  endif(ARGN)

  #dump(VNAME ${VNAME}_SOURCES SOURCES)

  if(BUILD_SHARED_MODULES)
    add_library(${TARGET_NAME} SHARED ${SOURCES})

    set_target_properties(${TARGET_NAME} PROPERTIES RPATH "${MBEDTLS_LIBRARY_DIR}" PREFIX "" OUTPUT_NAME "${VNAME}" COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(${TARGET_NAME} PRIVATE _GNU_SOURCE=1 JS_SHARED_LIBRARY=1 JS_${UNAME}_MODULE=1 CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}")

    target_link_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${TARGET_NAME} PUBLIC quickjs)

    #message("C module dir: ${QUICKJS_C_MODULE_DIR}")
    install(TARGETS ${TARGET_NAME} DESTINATION "${QUICKJS_C_MODULE_DIR}" PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    config_module(${TARGET_NAME})

    set(LIBRARIES ${${VNAME}_LIBRARIES})
    if(LIBRARIES)
      target_link_libraries(${TARGET_NAME} PRIVATE ${LIBRARIES})
    endif(LIBRARIES)
    if(DEPS)
      add_dependencies(${TARGET_NAME} ${DEPS})
    endif(DEPS)

  endif(BUILD_SHARED_MODULES)

  add_library(${TARGET_NAME}-static STATIC ${SOURCES})

  set(MODULES_STATIC "${QJS_MODULES_STATIC}")
  list(APPEND MODULES_STATIC "${TARGET_NAME}-static")
  set(QJS_MODULES_STATIC "${MODULES_STATIC}" PARENT_SCOPE)

  set_target_properties(${TARGET_NAME}-static PROPERTIES OUTPUT_NAME "${VNAME}" COMPILE_FLAGS "")
  target_compile_definitions(${TARGET_NAME}-static PRIVATE _GNU_SOURCE=1 JS_${UNAME}_MODULE=1 CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}")
  target_link_directories(${TARGET_NAME}-static PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(${TARGET_NAME}-static PUBLIC quickjs)

endfunction()
