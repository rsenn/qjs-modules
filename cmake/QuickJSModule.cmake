function(config_module TARGET_NAME)
  if(QUICKJS_LIBRARY_DIR)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_DIRECTORIES
                                                    "${QUICKJS_LIBRARY_DIR}")
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
  file(MAKE_DIRECTORY "${MODULES_DIR}")

  if(ARGN)
    set(OUTPUT_FILE ${ARGN})
  else(ARGN)
    set(OUTPUT_FILE "${MODULES_DIR}/${BASE}.c")
  endif(ARGN)

  list(APPEND COMPILED_MODULES "${BASE}.c")
  set(COMPILED_MODULES "${COMPILED_MODULES}" PARENT_SCOPE)

  #add_custom_command(OUTPUT "${OUTPUT_FILE}" COMMAND qjsc -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" DEPENDS ${QJSC_DEPS} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler" SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE} DEPENDS qjs-inspect qjs-misc)
  add_custom_target(
    "${BASE}.c" ALL
    BYPRODUCTS "${OUTPUT_FILE}"
    COMMAND "${QJSC}" -v -c -o "${OUTPUT_FILE}" -m
            "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
    DEPENDS ${QJSC_DEPS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler"
    SOURCES
      "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" #DEPENDS qjs-inspect qjs-misc
  )
endfunction(compile_module SOURCE)

function(generate_module_header SOURCE)
  basename(BASE "${SOURCE}" .c)
  string(REGEX REPLACE "\\.c$" ".h" HEADER "${SOURCE}")
  string(REGEX REPLACE "-" "_" NAME "${BASE}")
  #message("generate_module_header SOURCE=${SOURCE}")
  file(READ "${SOURCE}" CSRC)
  string(REGEX MATCHALL "qjsc_[0-9A-Za-z_]+" SYMBOLS "${CSRC}")
  list(FILTER SYMBOLS EXCLUDE REGEX "_size$")
  list(FILTER SYMBOLS EXCLUDE REGEX "^\\s*$")
  string(REGEX REPLACE "qjsc_" "" SYMBOLS "${SYMBOLS}")
  set(S "#include <inttypes.h>\n")
  set(INCLUDES "${ARGN}")
  foreach(INCLUDE ${INCLUDES})
    string(STRIP "${INCLUDE}" INCLUDE)
    string(REGEX REPLACE "_" "-" FNAME "${INCLUDE}")
    if(NOT FNAME MATCHES "\\.h$")
      set(FNAME "${INCLUDE}.h")
    endif(NOT FNAME MATCHES "\\.h$")
    set(S "${S}#include \"${FNAME}\"\n")
  endforeach(INCLUDE ${INCLUDES})
  #message("INCLUDES: ${INCLUDES}")

  foreach(NAME ${SYMBOLS})
    contains(INCLUDES "${NAME}" DOES_CONTAIN)
    #message(" contains(INCLUDES \"${NAME}\" DOES_CONTAIN) = ${DOES_CONTAIN}")
    if(NOT DOES_CONTAIN)
      set(S
          "${S}\nextern const uint32_t qjsc_${NAME}_size;\nextern const uint8_t qjsc_${NAME}[];\n"
      )
    endif(NOT DOES_CONTAIN)
  endforeach(NAME ${SYMBOLS})
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modules/${BASE}.h" "${S}")
  #string(REGEX REPLACE "[\\n;]" "\\\\n" SYMBOLS "${SYMBOLS}")
  #message("Symbols: ${SYMBOLS}")
endfunction(generate_module_header SOURCE)

function(make_module_header SOURCE)
  string(REGEX REPLACE "\\.tmp$" "" BASE2 "${SOURCE}")
  basename(BASE "${BASE2}" .c)
  string(REGEX REPLACE "\\.c$" ".h" HEADER "${BASE2}")
  string(REGEX REPLACE "-" "_" NAME "${BASE}")
  set(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/gen-${BASE}-header.cmake")
  make_script(
    "${SCRIPT}"
    "message(\"Generating module '${NAME}'\")\nremake_module(${SOURCE})\n"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake"
  )
  add_custom_target(${BASE}.h ALL ${CMAKE_COMMAND} -P ${SCRIPT}
                    DEPENDS ${SOURCE} BYPRODUCTS ${HEADER} SOURCES ${SOURCE})
endfunction(make_module_header SOURCE)

function(list_definitions SOURCE OUTVAR)
  file(READ "${SOURCE}" CSRC)
  string(REGEX MATCHALL "qjsc_[0-9A-Za-z_]+" SYMBOLS "${CSRC}")
  list(FILTER SYMBOLS EXCLUDE REGEX "_size$")
  string(REGEX REPLACE "qjsc_" "" SYMBOLS "${SYMBOLS}")
  set(OUT "")

  foreach(DEF ${SYMBOLS})
    if(ARGN AND NOT "${DEF}" STREQUAL "${ARGN}")
      list(APPEND OUT "${DEF}")
    endif(ARGN AND NOT "${DEF}" STREQUAL "${ARGN}")
  endforeach(DEF ${SYMBOLS})

  set("${OUTVAR}" "${OUT}" PARENT_SCOPE)
endfunction(list_definitions SOURCE OUTVAR)

function(include_definitions OUTVAR)
  #print_str("include_definitions(${OUTVAR} ${ARGN})")
  set(S "")
  foreach(DEF ${ARGN})
    string(STRIP "${DEF}" DEF)
    string(REGEX REPLACE "_" "-" NAME "${DEF}")
    set(S "${S}#include \"${NAME}.h\"\n")
  endforeach(DEF ${ARGN})

  #print_str("include_definitions S=${S}")
  set("${OUTVAR}" "${S}" PARENT_SCOPE)
endfunction(include_definitions OUTVAR)

function(extract_definition SOURCE OUTVAR DEF)
  basename(BASE "${SOURCE}" .c)
  file(READ "${SOURCE}" CSRC)
  string(REGEX MATCHALL "const[^\n;]*qjsc_${DEF}[[_][^;]*;" DEFINITIONS
               "${CSRC}")
  string(REPLACE "\n" "\\n" DEFINITIONS "${DEFINITIONS}")
  string(REGEX REPLACE ";\\s*;*" ";" DEFINITIONS "${DEFINITIONS}")
  string(REGEX REPLACE ";;" ";" DEFINITIONS "${DEFINITIONS}")
  string(REGEX REPLACE "\n" ";\n" DEFINITIONS "${DEFINITIONS}")
  string(REGEX REPLACE ";;*" ";" DEFINITIONS "${DEFINITIONS}")
  set(S "")

  foreach(LINE ${DEFINITIONS})
    if(S STREQUAL "")
      set(S "${LINE};")
    else(S STREQUAL "")
      set(S "${S}\n\n${LINE};")
    endif(S STREQUAL "")
  endforeach(LINE ${DEFINITIONS})

  string(REGEX REPLACE "\\\\n" "\\n" S "${S}")
  set("${OUTVAR}" "${S}\n" PARENT_SCOPE)
endfunction(extract_definition SOURCE OUTVAR DEF)

function(remake_module SOURCE)
  basename(BASE "${SOURCE}" .c)
  string(REGEX REPLACE "-" "_" NAME "${BASE}")

  list_definitions("${SOURCE}" DEFLIST ${NAME})
  list(REMOVE_ITEM DEFLIST "${NAME}")
  list(REMOVE_ITEM DEFLIST "${BASE}")
  list(FILTER DEFLIST EXCLUDE REGEX "^${NAME}$")
  list(FILTER DEFLIST EXCLUDE REGEX "^${BASE}$")

  #print_str("Included definitions in ${NAME}: ${DEFLIST}")

  include_definitions(INC "${DEFLIST}")

  extract_definition("${SOURCE}" DEF "${NAME}")

  file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/modules")

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modules/${BASE}.c"
       "#include \"${BASE}.h\"\n\n${DEF}")
  generate_module_header(${SOURCE} ${DEFLIST})

endfunction(remake_module SOURCE)

function(make_script OUTPUT_FILE TEXT INCLUDES)
  basename(BASE "${SOURCE}" .c)
  string(REGEX REPLACE "\\.c$" ".h" HEADER "${SOURCE}")
  string(REGEX REPLACE "-" "_" NAME "${BASE}")
  set(S "cmake_policy(SET CMP0007 NEW)\n")
  foreach(INC ${INCLUDES})
    set(S "${S}\ninclude(${INC})\n")
  endforeach(INC ${INCLUDES})
  set(S "${S}\n\n${TEXT}\n")
  file(WRITE "${OUTPUT_FILE}" "${S}")
endfunction(make_script OUTPUT_FILE TEXT INCLUDES)

function(make_module FNAME)
  string(REGEX REPLACE "_" "-" NAME "${FNAME}")
  string(REGEX REPLACE "-" "_" VNAME "${FNAME}")
  string(TOUPPER "${FNAME}" UUNAME)
  string(REGEX REPLACE "-" "_" UNAME "${UUNAME}")

  set(TARGET_NAME qjs-${NAME})
  set(DEPS ${${VNAME}_DEPS})
  set(LIBS ${${VNAME}_LIBRARIES})

  if(ARGN)
    set(SOURCES ${ARGN} ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    add_unique(DEPS ${${VNAME}_DEPS})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    add_unique(LIBS ${${VNAME}_LIBRARIES})
  endif(ARGN)

  message(
    STATUS
      "Building QuickJS module: ${FNAME} (deps: ${DEPS}, libs: ${LIBS}) JS_${UNAME}_MODULE=1"
  )

  if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(BUILD_SHARED_MODULES OFF)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "lib")
  else(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "")
  endif(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  #dump(VNAME ${VNAME}_SOURCES SOURCES)

  if(BUILD_SHARED_MODULES)
    #add_library(${TARGET_NAME} MODULE ${SOURCES})
    add_library(${TARGET_NAME} SHARED ${SOURCES})

    set_target_properties(
      ${TARGET_NAME}
      PROPERTIES RPATH "${MBEDTLS_LIBRARY_DIR}:${QUICKJS_C_MODULE_DIR}"
                 INSTALL_RPATH "${QUICKJS_C_MODULE_DIR}" PREFIX "${PREFIX}"
                 OUTPUT_NAME "${VNAME}" COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(
      ${TARGET_NAME}
      PRIVATE _GNU_SOURCE=1 JS_SHARED_LIBRARY=1 JS_${UNAME}_MODULE=1
              CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}")

    target_link_directories(${TARGET_NAME} PUBLIC "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(${TARGET_NAME} PUBLIC ${LIBS} ${QUICKJS_LIBRARY})

    #message("C module dir: ${QUICKJS_C_MODULE_DIR}")
    install(TARGETS ${TARGET_NAME} RUNTIME DESTINATION "${QUICKJS_C_MODULE_DIR}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ
                                GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

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

  set_target_properties(
    ${TARGET_NAME}-static
    PROPERTIES OUTPUT_NAME "${VNAME}" PREFIX "quickjs-" SUFFIX
                                                        "${LIBRARY_SUFFIX}"
               COMPILE_FLAGS "")
  target_compile_definitions(
    ${TARGET_NAME}-static PRIVATE _GNU_SOURCE=1 JS_${UNAME}_MODULE=1
                                  CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}")
  target_link_directories(${TARGET_NAME}-static PUBLIC
                          "${CMAKE_CURRENT_BINARY_DIR}")
  target_link_libraries(${TARGET_NAME}-static INTERFACE ${QUICKJS_LIBRARY})

endfunction()

if(WASI OR EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  option(BUILD_SHARED_MODULES "Build shared modules" OFF)
else(WASI OR EMSCRIPTEN)
  option(BUILD_SHARED_MODULES "Build shared modules" ON)
endif(WASI OR EMSCRIPTEN)

if(WIN32 OR MINGW)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif(WIN32 OR MINGW)

if(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  set(LIBRARY_PREFIX "lib")
  set(LIBRARY_SUFFIX ".a")
endif(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL
                                    "Emscripten")

if(NOT LIBRARY_PREFIX)
  set(LIBRARY_PREFIX "${CMAKE_STATIC_LIBRARY_PREFIX}")
endif(NOT LIBRARY_PREFIX)
if(NOT LIBRARY_SUFFIX)
  set(LIBRARY_SUFFIX "${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif(NOT LIBRARY_SUFFIX)

#set(CMAKE_SHARED_LINKER_FLAGS ${CMAKE_SHARED_LINKER_FLAGS} -Wl,-rpath=${QUICKJS_C_MODULE_DIR})
