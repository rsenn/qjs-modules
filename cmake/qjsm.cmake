# qjsm.cmake: what gets baked into the qjsm binary (qjs-modules only).
#
# builtins: native C modules (BUILTIN_NATIVE) compiled in, and lib/*.js
# compiled to bytecode (BUILTIN_COMPILED); both listed in quickjs-builtins.h.
# BUILTIN_MODULES also takes .js files and directories/libraries of out-of-tree
# qjs-<name> modules (built with -DBUILD_STATIC=ON).
#
# call order, see CMakeLists.txt:
#   qjsm_init_builtins        BUILTIN_MODULES default and cache entry
#   qjsm_resolve_builtins     BUILTINS_NATIVE/BUILTINS_COMPILED, install of the rest of lib/
#   qjsm_declare_imports      <name>_MODULES import lists of the compiled lib/*.js
#   qjsm_compile_builtins     qjsc runs, "compiled" library
#   qjsm_process_builtins     entries routed, dependencies closed, quickjs-builtins.h written
#   qjsm_layout               build info, manifest and layout report (qjsm-layout.cmake)
#   qjsm_add_executable       the qjsm target
#
# macros, not functions: they set variables the rest of CMakeLists.txt reads.

set(_QJSM_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")
include(${_QJSM_CMAKE_DIR}/qjsm-bytecode.cmake)
include(${_QJSM_CMAKE_DIR}/qjsm-layout.cmake)

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

# qjsm_header_compiled(<NAME>)
#
# Adds the line of the bytecode builtin NAME to COMPILED_BUILTIN_HEADER. The C symbols of qjsc
# have '/' and '-' written '_' (qjsc_node_events, qjsc_simple_queue); a name with a '-' keeps it
# as the module name, so `import 'simple-queue'` finds the builtin: BUILTIN_COMPILED_AS(symbol, "name").
macro(qjsm_header_compiled NAME)
  string(REGEX REPLACE "[/-]" "_" _hc_symbol "${NAME}")

  if("${NAME}" MATCHES "-")
    string(REPLACE "/" "_" _hc_name "${NAME}")
    set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED_AS(${_hc_symbol}, \"${_hc_name}\")\n")
  else()
    set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED(${_hc_symbol})\n")
  endif()
endmacro()

#
# qjsm_scan_imports <OUTPUT-VAR> <JS-FILE>
#
# The module specifiers a JS file imports with a literal string:
#
# ```js
# import x from 'a';  import 'b';  export * from 'c';  import { y,
#   z } from 'd';  await import('e');
# ```
#
# gives a b c d e. Specifiers computed at run time (require(name), import(variable))
# are not seen, and neither is anything in a comment that does not start a line.
#
function(qjsm_scan_imports OUTPUT_VAR FILE)
  file(READ "${FILE}" CONTENT)
  set(CONTENT "\n${CONTENT}")

  set(SPECS "")
  foreach(
    PATTERN
    "\n[ \t]*(import|export)[ \t]+[^'\"`=(;\n]*from[ \t]*['\"][^'\"\n]+['\"]" "\n[ \t]*import[ \t]*['\"][^'\"\n]+['\"]"
    "\n[ \t]*}[ \t]*from[ \t]*['\"][^'\"\n]+['\"]" "import[ \t]*\\([ \t]*['\"][^'\"\n]+['\"][ \t]*\\)")
    string(REGEX MATCHALL "${PATTERN}" FOUND "${CONTENT}")

    foreach(ITEM ${FOUND})
      string(REGEX REPLACE "^[^'\"]*['\"]([^'\"]+)['\"].*$" "\\1" SPEC "${ITEM}")
      list(APPEND SPECS "${SPEC}")
    endforeach()
  endforeach()

  list(REMOVE_DUPLICATES SPECS)
  set("${OUTPUT_VAR}" "${SPECS}" PARENT_SCOPE)
endfunction()

#
# qjsm_scan_computed_imports <OUTPUT-VAR> <JS-FILE>
#
# The `import(<expression>)` calls of FILE whose argument is not a string literal:
#
#   return import(m)      ->  "import(m)"
#
# qjsm_scan_imports cannot follow them, so what they load is looked up at run time like any
# user import (a builtin's dynamic import() is the user's request, see doc/qjsm-layout.md 4.1).
#
function(qjsm_scan_computed_imports OUTPUT_VAR FILE)
  file(READ "${FILE}" CONTENT)
  set(CONTENT "\n${CONTENT}")

  # no ';' in the argument: it would split the match into several list items
  string(REGEX MATCHALL "[^A-Za-z0-9_$.]import[ \t]*\\([ \t]*[^'\"` \t);][^);\n]*\\)" FOUND "${CONTENT}")

  set(CALLS "")
  foreach(ITEM ${FOUND})
    string(REGEX REPLACE "^[^i]+" "" CALL "${ITEM}")
    list(APPEND CALLS "${CALL}")
  endforeach()

  list(REMOVE_DUPLICATES CALLS)
  set("${OUTPUT_VAR}" "${CALLS}" PARENT_SCOPE)
endfunction()

#
# qjsm_import_kind <SPECIFIER> <KIND-VAR> <NAME-VAR>
#
# What a specifier in a builtin refers to, as the loader resolves it for a builtin:
#
#   JS      lib/<name>.js           NAME is the lib name: node/events, fsPromises
#   NATIVE  quickjs-<name>.c        NAME has '_' for '-': child_process
#   ENGINE  std and os, always compiled in
#   OTHER   a path, URL or something else; not a module of this tree
#
# `node:<name>` is the layer lib/node/<name>.js when there is one, else <name>; the Node
# subpath builtins (fs/promises) are the flat modules the loader maps them to. Reads
# NATIVE_MODULES and COMPILED_MODULE_NAMES from the caller.
#
function(qjsm_import_kind SPEC KIND_VAR NAME_VAR)
  set(KIND OTHER)
  set(NAME "${SPEC}")
  set(NODE FALSE)

  if(SPEC MATCHES "^node:(.*)$")
    set(NAME "${CMAKE_MATCH_1}")
    set(NODE TRUE)
  endif()

  if(NAME STREQUAL "fs/promises")
    set(NAME fsPromises)
  elseif(NAME STREQUAL "timers/promises")
    set(NAME timersPromises)
  elseif(NAME STREQUAL "assert/strict")
    set(NAME assertStrict)
  elseif(NAME STREQUAL "readline/promises")
    set(NAME readlinePromises)
  endif()

  string(REPLACE "-" "_" NATIVE_NAME "${NAME}")

  if(NAME MATCHES "^[./]" OR (NOT NODE AND NAME MATCHES "^[A-Za-z][A-Za-z0-9+.-]*:"))
    set(KIND OTHER)
  elseif(NATIVE_NAME STREQUAL "std" OR NATIVE_NAME STREQUAL "os")
    set(KIND ENGINE)
  elseif(NODE AND "node/${NAME}" IN_LIST COMPILED_MODULE_NAMES)
    set(KIND JS)
    set(NAME "node/${NAME}")
  elseif(NATIVE_NAME IN_LIST NATIVE_MODULES)
    set(KIND NATIVE)
    set(NAME "${NATIVE_NAME}")
  elseif(NAME IN_LIST COMPILED_MODULE_NAMES)
    set(KIND JS)
  endif()

  set("${KIND_VAR}" "${KIND}" PARENT_SCOPE)
  set("${NAME_VAR}" "${NAME}" PARENT_SCOPE)
endfunction()

# qjsm_need_specs(<IMPORTER>)
#
# Takes the specifiers in _specs, imported by the builtin IMPORTER, and adds what they refer to
# to BUILTINS_NEEDED_JS (queued for scanning in _need_queue) and BUILTINS_NEEDED_NATIVE.
# A native module that is not built cannot be compiled in: that is a warning, the builtin
# would fail to import it.
macro(qjsm_need_specs IMPORTER)
  foreach(_qn_spec ${_specs})
    qjsm_import_kind("${_qn_spec}" _qn_kind _qn_name)

    if(_qn_kind STREQUAL "JS")
      if(NOT _qn_name IN_LIST BUILTINS_COMPILED)
        if(NOT _qn_name IN_LIST BUILTINS_NEEDED_JS)
          list(APPEND BUILTINS_NEEDED_JS "${_qn_name}")
          list(APPEND _need_queue "${_qn_name}")
        endif()
        string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _qn_key "${_qn_name}")
        list(APPEND QJSM_NEEDED_BY_${_qn_key} "${IMPORTER}")
      endif()
    elseif(_qn_kind STREQUAL "NATIVE")
      string(REPLACE "_" "-" _qn_dash "${_qn_name}")
      if(NOT _qn_name IN_LIST BUILTINS_NATIVE AND NOT _qn_dash IN_LIST BUILTINS_NATIVE)
        if(_qn_dash IN_LIST QUICKJS_MODULES)
          if(NOT _qn_name IN_LIST BUILTINS_NEEDED_NATIVE)
            list(APPEND BUILTINS_NEEDED_NATIVE "${_qn_name}")
          endif()
          string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _qn_key "${_qn_name}")
          list(APPEND QJSM_NEEDED_BY_${_qn_key} "${IMPORTER}")
        else()
          message(WARNING "builtin '${IMPORTER}' imports the native module '${_qn_spec}', which is not built "
                          "(not in QUICKJS_MODULES): it cannot be compiled in and the import will fail")
        endif()
      endif()
    endif()
  endforeach()
endmacro()

# qjsm_need_closure()
#
# Scans every lib/<name>.js in _need_queue for imports and follows them, until nothing new
# turns up: the builtins a builtin needs are builtins too.
macro(qjsm_need_closure)
  while(_need_queue)
    list(POP_FRONT _need_queue _qn_cur)
    qjsm_scan_imports(_specs "${CMAKE_CURRENT_SOURCE_DIR}/lib/${_qn_cur}.js")
    qjsm_need_specs("${_qn_cur}")
  endwhile()
endmacro()

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
# (${CMAKE_CURRENT_BINARY_DIR}/modules) in OUTVAR.
#
function(module_path NAME OUTVAR)
  set("${OUTVAR}" "${CMAKE_CURRENT_BINARY_DIR}/modules/${NAME}" PARENT_SCOPE)
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

  set(MODULES_DIR "${CMAKE_CURRENT_BINARY_DIR}/modules")
  set(MODULES_DIR "${MODULES_DIR}" PARENT_SCOPE)
  file(MAKE_DIRECTORY "${MODULES_DIR}")

  if(OUT AND NOT "${OUT}" STREQUAL "")
    set(OUTPUT_FILE ${OUT})
  else(OUT AND NOT "${OUT}" STREQUAL "")
    set(OUTPUT_FILE "${MODULES_DIR}/${BASE}.c")
  endif(OUT AND NOT "${OUT}" STREQUAL "")

  string(REGEX REPLACE "_" "-" TARGET_NAME "qjs-${BASE}-js")

  relative_paths(OUTPUT_FILE "${MODULES_DIR}" "${OUTPUT_FILE}")
  string(REGEX REPLACE "/" "_" OUTPUT_FILE "${OUTPUT_FILE}")
  set(OUTPUT_FILE "modules/${OUTPUT_FILE}")

  #dump(OUTPUT_FILE)

  list(APPEND COMPILED_TARGETS "${TARGET_NAME}")
  set(COMPILED_TARGETS "${COMPILED_TARGETS}" PARENT_SCOPE)

  list(APPEND COMPILED_MODULES "${CMAKE_CURRENT_BINARY_DIR}/${OUTPUT_FILE}")
  set(COMPILED_MODULES "${COMPILED_MODULES}" PARENT_SCOPE)

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
  make_script(
    "${SCRIPT}"
    "message(\"Generating module '${NAME}'\")\nremake_module(${SOURCE})\n"
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Functions.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/Compat.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/qjsm.cmake"
  )
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
# qjsm_native_closure <OUTPUT-VAR> [MODULES...]
#
# MODULES plus the native modules they link (<name>_LIBRARIES entries qjs-<other>),
# transitively, as dashed names: deep -> deep pointer predicate location.
#
function(qjsm_native_closure OUTPUT_VAR)
  set(ALL "")
  foreach(MODULE ${ARGN})
    string(REPLACE "_" "-" MODULE "${MODULE}")
    list(APPEND ALL "${MODULE}")
  endforeach()
  list(REMOVE_DUPLICATES ALL)

  set(CHANGED TRUE)
  while(CHANGED)
    set(CHANGED FALSE)
    foreach(MODULE ${ALL})
      string(REPLACE "-" "_" VAR "${MODULE}")
      foreach(DEP ${${VAR}_LIBRARIES})
        # the match is read into a variable first: ${CMAKE_MATCH_1} inside the same if() would
        # still hold the previous match when the arguments are expanded
        if(DEP MATCHES "^qjs-(.+)$")
          set(OTHER "${CMAKE_MATCH_1}")

          if(NOT OTHER IN_LIST ALL AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-${OTHER}.c")
            list(APPEND ALL "${OTHER}")
            set(CHANGED TRUE)
          endif()
        endif()
      endforeach()
    endforeach()
  endwhile()

  set("${OUTPUT_VAR}" "${ALL}" PARENT_SCOPE)
endfunction()

#
# qjsm_native_dependencies <LINK-VAR> <RUNTIME-VAR> [MODULES...]
#
# What the native MODULES (and the modules they link) add to qjsm beyond libquickjs and
# libm, as file names:
#
#   LINK-VAR     libraries linked in:      libarchive.so  sqlite3.a
#   RUNTIME-VAR  of those, the shared ones qjsm needs at run time: libarchive.so
#
# a bare library name (`magic`) is looked up like -lmagic would be; a CMake target is named
# as it is, and counts as run time only when it is a shared library.
#
function(qjsm_native_dependencies LINK_VAR RUNTIME_VAR)
  qjsm_native_closure(ALL ${ARGN})

  set(LINKED "")
  set(RUNTIME "")

  foreach(MODULE ${ALL})
    string(REPLACE "-" "_" VAR "${MODULE}")

    foreach(LIB ${${MODULE}_LIBRARIES} ${${VAR}_LIBRARIES})
      if(LIB MATCHES "^(qjs-.*|-L.*|m|c|dl|pthread|-lm|-lc|-ldl|-lpthread|optimized|debug|general)$" OR LIB MATCHES
                                                                                                        "[$]<")
        continue()
      endif()

      set(FILE "${LIB}")
      set(SHARED FALSE)

      if(TARGET "${LIB}")
        get_target_property(TYPE "${LIB}" TYPE)
        if(TYPE STREQUAL "SHARED_LIBRARY")
          set(SHARED TRUE)
        endif()
        set(NAME "${LIB}")
      else()
        if(NOT IS_ABSOLUTE "${FILE}")
          string(REGEX REPLACE "^-l" "" FILE "${FILE}")
          find_library(QJSM_DEP_${FILE} NAMES "${FILE}")
          set(FOUND "${QJSM_DEP_${FILE}}")
          unset(QJSM_DEP_${FILE} CACHE)
          if(FOUND)
            set(FILE "${FOUND}")
          endif()
        endif()

        get_filename_component(NAME "${FILE}" NAME)
        if(NAME MATCHES "\\.(so(\\.[0-9.]+)?|dylib|dll)$")
          set(SHARED TRUE)
        endif()
      endif()

      list(APPEND LINKED "${NAME}")
      if(SHARED)
        list(APPEND RUNTIME "${NAME}")
      endif()
    endforeach()
  endforeach()

  list(REMOVE_DUPLICATES LINKED)
  list(REMOVE_DUPLICATES RUNTIME)
  set("${LINK_VAR}" "${LINKED}" PARENT_SCOPE)
  set("${RUNTIME_VAR}" "${RUNTIME}" PARENT_SCOPE)
endfunction()

# qjsm_external_sidecar(<quickjs-mod.module.cmake>)
#
# Registers one externally-built module: includes the sidecar make_module() wrote, checks
# that its archive exists, appends <mod> to EXTERNAL_MODULES and caches <mod>_MODULE.
macro(qjsm_external_sidecar _sc_file)
  get_filename_component(_bm_mod "${_sc_file}" NAME)
  string(REGEX REPLACE "^quickjs-(.*)\\.module\\.cmake$" "\\1" _bm_mod "${_bm_mod}")
  include("${_sc_file}")
  if(NOT EXISTS "${${_bm_mod}_MODULE_LIBRARY}")
    message(
      WARNING
        "BUILTIN_MODULES entry '${_sc_file}' -- ${${_bm_mod}_MODULE_LIBRARY} not built (configure the project with -DBUILD_STATIC=ON)"
    )
  else()
    list(APPEND EXTERNAL_MODULES "${_bm_mod}")
    message(STATUS "qjsm: external module '${_bm_mod}' from ${${_bm_mod}_MODULE_LIBRARY}")
    set(${_bm_mod}_MODULE "${${_bm_mod}_MODULE}"
        CACHE STRING "Static library and dependencies of external module '${_bm_mod}', found via BUILTIN_MODULES")
    set(${_bm_mod}_MODULE_LINK_FLAGS "${${_bm_mod}_MODULE_LINK_FLAGS}"
        CACHE STRING "Link flags of external module '${_bm_mod}'")
  endif()
endmacro()

# qjsm_init_builtins()
#
# BUILTIN_MODULES default (BUILTINS_NATIVE + BUILTINS_COMPILED) and its cache entry.
# Entries: module names, .js files, directories or libraries of out-of-tree qjs-<name> modules.
macro(qjsm_init_builtins)
  # Convenience list mixing three kinds of entries, each routed to wherever it belongs:
  #   - a name with a lib/<name>.js or quickjs-<name>.c file -> BUILTINS_COMPILED or
  #     BUILTINS_NATIVE respectively (native wins if both exist, matching the
  #     C-is-authoritative precedent for dual-implementation modules elsewhere in this
  #     project, e.g. deep/pointer/predicate/xml/misc/path).
  #   - a directory path (an externally-built sibling project's build directory, e.g.
  #     ../qjs-glfw/build/x86_64-linux-gnu) -> EXTERNAL_MODULES. The module name is taken
  #     from a "qjs-<name>" ancestor directory of the path, and <name>_MODULE is set to
  #     the lib<name>.a/.so already built inside it (the OUTPUT_NAME convention used by
  #     e.g. qjs-glfw's/qjs-nanovg's qjs-<name>-static targets).
  # This is evaluated here, before BUILTINS_COMPILED's compile_module() loop's own output
  # targets (modules/*.c above, the "compiled" library) and BUILTINS_NATIVE/EXTERNAL_MODULES
  # are consumed further down, so an entry routed to any of the three actually takes effect.
  if(NOT DEFINED BUILTIN_MODULES)
    # a default cached by an earlier configure pins its list; drop it so new builtins are
    # picked up (a -DBUILTINS_COMPILED=... given on the command line is kept)
    if(DEFINED CACHE{BUILTINS_COMPILED})
      get_property(_BC_HELP CACHE BUILTINS_COMPILED PROPERTY HELPSTRING)
      if(_BC_HELP STREQUAL "Compiled .js modules to include in 'qjsm' binary")
        unset(BUILTINS_COMPILED CACHE)
      endif()
    endif()

    if(NOT DEFINED BUILTINS_COMPILED)
      set(BUILTINS_COMPILED
          assert
          assertStrict
          bun
          loaders/commonjs
          console
          events
          node/buffer
          node/child_process
          node/events
          node/os
          node/util
          fs
          fsPromises
          globals
          io
          iterator
          module
          node
          lazyGlobal
          loaders/node
          loaders/package-aliases
          loaders/packages
          password
          perf_hooks
          process
          readline
          readlinePromises
          repl
          require
          stream
          string_decoder
          terminal
          timers
          timersPromises
          tty
          loaders/typescript
          url
          util
          vfs
          webassembly)
    endif(NOT DEFINED BUILTINS_COMPILED)

    if(DEFINED CACHE{BUILTINS_NATIVE})
      get_property(_BC_HELP CACHE BUILTINS_NATIVE PROPERTY HELPSTRING)
      if(_BC_HELP STREQUAL "Native C modules to include in 'qjsm' binary")
        unset(BUILTINS_NATIVE CACHE)
      endif()
    endif()

    if(NOT DEFINED BUILTINS_NATIVE)
      set(BUILTINS_NATIVE bjson blob child_process deep directory inspect json misc mmap path syscallerror textcode xml)
    endif(NOT DEFINED BUILTINS_NATIVE)

    set_init(BUILTIN_MODULES ${BUILTINS_NATIVE} ${BUILTINS_COMPILED})

    unset(BUILTINS_NATIVE)
    unset(BUILTINS_NATIVE CACHE)
    unset(BUILTINS_COMPILED)
    unset(BUILTINS_COMPILED CACHE)
  endif()

  set(BUILTIN_MODULES
      "${BUILTIN_MODULES}"
      CACHE
        STRING
        "Mixed list: module names (auto-routed into BUILTINS_COMPILED/BUILTINS_NATIVE) or paths to externally-built qjs-<name> module build directories (routed into EXTERNAL_MODULES)"
  )
endmacro(qjsm_init_builtins)

# qjsm_resolve_builtins()
#
# splits BUILTIN_MODULES into BUILTINS_NATIVE/BUILTINS_COMPILED against what the tree has,
# prints the module tables and installs the lib/*.js, baked in or not.
macro(qjsm_resolve_builtins)
  list(LENGTH BUILTIN_MODULES _bm_count)
  message(STATUS "qjsm: resolving ${_bm_count} BUILTIN_MODULES entries against lib/*.js and quickjs-*.c")

  #
  # Get the native & compiled modules list
  #
  get_compiled_modules(COMPILED_MODULE_NAMES)
  get_native_modules(NATIVE_MODULES)

  set_remove(COMPILED_MODULE_NAMES ${NATIVE_MODULES})

  set_intersection(BUILTINS_NATIVE NATIVE_MODULES BUILTIN_MODULES)
  set_intersection(BUILTINS_COMPILED COMPILED_MODULE_NAMES BUILTIN_MODULES)

  # A compiled builtin never loads a .js file from disk, so everything it imports is compiled in
  # too: BUILTINS_NEEDED_JS and BUILTINS_NEEDED_NATIVE, found from the imports of the
  # BUILTINS_COMPILED and followed to the end. Not cached: take the builtin away and what only it
  # needed goes with it. Compiled in is BUILTIN_MODULES plus these.
  set(BUILTINS_NEEDED_JS "")
  set(BUILTINS_NEEDED_NATIVE "")
  set(_need_queue ${BUILTINS_COMPILED})
  qjsm_need_closure()
  set(BUILTINS_NEEDED ${BUILTINS_NEEDED_JS} ${BUILTINS_NEEDED_NATIVE})

  set(COLUMNS 80)

  message("")

  set_extra(INSTALLED_NATIVE NATIVE_MODULES ${BUILTINS_NATIVE} ${BUILTINS_NEEDED_NATIVE} ${BUILTINS_COMPILED})
  print_columns("=> Builtin C native modules" BUILTINS_NATIVE ${COLUMNS})

  qjsm_native_dependencies(BUILTIN_LINK_DEPS BUILTIN_RUNTIME_DEPS ${BUILTINS_NATIVE})
  if(BUILTIN_LINK_DEPS)
    string(REPLACE ";" "  " _deps "${BUILTIN_LINK_DEPS}")
    message("  links in:           ${_deps}")
    if(BUILTIN_RUNTIME_DEPS)
      string(REPLACE ";" "  " _deps "${BUILTIN_RUNTIME_DEPS}")
      message("  needs at run time:  ${_deps}")
    else()
      message("  needs at run time:  nothing, all linked statically")
    endif()
    message("")
  endif()

  print_columns("=> Installed C native modules" INSTALLED_NATIVE ${COLUMNS})

  set(INSTALLED_JS ${COMPILED_MODULE_NAMES})
  print_columns("=> Builtin JS modules" BUILTINS_COMPILED ${COLUMNS})

  # qjsc writes the bytecode while building, so the total is only known from an earlier build
  set(_bytecode_files "")
  foreach(_bc ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
    string(REPLACE "/" "_" _bc "${_bc}")
    list(APPEND _bytecode_files "${CMAKE_CURRENT_BINARY_DIR}/modules/${_bc}.c")
  endforeach()
  list(LENGTH _bytecode_files _bytecode_expected)
  qjsm_bytecode_size(_bytecode_total _bytecode_found ${_bytecode_files})
  if(_bytecode_found EQUAL _bytecode_expected AND _bytecode_found GREATER 0)
    qjsm_format_bytes(_bytecode_text ${_bytecode_total})
    message("  bytecode compiled into qjsm: ${_bytecode_text} in ${_bytecode_found} files, from the last build\n")
  elseif(_bytecode_expected GREATER 0)
    message("  bytecode compiled into qjsm: known after the build (printed when qjsm is linked)\n")
  endif()

  set(_computed_lines "")
  foreach(_cb ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/lib/${_cb}.js")
      qjsm_scan_computed_imports(_computed "${CMAKE_CURRENT_SOURCE_DIR}/lib/${_cb}.js")
      string(REPLACE ";" "  " _computed "${_computed}")
      if(_computed)
        list(APPEND _computed_lines "${_cb}  ${_computed}")
      endif()
    endif()
  endforeach()
  list(REMOVE_DUPLICATES _computed_lines)
  if(_computed_lines)
    message("=> Builtins with a computed import() (not followed at build time, resolved at run time)")
    message("")
    foreach(_line ${_computed_lines})
      message("  ${_line}")
    endforeach()
    message("")
  endif()

  if(BUILTINS_NEEDED)
    list(LENGTH BUILTINS_NEEDED _needed_count)
    message("=> Pulled in as dependencies of the builtins (${_needed_count}), compiled in as well")
    message("")
    foreach(_needed ${BUILTINS_NEEDED_JS} ${BUILTINS_NEEDED_NATIVE})
      if(_needed IN_LIST BUILTINS_NEEDED_JS)
        set(_needed_kind "js")
      else()
        set(_needed_kind "C")
      endif()
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _needed_key "${_needed}")
      string(REPLACE ";" " " _needed_by "${QJSM_NEEDED_BY_${_needed_key}}")
      message("  ${_needed} (${_needed_kind})  <-  ${_needed_by}")
    endforeach()
    message("")
  endif()

  list(LENGTH INSTALLED_JS _installed_js_count)
  message("=> Installed JS modules (${_installed_js_count})\n")

  make_filter_re(EXCLUDE_REGEX ${NATIVE_MODULES})

  #
  # Install all scripts in lib/ directory, builtins included: the debugger needs the
  # sources on disk
  #
  install(DIRECTORY lib/ DESTINATION "${QUICKJS_JS_MODULE_DIR}" FILES_MATCHING PATTERN "*.js"
          REGEX "lib/(${EXCLUDE_REGEX})\\.js$" EXCLUDE)

  # not cached (see the top of this file): pass -DBUILTINS_COMPILED=... to override
  string(REGEX REPLACE "[^A-Za-z0-9_]" ";" BUILTIN_LIBS "${BUILTINS_COMPILED}")
endmacro(qjsm_resolve_builtins)

# qjsm_declare_imports()
#
# <name>_MODULES: the modules a compiled lib/<name>.js imports, passed to qjsc as -M.
macro(qjsm_declare_imports)
  set(COMMON_MODULES "")

  set(abort_MODULES events)
  set(assert_MODULES inspect)
  set(assertStrict_MODULES assert)
  set(events_MODULES node/events)
  set(bun_MODULES node blob fs path stream textcode url lazyGlobal)
  set(node_MODULES loaders/node loaders/packages loaders/commonjs lazyGlobal)
  set(loaders_commonjs_MODULES require path)
  set(console_MODULES util inspect)
  set(db_MODULES util list)
  set(dom_MODULES fs util timers css3-selectors deep pointer tree_walker xml url file)
  set(extendArrayBuffer_MODULES util misc)
  set(extendArray_MODULES util)
  set(extendAsyncFunction_MODULES util)
  set(extendAsyncGenerator_MODULES asyncIterator)
  set(extendFunction_MODULES util)
  set(extendGenerator_MODULES iterator)
  set(extendMap_MODULES util)
  set(extendSet_MODULES util)
  set(file_MODULES blob)
  set(fs_MODULES blob node/events os path misc mmap std syscallerror textcode directory)
  set(fsPromises_MODULES fs)
  set(globals_MODULES timers fs stream blob events textcode url abort streams perf_hooks intl)
  set(timersPromises_MODULES timers)
  set(io_MODULES util misc)
  set(module_MODULES fs)
  set(parser_MODULES path util list)
  set(password_MODULES bcrypt)
  set(process_MODULES path util misc node/events)
  set(readline_MODULES node/events textcode misc timers)
  set(readlinePromises_MODULES readline)
  set(repl_MODULES util inspect path io process terminal)
  set(loaders_node_MODULES loaders/nodeNames)
  set(require_MODULES util path process loaders/nodeNames)
  set(perf_hooks_MODULES misc timers)
  set(simple_queue_MODULES assert)
  set(stream_MODULES abort assert simple-queue util)
  set(streams_MODULES fs stream util std textcode)
  set(terminal_MODULES misc)
  set(tty_MODULES util misc)
  set(url_MODULES util path)
  set(util_MODULES timers inspect path misc textcode node/util)
  set(string_decoder_MODULES misc textcode)
  set(vfs_MODULES fs syscallerror textcode directory)
  set(webassembly_MODULES wasm)
  set(node_buffer_MODULES misc blob textcode)
  set(node_child_process_MODULES child_process node/events node/buffer misc sockets process textcode)
  set(node_os_MODULES misc node/buffer process)
  set(node_util_MODULES assert misc textcode)
  set(inspect_MODULES pointer)
endmacro(qjsm_declare_imports)

# qjsm_compile_builtins()
#
# compiles each BUILTINS_COMPILED lib/<name>.js to modules/<name>.c with qjsc, collects
# COMPILED_BUILTIN_HEADER and the "compiled" static library.
macro(qjsm_compile_builtins)
  set(COMPILED_BUILTIN_HEADER "")

  set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}\n/* Compiled .js builtins */\n")

  list(LENGTH BUILTINS_COMPILED _qjsc_count)
  message(
    STATUS
      "qjsc: compiling ${_qjsc_count} lib/*.js builtins to bytecode in ${CMAKE_CURRENT_BINARY_DIR}/modules (${QJSC})")

  foreach(LIB_MODULE ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
    set(MODLIST ${COMMON_MODULES} ${${LIB_MODULE}_MODULES})

    module_path("${LIB_MODULE}.c" LIB_MODULE_OUT)
    compile_module(lib/${LIB_MODULE}.js "${LIB_MODULE_OUT}" ${MODLIST})

    qjsm_header_compiled("${LIB_MODULE}")
  endforeach(LIB_MODULE ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
  set(QJSM_COMPILED_NEEDED ${BUILTINS_NEEDED_JS})

  dump_list(COMPILED_MODULES)
  dump(COMPILED_MODULES)

  if(NOT COMPILED_MODULES)
    message(WARNING "No compiled modules")
  else()
    add_library(compiled STATIC ${COMPILED_MODULES})
  endif()
endmacro(qjsm_compile_builtins)

# qjsm_process_builtins()
#
# after the native modules exist: routes BUILTIN_MODULES entries, closes the native
# dependencies, fills QJSM_SOURCES/QJSM_LDADD/QJSM_STATIC_LIBS and writes quickjs-builtins.h.
macro(qjsm_process_builtins)
  unset(QJSM_LDADD)

  set(QJSM_SOURCES src/qjsm.c)

  # the names BUILTIN_MODULES accepts: quickjs-<name>.c as <name> ('-' written '_') and
  # lib/<name>.js; get_native_modules()/get_compiled_modules() list them
  foreach(BUILTIN_MODULE ${BUILTIN_MODULES})
    string(REPLACE "_" "-" BUILTIN_NATIVE_NAME "${BUILTIN_MODULE}")
    string(REPLACE "-" "_" BUILTIN_NATIVE_KEY "${BUILTIN_MODULE}")

    # a path entry may be relative to the source dir
    get_filename_component(_bm_path "${BUILTIN_MODULE}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")

    if(IS_DIRECTORY "${_bm_path}")
      set(_bm_dir "${_bm_path}")
      set(_bm_name "")
      while(NOT "${_bm_dir}" STREQUAL "" AND NOT "${_bm_dir}" STREQUAL "/")
        get_filename_component(_bm_base "${_bm_dir}" NAME)
        if(_bm_base MATCHES "^qjs-(.+)$")
          set(_bm_name "${CMAKE_MATCH_1}")
          break()
        endif()
        get_filename_component(_bm_dir "${_bm_dir}" DIRECTORY)
      endwhile()

      if("${_bm_name}" STREQUAL "")
        message(
          WARNING
            "BUILTIN_MODULES entry '${BUILTIN_MODULE}' is a directory but no ancestor directory is named 'qjs-<name>' -- skipping"
        )
      else()
        # quickjs-<mod>.module.cmake sidecars (one per module a project builds, written by
        # make_module()): <mod>_MODULE_LIBRARY = the archive, _LIBRARIES = what it links
        file(GLOB _bm_sidecars "${_bm_path}/quickjs-*.module.cmake")
        if(_bm_sidecars)
          foreach(_bm_sidecar ${_bm_sidecars})
            qjsm_external_sidecar("${_bm_sidecar}")
          endforeach()
        else()
          find_library(_bm_lib_${_bm_name} NAMES "lib${_bm_name}.a" "quickjs-${_bm_name}.a" "${_bm_name}"
                       PATHS "${_bm_path}" NO_DEFAULT_PATH)
          if(_bm_lib_${_bm_name})
            list(APPEND EXTERNAL_MODULES "${_bm_name}")
            message(STATUS "qjsm: external module '${_bm_name}' from ${_bm_lib_${_bm_name}}")
            # No FORCE: an explicit <name>_MODULE (e.g. listing the module's own system
            # libraries, which a bare archive cannot tell us) wins over this bare path.
            set(${_bm_name}_MODULE "${_bm_lib_${_bm_name}}"
                CACHE STRING "Static library for external module '${_bm_name}', found via BUILTIN_MODULES")
          else()
            message(
              WARNING
                "BUILTIN_MODULES entry '${BUILTIN_MODULE}' -- no quickjs-*.module.cmake and no lib${_bm_name}.a in that directory"
            )
          endif()
        endif()
      endif()
    elseif(EXISTS "${_bm_path}" AND _bm_path MATCHES "quickjs-[^/]*\\.module\\.cmake$")
      # one module of a multi-module project (e.g. qjs-sound/build/quickjs-sndfile.module.cmake)
      qjsm_external_sidecar("${_bm_path}")
    elseif(EXISTS "${_bm_path}" AND _bm_path MATCHES "\\.js$")
      # a JS file: compiled into qjsm as builtin <basename>
      get_filename_component(_bm_name "${_bm_path}" NAME_WE)
      if(_bm_name MATCHES "[^A-Za-z0-9_]")
        message(WARNING "BUILTIN_MODULES entry '${BUILTIN_MODULE}' -- '${_bm_name}' is not a C identifier, skipping")
      elseif(NOT _bm_name IN_LIST BUILTINS_COMPILED)
        list(APPEND BUILTINS_COMPILED "${_bm_name}")
        message(STATUS "qjsc: compiling ${_bm_path} to bytecode, builtin '${_bm_name}'")
        set(MODLIST ${COMMON_MODULES} ${${_bm_name}_MODULES})
        module_path("${_bm_name}.c" BUILTIN_MODULE_OUT)
        # compile_module() prefixes the source dir, so hand it a relative path
        file(RELATIVE_PATH _bm_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_bm_path}")
        compile_module("${_bm_rel}" "${BUILTIN_MODULE_OUT}" ${MODLIST})

        qjsm_scan_imports(_specs "${_bm_path}")
        set(_need_queue "")
        qjsm_need_specs("${_bm_name}")
        qjsm_need_closure()
        qjsm_header_compiled("${_bm_name}")
      endif()
    elseif(EXISTS "${_bm_path}" AND _bm_path MATCHES "\\.(a|so|dylib|lib)$")
      # a prebuilt module library: lib<name>.a / <name>.so -> external module <name>
      get_filename_component(_bm_name "${_bm_path}" NAME_WE)
      string(REGEX REPLACE "^(lib|quickjs-)" "" _bm_name "${_bm_name}")
      list(APPEND EXTERNAL_MODULES "${_bm_name}")
      message(STATUS "qjsm: external module '${_bm_name}' from ${_bm_path}")
      set(${_bm_name}_MODULE "${_bm_path}"
          CACHE STRING "Static library for external module '${_bm_name}', given via BUILTIN_MODULES")
    elseif(BUILTIN_NATIVE_KEY IN_LIST NATIVE_MODULES)
      if(NOT BUILTIN_NATIVE_NAME IN_LIST BUILTINS_NATIVE)
        list(APPEND BUILTINS_NATIVE "${BUILTIN_NATIVE_NAME}")
      endif()
    elseif(BUILTIN_MODULE IN_LIST COMPILED_MODULE_NAMES)
      #message("BUILTIN_MODULE: ${BUILTIN_MODULE}")

      #[[if(BUILTIN_MODULE MATCHES "[^A-Za-z0-9_/]")
      message(WARNING "BUILTIN_MODULES entry '${BUILTIN_MODULE}' is not a C identifier (a '-' or '/' in lib/${BUILTIN_MODULE}.js) -- the builtin table needs one, skipping")
    else]]
      if(NOT BUILTIN_MODULE IN_LIST BUILTINS_COMPILED)
        list(APPEND BUILTINS_COMPILED "${BUILTIN_MODULE}")
        set(MODLIST ${COMMON_MODULES} ${${BUILTIN_MODULE}_MODULES})
        module_path("${BUILTIN_MODULE}.c" BUILTIN_MODULE_OUT)
        compile_module(lib/${BUILTIN_MODULE}.js "${BUILTIN_MODULE_OUT}" ${MODLIST})

        set(_need_queue "${BUILTIN_MODULE}")
        qjsm_need_closure()

        qjsm_header_compiled("${BUILTIN_MODULE}")
      endif()
    else()
      message(
        WARNING
          "BUILTIN_MODULES entry '${BUILTIN_MODULE}' matches no lib/<name>.js, quickjs-<name>.c, .js/.a/.so file or existing directory -- skipping (native: ${NATIVE_MODULES}; compiled: ${COMPILED_MODULE_NAMES})"
      )
    endif()
  endforeach()

  # what the modules routed above need and the first pass did not know about
  foreach(_lm ${BUILTINS_NEEDED_JS})
    if(NOT _lm IN_LIST QJSM_COMPILED_NEEDED)
      list(APPEND QJSM_COMPILED_NEEDED "${_lm}")
      set(MODLIST ${COMMON_MODULES} ${${_lm}_MODULES})
      module_path("${_lm}.c" BUILTIN_MODULE_OUT)
      compile_module(lib/${_lm}.js "${BUILTIN_MODULE_OUT}" ${MODLIST})
      qjsm_header_compiled("${_lm}")
      string(REGEX REPLACE "[^A-Za-z0-9_]" "_" _needed_key "${_lm}")
      message(STATUS "qjsm: also compiling lib/${_lm}.js, needed by ${QJSM_NEEDED_BY_${_needed_key}}")
    endif()
  endforeach()

  # Names of externally-built native modules to statically link into qjsm, each
  # providing a js_init_module_<name>(JSContext*, const char*) entry point
  # (see the JS_SHARED_LIBRARY / JS_INIT_MODULE convention used by every
  # quickjs-*.c in this tree). For each entry "foo" in this list, <foo>_MODULE
  # must be set to the static library (target name or full path, plus any of its
  # own transitive link dependencies as a ";"-separated list) that defines
  # js_init_module_foo -- e.g.:
  #   -DEXTERNAL_MODULES=foo -Dfoo_MODULE=/path/to/libfoo.a
  # Named "<foo>_MODULE", not "<foo>_LIBRARY", to avoid colliding with the
  # find_library()/find_package() "<pkg>_LIBRARY" convention used elsewhere in
  # this file (e.g. a real find_package(nanovg) would also set nanovg_LIBRARY).
  set(EXTERNAL_MODULES "${EXTERNAL_MODULES}" CACHE STRING
                                                   "Externally-built native modules to statically link into qjsm")

  set(NATIVE_BUILTIN_HEADER "")
  set(QJSM_STATIC_LIBS "")

  set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}/* native C builtins */\n")

  # BUILTINS_NATIVE plus the native modules they link (module_depends), transitively
  qjsm_native_closure(_native_all ${BUILTINS_NATIVE} ${BUILTINS_NEEDED_NATIVE})

  set(_native_pulled "${_native_all}")
  foreach(_nb ${BUILTINS_NATIVE})
    string(REPLACE "_" "-" _nb "${_nb}")
    list(REMOVE_ITEM _native_pulled "${_nb}")
  endforeach()
  if(_native_pulled)
    message(STATUS "qjsm: native modules linked in as dependencies: ${_native_pulled}")
  endif()

  foreach(NATIVE_BUILTIN ${_native_all})
    string(REPLACE "_" "-" NATIVE_SOURCE "${NATIVE_BUILTIN}")
    string(REPLACE "-" "_" NATIVE_MODULE_VAR "${NATIVE_BUILTIN}")
    # system libraries of the module (not the qjs-* targets, whose sources are compiled in); the
    # static variants are what an executable links
    foreach(_ld_lib ${${NATIVE_BUILTIN}_LIBRARIES} ${${NATIVE_MODULE_VAR}_LIBRARIES}
                    ${${NATIVE_MODULE_VAR}_LIBRARIES_static})
      if(NOT _ld_lib MATCHES "^qjs-")
        list(APPEND QJSM_LDADD "${_ld_lib}")
      endif()
    endforeach()
    list(APPEND QJSM_SOURCES quickjs-${NATIVE_SOURCE}.c)

    # sources the module needs besides its quickjs-<name>.c, unless libmodules already has them
    foreach(_src ${${NATIVE_MODULE_VAR}_SOURCES})
      get_filename_component(_src_abs "${_src}" ABSOLUTE)
      set(_in_library FALSE)
      foreach(_lib_src ${LIBRARY_SOURCES})
        get_filename_component(_lib_abs "${_lib_src}" ABSOLUTE)
        if(_lib_abs STREQUAL _src_abs)
          set(_in_library TRUE)
        endif()
      endforeach()
      if(NOT _in_library AND NOT _src MATCHES "^quickjs-")
        list(APPEND QJSM_SOURCES "${_src}")
      endif()
    endforeach()
    string(REGEX REPLACE "-" "_" NATIVE_MODULE "${NATIVE_BUILTIN}")
    set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}BUILTIN_NATIVE(${NATIVE_MODULE})\n")
  endforeach()

  if(EXTERNAL_MODULES)
    list(REMOVE_DUPLICATES EXTERNAL_MODULES)
  endif()
  unset(QJSM_EXTERNAL_TARGETS)
  # externally-built modules: entry point js_init_module_<name> in <name>_MODULE's archive
  foreach(_ext ${EXTERNAL_MODULES})
    string(REPLACE "-" "_" _ext_id "${_ext}")
    set(_ext_libs ${${_ext}_MODULE})
    list(GET _ext_libs 0 _ext_archive)
    # the archive carries its own copies of helpers (js_error_print, js_function_prototype, ...)
    # that libmodules and other archives define too; partial-link it and keep only the entry
    # point global so the copies stay private
    if(UNIX AND CMAKE_OBJCOPY AND CMAKE_AR AND _ext_archive MATCHES "\\.a$")
      set(_ext_dir "${CMAKE_CURRENT_BINARY_DIR}/external")
      set(_ext_out "${_ext_dir}/quickjs-${_ext}.a")
      add_custom_command(
        OUTPUT "${_ext_out}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${_ext_dir}"
        COMMAND ${CMAKE_C_COMPILER} -r -nostdlib -Wl,--whole-archive "${_ext_archive}" -Wl,--no-whole-archive -o
                "${_ext_dir}/${_ext}.o"
        COMMAND ${CMAKE_OBJCOPY} --keep-global-symbol=js_init_module_${_ext_id} "${_ext_dir}/${_ext}.o"
        COMMAND ${CMAKE_COMMAND} -E remove -f "${_ext_out}"
        COMMAND ${CMAKE_AR} rcs "${_ext_out}" "${_ext_dir}/${_ext}.o"
        DEPENDS "${_ext_archive}"
        COMMENT "qjsm: isolating the symbols of external module ${_ext}"
        VERBATIM)
      add_custom_target(qjsm-external-${_ext} DEPENDS "${_ext_out}")
      list(APPEND QJSM_EXTERNAL_TARGETS qjsm-external-${_ext})
      list(REMOVE_AT _ext_libs 0)
      list(PREPEND _ext_libs "${_ext_out}")
    endif()
    list(APPEND QJSM_STATIC_LIBS ${_ext_libs})
    if(${_ext}_MODULE_LINK_FLAGS)
      separate_arguments(_ext_flags UNIX_COMMAND "${${_ext}_MODULE_LINK_FLAGS}")
      list(APPEND QJSM_EXTERNAL_LINK_OPTIONS ${_ext_flags})
    endif()
    set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}BUILTIN_NATIVE(${_ext_id})\n")
  endforeach()

  if(BUILTIN_HEADER)
    message(STATUS "Builtin modules:\n${BUILTIN_HEADER}")
  endif()

  set(BUILTIN_HEADER "#ifndef BUILTIN_NATIVE\n#define BUILTIN_NATIVE(x)\n#endif\n")
  set(BUILTIN_HEADER
      "${BUILTIN_HEADER}\n#ifndef BUILTIN_COMPILED\n#define BUILTIN_COMPILED(x)\n#endif\n\n#ifndef BUILTIN_COMPILED_AS\n#define BUILTIN_COMPILED_AS(x, name)\n#endif\n"
  )

  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-builtins.h"
       "${BUILTIN_HEADER}\n${NATIVE_BUILTIN_HEADER}\n${COMPILED_BUILTIN_HEADER}")
  message(STATUS "qjsm: wrote ${CMAKE_CURRENT_BINARY_DIR}/quickjs-builtins.h")
endmacro(qjsm_process_builtins)

# qjsm_add_executable()
#
# the qjsm target: sources, link line, rpath, definitions, install rule.
macro(qjsm_add_executable)
  add_executable(qjsm ${QJSM_SOURCES})
  target_link_libraries(qjsm PUBLIC ${LIBM})

  if(COMPILED_TARGETS)
    add_dependencies(qjsm ${COMPILED_TARGETS})
  endif(COMPILED_TARGETS)

  if(QJSM_EXTERNAL_TARGETS)
    add_dependencies(qjsm ${QJSM_EXTERNAL_TARGETS})
  endif()

  if(LIBDL)
    link_libraries(${LIBDL})
  endif(LIBDL)

  link_directories(${QJSM_LIBDIR})

  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${QJSM_LIBDIR}")
  set_target_properties(qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT};-L${QJSM_LIBDIR};${QJSM_EXTERNAL_LINK_OPTIONS}"
                                        COMPILE_FLAGS "-w")

  if(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    # -lquickjs pulls in quickjs-libc.c's js_std_file/std.popen(), which calls
    # the real popen() -- something the wasm sandbox has no equivalent for.
    # Mirrors the same allowance the parent quickjs project makes for its own
    # qjs/qjsm executables: leave the symbol unresolved at link time instead of
    # failing the build; std.popen() will simply be unavailable at runtime.
    # Scoped to just this target (not CMAKE_EXE_LINKER_FLAGS) so it doesn't
    # also mask undefined symbols in the check_library_exists()/
    # check_function_exists() probes done earlier in this file. Must come
    # after the set_target_properties(... LINK_OPTIONS ...) above, which
    # overwrites (not appends to) that property.
    target_link_options(qjsm PRIVATE -sERROR_ON_UNDEFINED_SYMBOLS=0)
  endif()

  if(BUILD_SHARED_LIBS)
    set_target_properties(qjsm PROPERTIES RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}"
                                          INSTALL_RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}")
  endif(BUILD_SHARED_LIBS)

  # cmake --install strips the build RUNPATH; QUICKJS_LIBRARY_DIR is already absolute.
  set_property(TARGET qjsm APPEND PROPERTY INSTALL_RPATH "${QUICKJS_INSTALL_LIBRARY_DIR}")

  target_link_directories(qjsm PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${QUICKJS_LIBRARY_DIR})

  if(LIBPTHREAD_LIBRARY)
    # Passing LIBPTHREAD_LIBRARY's path to target_link_libraries() directly
    # would get collapsed back down to a bare "-lpthread" by CMake (its
    # directory is also one of the compiler's own implicit link
    # directories), reintroducing the wrong-copy problem find_library()
    # above was trying to avoid. Passing "-L<dir>"/"-lpthread" as raw
    # linker-flag strings (leading "-") instead makes CMake pass them
    # through untouched, in the position given -- unlike
    # target_link_options(), which always places its flags before the
    # link libraries, too early to satisfy symbols the archives further
    # below still need.
    get_filename_component(LIBPTHREAD_DIR "${LIBPTHREAD_LIBRARY}" DIRECTORY)
    set(LIBPTHREAD "-L${LIBPTHREAD_DIR}" -lpthread)
  endif(LIBPTHREAD_LIBRARY)

  target_link_libraries(qjsm PUBLIC ${QJSM_LDADD} ${QJSM_STATIC_LIBS} ${COMMON_LIBRARIES} ${QUICKJS_LIBRARY}
                                    ${LIBPTHREAD} ${LIBM} ${LIBWS2_32})

  target_compile_definitions(
    qjsm PRIVATE _GNU_SOURCE=1 CONFIG_VERSION="${QUICKJS_VERSION}" QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}"
                 LIBMAGIC_DB="${LIBMAGIC_DB}" ${CONFIG_DEFS})

  install(TARGETS qjsm DESTINATION bin)

  # the bytecode only exists once qjsc has run
  set(_bytecode_files "")
  foreach(_bc ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
    string(REPLACE "/" "_" _bc "${_bc}")
    list(APPEND _bytecode_files "${CMAKE_CURRENT_BINARY_DIR}/modules/${_bc}.c")
  endforeach()
  string(REPLACE ";" "|" _bytecode_files "${_bytecode_files}")
  add_custom_command(TARGET qjsm POST_BUILD COMMAND "${CMAKE_COMMAND}" "-DFILES=${_bytecode_files}" -P
                                                    "${_QJSM_CMAKE_DIR}/qjsm-bytecode.cmake" VERBATIM)
endmacro(qjsm_add_executable)

# qjsm_report_compat()
#
# Node.js, Bun and Web compatibility are not switches: they are the builtins that provide them.
# This lists each one present in the build, with the specifiers it provides, what it rewrites
# and the globals it defines. Prints nothing when none of their builtins is compiled in.
#
#   -- compatibility: Node.js
#   --   builtins   loaders/node node/buffer ...
#   --   provides   node:buffer node:child_process ...
#   --   rewrites   node:<name>  ->  node_<name> if that builtin exists, else <name>
#   --   globals    process
macro(qjsm_report_compat)
  set(_cb ${BUILTINS_COMPILED} ${BUILTINS_NEEDED_JS})
  set(_cn ${BUILTINS_NATIVE} ${BUILTINS_NEEDED_NATIVE})

  # Node.js
  set(_node_builtins "")
  foreach(_m loaders/node loaders/packages node/buffer node/child_process node/events node/os node/util module process
             fsPromises timersPromises assertStrict readlinePromises string_decoder)
    if(_m IN_LIST _cb)
      list(APPEND _node_builtins "${_m}")
    endif()
  endforeach()

  if(_node_builtins)
    set(_provides "")
    set(_rewrites "")
    set(_globals "")

    foreach(_m ${_node_builtins})
      if(_m MATCHES "^node/(.*)$")
        list(APPEND _provides "node:${CMAKE_MATCH_1}")
      endif()
    endforeach()

    if("module" IN_LIST _node_builtins)
      list(APPEND _provides "node:module")
    endif()

    if("loaders/node" IN_LIST _node_builtins)
      list(APPEND _rewrites "node:<name>  ->  node_<name> if that builtin exists, else <name> (node:os is left alone)"
           "run by the loaders/node resolve hook, off with -N / --no-node")
    endif()

    if("loaders/packages" IN_LIST _node_builtins)
      list(APPEND _rewrites "pkg, @scope/pkg/sub, #name  ->  node_modules and package.json exports/imports/main"
           "run by the loaders/packages resolve hook, off with -N / --no-node")
    endif()

    foreach(_alias "fsPromises:fs/promises" "timersPromises:timers/promises" "assertStrict:assert/strict"
                   "readlinePromises:readline/promises")
      string(REPLACE ":" ";" _pair "${_alias}")
      list(GET _pair 0 _target)
      list(GET _pair 1 _from)
      if(_target IN_LIST _node_builtins)
        list(APPEND _rewrites "${_from}  ->  ${_target}   (in the loader, no hook)")
      endif()
    endforeach()

    if("process" IN_LIST _node_builtins)
      list(APPEND _globals "process (off with -P / --no-process)")
    endif()

    wrap_list(_node_lines 56 ${_node_builtins})
    wrap_list(_provides_lines 56 ${_provides})
    message("")
    message_table("compatibility: Node.js" "builtins" "${_node_lines}" "provides" "${_provides_lines}" "rewrites"
                  "${_rewrites}" "globals" "${_globals}")
  endif()

  # module-alias
  if("loaders/package-aliases" IN_LIST _cb)
    message("")
    message_table(
      "compatibility: module-alias (package.json)" "builtins" "loaders/package-aliases" "provides"
      "nothing, it only rewrites" "rewrites"
      "a specifier named in ./package.json \"_moduleAliases\" -> its target, after the builtins" "globals" "none")
  endif()

  # Bun
  if("bun" IN_LIST _cb)
    message("")
    message_table(
      "compatibility: Bun" "builtins" "bun" "provides"
      "bun (Bun.plugin(), Bun.file(), Bun.write(), Bun.stdin/stdout/stderr)" "rewrites"
      "none itself, plugins registered by a script resolve and load" "globals" "Bun")
  endif()

  # Web platform
  if("globals" IN_LIST _cb)
    wrap_list(_web_lines 56 URL URLSearchParams TextEncoder TextDecoder AbortController AbortSignal EventTarget Blob
              atob btoa setTimeout clearTimeout setInterval clearInterval setImmediate clearImmediate queueMicrotask
              structuredClone crypto performance "the WHATWG streams classes")
    message("")
    message_table("compatibility: Web platform" "builtins" "globals" "provides"
                  "globals (imported for its side effect)" "rewrites" "none" "globals" "${_web_lines}")
  endif()
endmacro(qjsm_report_compat)

# qjsm_report()
#
# the end-of-configure summary of the qjsm build: what is baked in and from where, what
# qjsc generates, how the binary finds modules and libraries at run time, and which
# compatibility layers came with the builtins. Nothing at all when no builtin is compiled in.
#
#   -- qjsm: what is baked in
#   --   C modules (13)  bjson blob ...
macro(qjsm_report)
  if(QJSM_LAYOUT_REPORT)
    message("${QJSM_LAYOUT_REPORT}")
  endif()

  set(_rep_native_names "")
  foreach(_rep_name ${BUILTINS_NATIVE})
    string(REPLACE "_" "-" _rep_name "${_rep_name}")
    list(APPEND _rep_native_names "${_rep_name}")
  endforeach()
  list(REMOVE_DUPLICATES _rep_native_names)
  list(REMOVE_DUPLICATES QJSM_LDADD)
  list(LENGTH _rep_native_names _rep_native)
  list(LENGTH BUILTINS_COMPILED _rep_compiled)
  list(LENGTH BUILTINS_NEEDED_JS _rep_needed_js)
  list(LENGTH COMPILED_MODULES _rep_bytecode)
  list(LENGTH EXTERNAL_MODULES _rep_external)

  math(EXPR _rep_all "${_rep_native} + ${_rep_compiled} + ${_rep_needed_js} + ${_rep_external}")
  list(LENGTH _native_pulled _rep_pulled)
  math(EXPR _rep_all "${_rep_all} + ${_rep_pulled}")

  if(_rep_all GREATER 0)
    wrap_list(_rep_native_lines 56 ${_rep_native_names})
    wrap_list(_rep_compiled_lines 56 ${BUILTINS_COMPILED})
    wrap_list(_rep_needed_lines 56 ${BUILTINS_NEEDED_JS})
    wrap_list(_rep_pulled_lines 56 ${_native_pulled})
    wrap_list(_rep_ldadd_lines 56 ${QJSM_LDADD})

    message("")
    message_table(
      "qjsm: what is baked in"
      "C modules (${_rep_native})"
      "${_rep_native_lines}"
      "C modules, as dependencies (${_rep_pulled})"
      "${_rep_pulled_lines}"
      "JS modules, bytecode (${_rep_compiled})"
      "${_rep_compiled_lines}"
      "JS modules, as dependencies (${_rep_needed_js})"
      "${_rep_needed_lines}"
      "external modules (${_rep_external})"
      "${EXTERNAL_MODULES}"
      "libraries linked"
      "${_rep_ldadd_lines}"
      "static libraries"
      "${QJSM_STATIC_LIBS}")

    if(_rep_bytecode GREATER 0)
      message_table(
        "qjsc: generated files" "compiler" "${QJSC}" "bytecode sources"
        "${_rep_bytecode} in ${CMAKE_CURRENT_BINARY_DIR}/modules" "bytecode library"
        "${CMAKE_CURRENT_BINARY_DIR}/libcompiled.a" "builtin table" "${CMAKE_CURRENT_BINARY_DIR}/quickjs-builtins.h")
    endif()

    qjsm_report_compat()
  endif()

  message_table(
    "qjsm: executable" "built as" "${CMAKE_CURRENT_BINARY_DIR}/qjsm" "installed as" "${CMAKE_INSTALL_PREFIX}/bin/qjsm"
    "default module path" "${QUICKJS_MODULE_PATH}" "libquickjs" "${QUICKJS_LIBRARY}" "run time library dir"
    "${QUICKJS_INSTALL_LIBRARY_DIR}" "override at run time" "QUICKJS_MODULE_PATH=<dir>[:<dir>...]")
endmacro(qjsm_report)
