# quickjs_module_options([SHARED_DEFAULT <ON|OFF>] [STATIC_DEFAULT <ON|OFF>])
#
# Declares BUILD_SHARED/BUILD_STATIC the same way across
# every qjs-* project, so a single -DBUILD_SHARED_MODULES=.../
# -DBUILD_STATIC_MODULES=... passed to the top-level quickjs/ build reaches
# every add_subdirectory()'d qjs-* submodule unchanged (same cache-variable
# name everywhere). Guarded by NOT DEFINED so a value already set by the
# caller - this project's own earlier option() call, or the outer build -
# always wins; this macro only ever supplies the fallback default.
#
# WASI/Emscripten have no dlopen()-able shared-module story, so the shared
# default is forced off and the static default forced on there regardless
# of what the caller asked for.
macro(quickjs_module_options)
  cmake_parse_arguments(QMO "" "SHARED_DEFAULT;STATIC_DEFAULT" "" ${ARGN})
  if(NOT DEFINED QMO_SHARED_DEFAULT)
    set(QMO_SHARED_DEFAULT ON)
  endif()

  if(NOT DEFINED QMO_STATIC_DEFAULT)
    set(QMO_STATIC_DEFAULT OFF)
  endif(NOT DEFINED QMO_STATIC_DEFAULT)

  if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(QMO_SHARED_DEFAULT OFF)
    set(QMO_STATIC_DEFAULT ON)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT DEFINED BUILD_SHARED)
    option(BUILD_SHARED "Build shared QuickJS module(s)" ${QMO_SHARED_DEFAULT})
  endif()

  if(NOT DEFINED BUILD_STATIC)
    option(BUILD_STATIC "Build static QuickJS module(s) (*.a)" ${QMO_STATIC_DEFAULT})
  endif(NOT DEFINED BUILD_STATIC)
endmacro()

#
# get_native_modules <OUTPUT-VARIABLE>
#
# Names of the native modules, one per quickjs-<name>.c in the source
# directory, with '-' written '_': quickjs-child-process.c -> child_process.
#
function(get_native_modules OUTVAR)
  file(GLOB FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-*.c")

  set(NAMES "")
  foreach(FILE ${FILES})
    string(REGEX REPLACE "^quickjs-(.*)\\.c$" "\\1" NAME "${FILE}")
    string(REPLACE "-" "_" NAME "${NAME}")
    list(APPEND NAMES "${NAME}")
  endforeach(FILE ${FILES})

  list(SORT NAMES)
  set("${OUTVAR}" "${NAMES}" PARENT_SCOPE)
endfunction()

#
# get_compiled_modules <OUTPUT-VARIABLE>
#
# Names of the JS modules that can be compiled into a builtin, one per .js
# file below lib/ without the extension: lib/fsPromises.js -> fsPromises,
# lib/xml/read.js -> xml/read.
#
function(get_compiled_modules OUTVAR)
  file(GLOB_RECURSE FILES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}/lib" "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.js")

  set(NAMES "")
  foreach(FILE ${FILES})
    string(REGEX REPLACE "\\.js$" "" NAME "${FILE}")
    list(APPEND NAMES "${NAME}")
  endforeach(FILE ${FILES})

  list(SORT NAMES)
  set("${OUTVAR}" "${NAMES}" PARENT_SCOPE)
endfunction()
 
#
# module_depends <MODULE> <DEP>...
#
# Link the native module MODULE with the native modules DEP... (names, no
# prefix); module_depends(deep predicate pointer) is
#
# ```cmake
# if(TARGET qjs-deep)
#   target_link_libraries(qjs-deep PRIVATE qjs-predicate qjs-pointer)
# endif(TARGET qjs-deep)
# ```
#
# and the same for qjs-deep-static with the -static targets. Also records
# DEP... in ${MODULE}_LIBRARIES, which qjsm reads to compile them in.
# Call after make_module(MODULE).
#
function(module_depends MODULE)
  set(DEPS "")
  set(STATIC_DEPS "")
  foreach(DEP ${ARGN})
    list(APPEND DEPS "qjs-${DEP}")
    list(APPEND STATIC_DEPS "qjs-${DEP}-static")
  endforeach(DEP ${ARGN})

  if(TARGET qjs-${MODULE})
    target_link_libraries(qjs-${MODULE} PRIVATE ${DEPS})
  endif(TARGET qjs-${MODULE})

  if(TARGET qjs-${MODULE}-static)
    target_link_libraries(qjs-${MODULE}-static PRIVATE ${STATIC_DEPS})
  endif(TARGET qjs-${MODULE}-static)

  string(REPLACE "-" "_" VAR "${MODULE}")
  set(${VAR}_LIBRARIES ${${VAR}_LIBRARIES} ${DEPS} PARENT_SCOPE)
endfunction(module_depends)

#
# module_path <NAME> <OUTVAR>
#
# Store the path of NAME inside the precompiled-modules directory
# (${CMAKE_BINARY_DIR}/modules) in OUTVAR.
#
function(module_path NAME OUTVAR)
  set("${OUTVAR}" "${CMAKE_BINARY_DIR}/modules/${NAME}" PARENT_SCOPE)
endfunction()

#
# compile_code <RESULT-VARIABLE> <CODE>
#
# Try to compile the C source CODE against QuickJS and store whether it worked
# in RESULT-VARIABLE, unless already defined.
#
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

#
# config_module <TARGET_NAME>
#
# Apply the common link directory, dependencies and compile options
# (QUICKJS_LIBRARY_DIR, QUICKJS_MODULE_DEPENDENCIES, QUICKJS_MODULE_CFLAGS) to
# the target TARGET_NAME.
#
function(config_module TARGET_NAME)
  if(QUICKJS_LIBRARY_DIR)
    set_target_properties(${TARGET_NAME} PROPERTIES LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
  endif()

  if(QUICKJS_MODULE_DEPENDENCIES)
    target_link_libraries(${TARGET_NAME} ${QUICKJS_MODULE_DEPENDENCIES})
  endif()

  if(QUICKJS_MODULE_CFLAGS)
    target_compile_options(${TARGET_NAME} PRIVATE "${QUICKJS_MODULE_CFLAGS}")
  endif(QUICKJS_MODULE_CFLAGS)
endfunction()

#
# compile_module <SOURCE>
#
# Add a target that compiles the JS module SOURCE to C with qjsc, into OUT
# (default modules/<name>.c) with the MODULES imports given as -M.
#
function(compile_module SOURCE)
  string(REGEX REPLACE "lib/" "" BASE "${SOURCE}")
  string(REGEX REPLACE "\\.js$" "" BASE "${BASE}")
  string(REGEX REPLACE "[/]" "_" BASE "${BASE}")

  #message("compile_module(\n\tSOURCE ${SOURCE}\n\tBASE ${BASE}\n)")
  
  #if(COMPILE_MODULE_CNAME)
  #  set(BASE "${COMPILE_MODULE_CNAME}")
  #endif(COMPILE_MODULE_CNAME)

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

    string(REGEX REPLACE "_" "-" TARGET_NAME "qjs-${BASE}-js")
    
  relative_paths(OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/modules" "${OUTPUT_FILE}")
  string(REGEX REPLACE "/" "_" OUTPUT_FILE "${OUTPUT_FILE}")
  set(OUTPUT_FILE "modules/${OUTPUT_FILE}")


  list(APPEND COMPILED_MODULES "${OUTPUT_FILE}")
  list(APPEND COMPILED_TARGETS "${TARGET_NAME}")

  set(COMPILED_MODULES "${COMPILED_MODULES}" PARENT_SCOPE)
  set(COMPILED_TARGETS "${COMPILED_TARGETS}" PARENT_SCOPE)

  unset(ADD_MODULES)

  string(REGEX REPLACE "qjs-\(.*\)-js" "\\1" COMPILE_MODULE_CNAME "${TARGET_NAME}")
  string(REGEX REPLACE "-" "_" COMPILE_MODULE_CNAME "${COMPILE_MODULE_CNAME}")
  
  # COMPILE_MODULE_CNAME: C name of the generated data (qjsc -N), default is the file's basename
  if(COMPILE_MODULE_CNAME)
    list(APPEND ADD_MODULES -N "qjsc_${COMPILE_MODULE_CNAME}")
  endif()

  if(${COMPILE_MODULE_CNAME}_MODULES)
    list(APPEND ARGLIST ${${COMPILE_MODULE_CNAME}_MODULES})
  endif()

  foreach(MOD IN ITEMS ${ARGLIST})
    list(APPEND ADD_MODULES -M "${MOD}")
  endforeach(MOD IN ITEMS ${ARGLIST})

  add_custom_target(
    "${TARGET_NAME}" ALL
    BYPRODUCTS "${OUTPUT_FILE}"
    COMMAND "${QJSC}" ${ADD_MODULES} -v -c -o "${OUTPUT_FILE}" -m "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}"
    DEPENDS ${QJSC_DEPS}
    WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
    COMMENT "Generate ${OUTPUT_FILE} from ${SOURCE} using qjs compiler"
    SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${SOURCE}" #DEPENDS qjs-inspect qjs-misc
  )
endfunction()

#
# generate_module_header <SOURCE>
#
# Write modules/<name>.h declaring the qjsc_* bytecode symbols in the C file
# SOURCE, and include the headers of the INCLUDES it shares.
#
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
    if(NOT NAME IN_LIST INCLUDES)
      set(S "${S}\nextern const uint32_t qjsc_${NAME}_size;\nextern const uint8_t qjsc_${NAME}[];\n")
    endif()
  endforeach()

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/modules/${BASE}.h" "${S}")
  #string(REGEX REPLACE "[\\n;]" "\\\\n" SYMBOLS "${SYMBOLS}")
  #message("Symbols: ${SYMBOLS}")
endfunction()

#
# make_module_header <SOURCE>
#
# Add a target that regenerates the header of the C module SOURCE by running
# remake_module() in a cmake script.
#
function(make_module_header SOURCE)
  string(REGEX REPLACE "\\.tmp$" "" BASE2 "${SOURCE}")
  basename(BASE "${BASE2}" .c)
  string(REGEX REPLACE "\\.c$" ".h" HEADER "${BASE2}")
  string(REGEX REPLACE "-" "_" NAME "${BASE}")
  set(SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/gen-${BASE}-header.cmake")
  make_script("${SCRIPT}" "message(\"Generating module '${NAME}'\")\nremake_module(${SOURCE})\n"
              "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Functions.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/Compat.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake")
  add_custom_target(${BASE}.h ALL ${CMAKE_COMMAND} -P ${SCRIPT} DEPENDS ${SOURCE} BYPRODUCTS ${HEADER}
                    SOURCES ${SOURCE})
endfunction()

#
# list_definitions <SOURCE> <OUTVAR>
#
# Store the names of the qjsc_* definitions in the C file SOURCE in OUTVAR,
# leaving out the one named by the extra argument.
#
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
endfunction()

#
# include_definitions <OUTVAR>
#
# Store in OUTVAR an #include line for the header of each definition name
# given (underscores written as '-').
#
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
endfunction()

#
# extract_definition <SOURCE> <OUTVAR> <DEF>
#
# Store in OUTVAR the qjsc_<DEF> definition (the const data declaration) found
# in the C file SOURCE.
#
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
endfunction()

#
# remake_module <SOURCE>
#
# Split the compiled module C file SOURCE into modules/<name>.c and a matching
# header, so each module's bytecode has its own file.
#
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

endfunction()

#
# make_script <OUTPUT_FILE> <TEXT> <INCLUDES>
#
# Write the cmake script OUTPUT_FILE that includes the files INCLUDES and then
# runs TEXT.
#
function(make_script OUTPUT_FILE TEXT INCLUDES)
  basename(BASE "${SOURCE}" .c)
  string(REGEX REPLACE "\\.c$" ".h" HEADER "${SOURCE}")
  string(REGEX REPLACE "-" "_" NAME "${BASE}")
  set(S "cmake_policy(SET CMP0007 NEW)\n")
  foreach(INC ${INCLUDES})
    set(S "${S}\ninclude(${INC})\n")
  endforeach()

  set(S "${S}\n\n${TEXT}\n")
  file(WRITE "${OUTPUT_FILE}" "${S}")
endfunction()

#
# make_module <FNAME>
#
# Define the QuickJS native module FNAME: a shared library target and/or a
# static one (per BUILD_SHARED/BUILD_STATIC) from its sources, libraries and
# deps, plus install rules.
#
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
    set_add(DEPS ${${VNAME}_DEPS})
  else(ARGN)
    set(SOURCES quickjs-${NAME}.c ${${VNAME}_SOURCES} ${COMMON_SOURCES})
    set_add(LIBS ${${VNAME}_LIBRARIES})
  endif()

  set_add(LIBS ${COMMON_LIBRARIES})

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
    set(BUILD_SHARED OFF)
  endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "lib")
  else(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(PREFIX "")
  endif(NOT WASI AND "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

  if(BUILD_SHARED)
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

    # ${VNAME}_LIBRARIES_shared overrides plain ${VNAME}_LIBRARIES when a module needs a
    # genuinely different (PIC-built) import target for its shared vs. static build --
    # see build_libarchive()/build_libserialport() in cmake/BuildLib*.cmake, which set
    # e.g. archive_LIBRARIES_shared/archive_LIBRARIES_static to two distinct IMPORTED
    # targets rather than one shared between both.
    if(DEFINED ${VNAME}_LIBRARIES_shared)
      set(LIBRARIES ${${VNAME}_LIBRARIES_shared})
    else()
      set(LIBRARIES ${${VNAME}_LIBRARIES})
    endif()
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

  endif(BUILD_SHARED)

  list(APPEND MODULES_SOURCES quickjs-${NAME}.c)
  set(MODULES_SOURCES "${MODULES_SOURCES}" PARENT_SCOPE)

  if(BUILD_STATIC)
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
    # under BUILD_STATIC only "<that>-static" actually exists, so rewrite
    # in-tree "qjs-*" references to their static counterpart. Anything else
    # (e.g. serial_DEPS=libserialport, an ExternalProject target) is left
    # alone. ${VNAME}_LIBRARIES_static, when defined, is used as-is instead (already the
    # correct non-PIC target -- see the ${VNAME}_LIBRARIES_shared comment above).
    set(STATIC_LIBRARIES "")
    if(DEFINED ${VNAME}_LIBRARIES_static)
      set(STATIC_LIBRARIES ${${VNAME}_LIBRARIES_static})
    else()
      foreach(LIB ${${VNAME}_LIBRARIES})
        if(LIB MATCHES "^qjs-")
          list(APPEND STATIC_LIBRARIES "${LIB}-static")
        else()
          list(APPEND STATIC_LIBRARIES "${LIB}")
        endif()
      endforeach()
    endif()
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

    # A <name>.module.cmake next to the .a, for a *separate* CMake configure (a sibling
    # project, or this one's own EXTERNAL_MODULES/BUILTIN_MODULES machinery) to include()
    # and learn this module's transitive link requirements without re-deriving them --
    # "modules"/"compiled" (this project's own internal libraries, meaningless outside
    # it) are dropped, and any qjs-<dep> sibling module is exported by bare name ("dep",
    # not "qjs-dep-static", a target name that wouldn't exist in the other configure) so
    # the consumer can resolve it however it resolves module names (e.g. its own
    # BUILTIN_MODULES).
    # Recomputed from LIBS rather than reusing OUT: OUT gets mangled into a
    # space-joined display string above (for the status message), no longer a
    # proper list.
    set(_module_cmake_libs "${LIBS}")
    list(REMOVE_ITEM _module_cmake_libs compiled modules)
    list(TRANSFORM _module_cmake_libs REPLACE "^qjs-" "")
    get_target_property(_module_cmake_lib_path "${STATIC_TARGET_NAME}" ARCHIVE_OUTPUT_DIRECTORY)
    if(NOT _module_cmake_lib_path)
      set(_module_cmake_lib_path "${CMAKE_CURRENT_BINARY_DIR}")
    endif()
    # No CMAKE_STATIC_LIBRARY_PREFIX ("lib"): the target's own PREFIX property above
    # overrides it to "quickjs-" outright, not on top of it.
    set(_module_cmake_lib_file "${_module_cmake_lib_path}/quickjs-${VNAME}${CMAKE_STATIC_LIBRARY_SUFFIX}")

    file(
      WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-${VNAME}.module.cmake"
      "# Auto-generated by make_module(${NAME}) -- do not edit, will be overwritten.\n"
      "set(${VNAME}_MODULE_LIBRARY \"${_module_cmake_lib_file}\")\n"
      "set(${VNAME}_MODULE_LIBRARIES \"${_module_cmake_libs}\")\n"
      "set(${VNAME}_MODULE_LINK_FLAGS \"${LINK_FLAGS}\")\n"
      "set(${VNAME}_MODULE \"\${${VNAME}_MODULE_LIBRARY};\${${VNAME}_MODULE_LIBRARIES}\")\n")
  endif(BUILD_STATIC)

endfunction()

if(WASI)
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif(WASI)

# BUILD_SHARED/BUILD_STATIC themselves are declared by this
# project's own CMakeLists.txt, right after it include()s this file (see
# quickjs_module_options() above) - not here, so each project can pick its
# own defaults through that one call site instead of this shared file
# hardcoding one default for everybody.

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
