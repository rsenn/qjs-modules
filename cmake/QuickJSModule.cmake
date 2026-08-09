##
## compile_code <FILENAME> <CODE> <RESULT-VARIABLE> <OUTPUT-VARIABLE> <LIBS> <LINKER-FLAGS>
##
function(compile_code RESULT_VAR CODE)
  string(TOLOWER "${RESULT_VAR}" NAME)
  string(REGEX REPLACE "_" "-" FILE "try-${NAME}.c")

  if(NOT DEFINED "${RESULT_VAR}")
    file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/${FILE}" "${CODE}")

    # must match the include search order the real qjsm.c/quickjs-*.c translation units get
    # (see include_directories(${QUICKJS_INCLUDE_DIRS}) / include_directories(${QUICKJS_INCLUDE_DIR})
    # in CMakeLists.txt) - otherwise this probe can silently detect a different header than the one
    # actually compiled against (js-module-loader-detection-vs-actual-headers)
    set(_compile_code_includes "${QUICKJS_INCLUDE_DIRS}" "${QUICKJS_SOURCES_ROOT}")
    set(_compile_code_iflags "")

    foreach(_dir ${_compile_code_includes})
      set(_compile_code_iflags "${_compile_code_iflags} -I${_dir}")
    endforeach(_dir ${_compile_code_includes})

    try_compile(
      RESULT "${CMAKE_CURRENT_BINARY_DIR}"
      "${CMAKE_CURRENT_BINARY_DIR}/${FILE}"
      LINK_OPTIONS "-L${QUICKJS_LIBRARY_DIR}"
      COMPILE_DEFINITIONS "${_compile_code_iflags}"
      CMAKE_FLAGS
        "-DINCLUDE_DIRECTORIES=${_compile_code_includes}" "-DLINK_DIRECTORIES=${QUICKJS_LIBRARY_DIR}" LINK_DIRECTORIES
        "${QUICKJS_LIBRARY_DIR}"
      LINK_LIBRARIES "${QUICKJS_LIBRARY}"
      OUTPUT_VARIABLE OUTPUT)

    set(${RESULT_VAR} "${RESULT}" PARENT_SCOPE)

    if(NOT RESULT)
      message(STATUS "Failed to compile '${FILE}'. Output:\n${OUTPUT}")
    endif(NOT RESULT)

  endif(NOT DEFINED "${RESULT_VAR}")
endfunction()

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
  #message(STATUS "Compile QuickJS module '${BASE}.c' from '${SOURCE}'")

  set(ARGLIST "${ARGN}")
  list(POP_FRONT ARGLIST OUT)

  set(MODULES_DIR "${CMAKE_BINARY_DIR}/modules")
  set(MODULES_DIR "${MODULES_DIR}" PARENT_SCOPE)
  file(MAKE_DIRECTORY "${MODULES_DIR}")

  if(OUT AND NOT "${OUT}" STREQUAL "")
    set(OUTPUT_FILE ${OUT})
  else(OUT AND NOT "${OUT}" STREQUAL "")
    set(OUTPUT_FILE "${MODULES_DIR}/${BASE}.c")
  endif(OUT AND NOT "${OUT}" STREQUAL "")

  list(APPEND COMPILED_MODULES "${OUTPUT_FILE}")
  list(APPEND COMPILED_TARGETS "${BASE}.c")
  set(COMPILED_MODULES "${COMPILED_MODULES}" PARENT_SCOPE)
  set(COMPILED_TARGETS "${COMPILED_TARGETS}" PARENT_SCOPE)

  unset(ADD_MODULES)
  foreach(MOD IN ITEMS ${ARGLIST})
    list(APPEND ADD_MODULES -M "${MOD}")
  endforeach(MOD IN ITEMS ${ARGLIST})

  #add_custom_command(OUTPUT "${OUTPUT_FILE}" COMMAND qjsc ${ADD_MODULES} -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" DEPENDS ${QJSC_DEPS} WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler" SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE} DEPENDS qjs-inspect qjs-misc)
  add_custom_target(
    "${BASE}.c" ALL
    BYPRODUCTS "${OUTPUT_FILE}"
    COMMAND "${QJSC}" ${ADD_MODULES} -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
    DEPENDS ${QJSC_DEPS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" #DEPENDS qjs-inspect qjs-misc
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
      set(S "${S}\nextern const uint32_t qjsc_${NAME}_size;\nextern const uint8_t qjsc_${NAME}[];\n")
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
  make_script("${SCRIPT}" "message(\"Generating module '${NAME}'\")\nremake_module(${SOURCE})\n"
              "${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake")
  add_custom_target(${BASE}.h ALL ${CMAKE_COMMAND} -P ${SCRIPT} DEPENDS ${SOURCE} BYPRODUCTS ${HEADER}
                    SOURCES ${SOURCE})
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
  string(REGEX MATCHALL "const[^\n;]*qjsc_${DEF}[[_][^;]*;" DEFINITIONS "${CSRC}")
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

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modules/${BASE}.c" "#include \"${BASE}.h\"\n\n${DEF}")
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
  set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
  set(LINK_FLAGS ${${VNAME}_LINK_FLAGS})

  #dump(VNAME ${VNAME}_SOURCES)

  if(ARGN)
    set(SOURCES ${ARGN} ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    add_unique(DEPS ${${VNAME}_DEPS})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    add_unique(LIBS ${${VNAME}_LIBRARIES})
  endif(ARGN)
  add_unique(LIBS ${COMMON_LIBRARIES})

  set(MSG "Building QuickJS module: ${FNAME}")

  if(DEPS)
    set(MSG "${MSG} (deps: ${DEPS})")
  endif()
  set(OUT "${LIBS}")
  list(REMOVE_ITEM OUT compiled)
  list(REMOVE_ITEM OUT modules)
  if(OUT)
    string(REPLACE ";" " " OUT "${OUT}")
    set(MSG "${MSG} (libs: ${OUT})")
  endif()

  #message(STATUS "${MSG}")

  if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(BUILD_SHARED_MODULES OFF)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "lib")
  else(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "")
  endif(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(BUILD_SHARED_MODULES)
    #add_library(${TARGET_NAME} MODULE ${SOURCES})
    add_library(${TARGET_NAME} SHARED ${SOURCES})

    #dump(QUICKJS_C_MODULE_DIR QUICKJS_LIBRARY_DIR)
    #dump(MODULE_COMPILE_FLAGS)

    set_target_properties(
      ${TARGET_NAME}
      PROPERTIES RPATH "${MBEDTLS_LIBRARY_DIR}:${QUICKJS_C_MODULE_DIR}" INSTALL_RPATH "${QUICKJS_C_MODULE_DIR}"
                 LINK_FLAGS "${LINK_FLAGS}" PREFIX "${PREFIX}" OUTPUT_NAME "${VNAME}" COMPILE_FLAGS
                                                                                      "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(
      ${TARGET_NAME} PRIVATE _GNU_SOURCE=1 JS_SHARED_LIBRARY=1 JS_${UNAME}_MODULE=1
                             QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}" LIBMAGIC_DB="${LIBMAGIC_DB}")

    target_link_directories(${TARGET_NAME} PUBLIC ${LINK_DIRECTORIES} ${QUICKJS_LIBRARY_DIR}
                            ${CMAKE_CURRENT_BINARY_DIR})

    target_link_libraries(${TARGET_NAME} PUBLIC ${LIBS} ${QUICKJS_LIBRARY})

    install(TARGETS ${TARGET_NAME} DESTINATION "${QUICKJS_C_MODULE_DIR}"
            PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)

    config_module(${TARGET_NAME})

    set(LIBRARIES ${${VNAME}_LIBRARIES})
    if(LIBRARIES)
      target_link_libraries(${TARGET_NAME} PRIVATE ${LIBRARIES})
    endif(LIBRARIES)
    set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
    if(LINK_DIRECTORIES)
      target_link_directories(${TARGET_NAME} PRIVATE ${LINK_DIRECTORIES})
    endif(LINK_DIRECTORIES)
    if(DEPS)
      add_dependencies(${TARGET_NAME} ${DEPS})
    endif(DEPS)

  endif(BUILD_SHARED_MODULES)

  list(APPEND MODULES_SOURCES quickjs-${NAME}.c)
  set(MODULES_SOURCES "${MODULES_SOURCES}" PARENT_SCOPE)

  if(BUILD_STATIC_MODULES)
    set(STATIC_TARGET_NAME "${TARGET_NAME}-static")

    add_library(${STATIC_TARGET_NAME} STATIC ${SOURCES})

    # Deliberately no JS_SHARED_LIBRARY define here: every quickjs-*.c ends
    # with `#if defined(JS_SHARED_LIBRARY) && defined(JS_*_MODULE) /
    # define JS_INIT_MODULE js_init_module / #else / js_init_module_<name>`,
    # so leaving it undefined is what makes the entry point come out named
    # js_init_module_${VNAME} instead of the dlopen-convention js_init_module
    # (which would collide across every statically-linked module).
    set_target_properties(${STATIC_TARGET_NAME} PROPERTIES OUTPUT_NAME "${VNAME}" PREFIX "quickjs-"
                                                            COMPILE_FLAGS "${MODULE_COMPILE_FLAGS}")

    target_compile_definitions(
      ${STATIC_TARGET_NAME} PRIVATE _GNU_SOURCE=1 JS_${UNAME}_MODULE=1 QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}"
                                    LIBMAGIC_DB="${LIBMAGIC_DB}")

    # LIBRARIES/DEPS come from shared variables like ${VNAME}_LIBRARIES (e.g.
    # lexer_LIBRARIES=qjs-location) that name the *shared* module target;
    # under BUILD_STATIC_MODULES only "<that>-static" actually exists, so rewrite
    # in-tree "qjs-*" references to their static counterpart. Anything else
    # (e.g. serial_DEPS=libserialport, an ExternalProject target) is left
    # alone.
    set(STATIC_LIBRARIES "")
    foreach(LIB ${${VNAME}_LIBRARIES})
      if(LIB MATCHES "^qjs-")
        list(APPEND STATIC_LIBRARIES "${LIB}-static")
      else()
        list(APPEND STATIC_LIBRARIES "${LIB}")
      endif()
    endforeach()
    if(STATIC_LIBRARIES)
      target_link_libraries(${STATIC_TARGET_NAME} PRIVATE ${STATIC_LIBRARIES})
    endif(STATIC_LIBRARIES)
    set(LINK_DIRECTORIES ${${VNAME}_LINK_DIRECTORIES})
    if(LINK_DIRECTORIES)
      target_link_directories(${STATIC_TARGET_NAME} PRIVATE ${LINK_DIRECTORIES})
    endif(LINK_DIRECTORIES)
    set(STATIC_DEPS "")
    foreach(DEP ${DEPS})
      if(DEP MATCHES "^qjs-")
        list(APPEND STATIC_DEPS "${DEP}-static")
      else()
        list(APPEND STATIC_DEPS "${DEP}")
      endif()
    endforeach()
    if(STATIC_DEPS)
      add_dependencies(${STATIC_TARGET_NAME} ${STATIC_DEPS})
    endif(STATIC_DEPS)

    list(APPEND STATIC_MODULE_TARGETS "${STATIC_TARGET_NAME}")
    list(APPEND STATIC_MODULE_NAMES "${NAME}")
    set(STATIC_MODULE_TARGETS "${STATIC_MODULE_TARGETS}" PARENT_SCOPE)
    set(STATIC_MODULE_NAMES "${STATIC_MODULE_NAMES}" PARENT_SCOPE)
  endif(BUILD_STATIC_MODULES)

endfunction()

if(WASI)
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif(WASI)

if(WASI OR EMSCRIPTEN)
  option(BUILD_SHARED_MODULES "Build shared modules" OFF)
else(WASI OR EMSCRIPTEN)
  option(BUILD_SHARED_MODULES "Build shared modules" ON)
endif(WASI OR EMSCRIPTEN)

if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  # There's no dlopen()-able shared-module story on wasm (BUILD_SHARED_MODULES
  # is forced off there), so statically linking every module straight into
  # qjsm is the only way to get them at all.
  option(BUILD_STATIC_MODULES "Build modules as static libraries linked into qjsm" ON)
else()
  option(BUILD_STATIC_MODULES "Build modules as static libraries linked into qjsm" OFF)
endif()

if(WIN32 OR MINGW)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif(WIN32 OR MINGW)

if(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  set(LIBRARY_PREFIX "lib")
  set(LIBRARY_SUFFIX ".a")
endif(WASI OR WASM OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

if(NOT LIBRARY_PREFIX)
  set(LIBRARY_PREFIX "${CMAKE_STATIC_LIBRARY_PREFIX}")
endif(NOT LIBRARY_PREFIX)
if(NOT LIBRARY_SUFFIX)
  set(LIBRARY_SUFFIX "${CMAKE_STATIC_LIBRARY_SUFFIX}")
endif(NOT LIBRARY_SUFFIX)
