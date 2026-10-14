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
#   qjsm_add_executable       the qjsm target
#
# macros, not functions: they set variables the rest of CMakeLists.txt reads.

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
              "${CMAKE_CURRENT_SOURCE_DIR}/cmake/Functions.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/Compat.cmake;${CMAKE_CURRENT_SOURCE_DIR}/cmake/qjsm.cmake")
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

# qjsm_init_builtins()
#
# BUILTIN_MODULES default (BUILTINS_NATIVE + BUILTINS_COMPILED) and its cache entry.
# Entries: module names, .js files, directories or libraries of out-of-tree qjs-<name> modules.
macro(qjsm_init_builtins)
# Convenience list mixing three kinds of entries, each routed to wherever it belongs:
#   - a name with a lib/<name>.js or quickjs-<name>.c file -> BUILTINS_COMPILED or
#     BUILTINS_NATIVE respectively (native wins if both exist, matching the
#     C-is-authoritative precedent for dual-implementation modules elsewhere in this
#     project, e.g. deep/pointer/predicate/xml/stream/misc/path).
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
       set(BUILTINS_COMPILED assert assertStrict bun commonjs console events node/buffer node/child_process node/events node/util fs fsPromises globals io iterator module nodeHooks password perf_hooks process readline readlinePromises repl require stream terminal timers timersPromises tty typescript url util vfs webassembly)
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

set(BUILTIN_MODULES "${BUILTIN_MODULES}" CACHE STRING "Mixed list: module names (auto-routed into BUILTINS_COMPILED/BUILTINS_NATIVE) or paths to externally-built qjs-<name> module build directories (routed into EXTERNAL_MODULES)")
endmacro(qjsm_init_builtins)

# qjsm_resolve_builtins()
#
# splits BUILTIN_MODULES into BUILTINS_NATIVE/BUILTINS_COMPILED against what the tree has,
# prints the module tables and installs the lib/*.js that are not baked in.
macro(qjsm_resolve_builtins)
#
# Get the native & compiled modules list
#
get_compiled_modules(COMPILED_MODULE_NAMES)
get_native_modules(NATIVE_MODULES)

list(FILTER NATIVE_MODULES EXCLUDE REGEX "^(stream)$")

set_remove(COMPILED_MODULE_NAMES ${NATIVE_MODULES})

set_intersection(BUILTINS_NATIVE NATIVE_MODULES BUILTIN_MODULES)
set_intersection(BUILTINS_COMPILED COMPILED_MODULE_NAMES BUILTIN_MODULES)

#dump_list(BUILTINS_NATIVE BUILTINS_COMPILED)

set(COLUMNS 80)

message("")

set_extra(INSTALLED_NATIVE NATIVE_MODULES ${BUILTINS_NATIVE} ${BUILTINS_COMPILED})
print_columns("=> Builtin C native modules" BUILTINS_NATIVE ${COLUMNS})
print_columns("=> Installed C native modules" INSTALLED_NATIVE ${COLUMNS})

set_extra(INSTALLED_JS COMPILED_MODULE_NAMES ${BUILTINS_NATIVE} ${BUILTINS_COMPILED})
print_columns("=> Builtin JS modules" BUILTINS_COMPILED ${COLUMNS})
print_columns("=> Installed JS modules" INSTALLED_JS ${COLUMNS})

make_filter_re(EXCLUDE_REGEX ${NATIVE_MODULES} ${BUILTINS_COMPILED})

#
# Install all scripts in lib/ directory
#
install(
  DIRECTORY lib/
  DESTINATION "${QUICKJS_JS_MODULE_DIR}"
  FILES_MATCHING PATTERN "*.js"
  REGEX "lib/(${EXCLUDE_REGEX})\\.js$" EXCLUDE
)

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
set(commonjs_MODULES require path)
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
set(fs_MODULES node/events os path misc mmap std syscallerror textcode directory)
set(fsPromises_MODULES fs misc textcode)
set(globals_MODULES timers fs stream blob events textcode url abort streams perf_hooks)
set(timersPromises_MODULES timers)
set(io_MODULES util misc)
set(module_MODULES fs)
set(parser_MODULES path util list)
set(password_MODULES bcrypt)
set(process_MODULES path util misc node:events)
set(readline_MODULES node:events textcode misc timers)
set(readlinePromises_MODULES readline)
set(repl_MODULES util inspect path io process)
set(require_MODULES util path process)
set(perf_hooks_MODULES misc timers)
set(simple_queue_MODULES assert)
set(stream_MODULES abort assert simple-queue util)
set(streams_MODULES fs stream util std textcode)
set(terminal_MODULES misc process)
set(tty_MODULES util misc)
set(url_MODULES util path)
set(util_MODULES timers inspect path misc textcode)
set(vfs_MODULES fs fs fsPromises path util archive extendArray io queue)
set(webassembly_MODULES wasm)
set(node_buffer_MODULES misc blob)
set(node_child_process_MODULES child_process node:events node:buffer misc sockets process)
set(node_util_MODULES assert misc)
set(inspect_MODULES pointer)
endmacro(qjsm_declare_imports)

# qjsm_compile_builtins()
#
# compiles each BUILTINS_COMPILED lib/<name>.js to modules/<name>.c with qjsc, collects
# COMPILED_BUILTIN_HEADER and the "compiled" static library.
macro(qjsm_compile_builtins)
set(COMPILED_BUILTIN_HEADER "")

set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}\n/* Compiled .js builtins */\n")

foreach(LIB_MODULE ${BUILTINS_COMPILED})
  set(MODLIST ${COMMON_MODULES} ${${LIB_MODULE}_MODULES})

  module_path("${LIB_MODULE}.c" LIB_MODULE_OUT)
  compile_module(lib/${LIB_MODULE}.js "${LIB_MODULE_OUT}" ${MODLIST})

  string(REGEX REPLACE "[/]" "_" BC_NAME "${LIB_MODULE}")

  set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED(${BC_NAME})\n")
endforeach(LIB_MODULE ${BUILTINS_COMPILED})

# lib/node/<name>.js: Node-shaped layers, builtin "node_<name>", mapped from `node:<name>` by lib/nodeHooks.js
#[[set(BUILTINS_NODE buffer child_process events util)
set(node_buffer_MODULES misc blob)
set(node_child_process_MODULES child_process node:events node:buffer misc sockets process)
set(node_events_MODULES)
set(node_util_MODULES util inspect misc textcode assert)

foreach(NODE_MODULE ${BUILTINS_NODE})
  set(LIB_MODULE node_${NODE_MODULE})
  set(MODLIST ${COMMON_MODULES} ${${LIB_MODULE}_MODULES})

  module_path("${LIB_MODULE}.c" LIB_MODULE_OUT)
  set(COMPILE_MODULE_CNAME "${LIB_MODULE}")
  compile_module(lib/node/${NODE_MODULE}.js "${LIB_MODULE_OUT}" ${MODLIST})
  unset(COMPILE_MODULE_CNAME)

  set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED(${LIB_MODULE})\n")
endforeach(NODE_MODULE ${BUILTINS_NODE})]]

#dump_list(BUILTINS_COMPILED BUILTINS_NATIVE BUILTIN_MODULES)

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

  #dump(BUILTIN_MODULE)

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
      find_library(_bm_lib_${_bm_name} NAMES "${_bm_name}" PATHS "${_bm_path}" NO_DEFAULT_PATH)
      if(_bm_lib_${_bm_name})
        list(APPEND EXTERNAL_MODULES "${_bm_name}")
        # No FORCE: if <name>_MODULE was already set explicitly (e.g. to also list the
        # module's own transitive system-library dependencies, which this auto-detection
        # can't know about -- it only locates the module's own lib<name>.a/.so), that
        # explicit value wins over the auto-detected bare path.
        set(${_bm_name}_MODULE "${_bm_lib_${_bm_name}}"
            CACHE STRING "Static library for external module '${_bm_name}', found via BUILTIN_MODULES")
      else()
        message(WARNING "BUILTIN_MODULES entry '${BUILTIN_MODULE}' -- no lib${_bm_name}.a/.so found in that directory")
      endif()
    endif()
  elseif(EXISTS "${_bm_path}" AND _bm_path MATCHES "\\.js$")
    # a JS file: compiled into qjsm as builtin <basename>
    get_filename_component(_bm_name "${_bm_path}" NAME_WE)
    if(_bm_name MATCHES "[^A-Za-z0-9_]")
      message(WARNING "BUILTIN_MODULES entry '${BUILTIN_MODULE}' -- '${_bm_name}' is not a C identifier, skipping")
    elseif(NOT _bm_name IN_LIST BUILTINS_COMPILED)
      list(APPEND BUILTINS_COMPILED "${_bm_name}")
      set(MODLIST ${COMMON_MODULES} ${${_bm_name}_MODULES})
      module_path("${_bm_name}.c" BUILTIN_MODULE_OUT)
      # compile_module() prefixes the source dir, so hand it a relative path
      file(RELATIVE_PATH _bm_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${_bm_path}")
      compile_module("${_bm_rel}" "${BUILTIN_MODULE_OUT}" ${MODLIST})
      
     # dump(_bm_name)
      string(REGEX REPLACE "[/]" "_" BC_NAME "${_bm_name}")

      set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED(${BC_NAME})\n")
    endif()
  elseif(EXISTS "${_bm_path}" AND _bm_path MATCHES "\\.(a|so|dylib|lib)$")
    # a prebuilt module library: lib<name>.a / <name>.so -> external module <name>
    get_filename_component(_bm_name "${_bm_path}" NAME_WE)
    string(REGEX REPLACE "^(lib|quickjs-)" "" _bm_name "${_bm_name}")
    list(APPEND EXTERNAL_MODULES "${_bm_name}")
    set(${_bm_name}_MODULE "${_bm_path}"
        CACHE STRING "Static library for external module '${_bm_name}', given via BUILTIN_MODULES")
  elseif(BUILTIN_NATIVE_KEY IN_LIST NATIVE_MODULES)
    if(NOT BUILTIN_NATIVE_NAME IN_LIST BUILTINS_NATIVE)
      list(APPEND BUILTINS_NATIVE "${BUILTIN_NATIVE_NAME}")
    endif()
  elseif(BUILTIN_MODULE IN_LIST COMPILED_MODULE_NAMES)
    if(BUILTIN_MODULE MATCHES "[^A-Za-z0-9_/]")
      message(
        WARNING
          "BUILTIN_MODULES entry '${BUILTIN_MODULE}' is not a C identifier (a '-' or '/' in lib/${BUILTIN_MODULE}.js) -- the builtin table needs one, skipping"
      )
    elseif(NOT BUILTIN_MODULE IN_LIST BUILTINS_COMPILED)
      list(APPEND BUILTINS_COMPILED "${BUILTIN_MODULE}")
      set(MODLIST ${COMMON_MODULES} ${${BUILTIN_MODULE}_MODULES})
      module_path("${BUILTIN_MODULE}.c" BUILTIN_MODULE_OUT)
      compile_module(lib/${BUILTIN_MODULE}.js "${BUILTIN_MODULE_OUT}" ${MODLIST})

      string(REGEX REPLACE "[/]" "_" BC_NAME "${BUILTIN_MODULE}")

      set(COMPILED_BUILTIN_HEADER "${COMPILED_BUILTIN_HEADER}BUILTIN_COMPILED(${BC_NAME})\n")
    endif()
  else()
    message(
      WARNING
        "BUILTIN_MODULES entry '${BUILTIN_MODULE}' matches no lib/<name>.js, quickjs-<name>.c, .js/.a/.so file or existing directory -- skipping (native: ${NATIVE_MODULES}; compiled: ${COMPILED_MODULE_NAMES})"
    )
  endif()
endforeach()

#relative_paths(COMPILED_FILES "${CMAKE_CURRENT_BINARY_DIR}" ${COMPILED_MODULES})
#dump_list(COMPILED_MODULES)


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
set(EXTERNAL_MODULES "${EXTERNAL_MODULES}" CACHE STRING "Externally-built native modules to statically link into qjsm")

# Core modules that always get a qjs-<name>-static library (no MODULE_X toggle gates
# them) but aren't part of the default qjsm builtin set: still buildable/embeddable,
# just not registered/linked into qjsm unless required (see NATIVE_REQUIRED below) or
# added back to BUILTINS_NATIVE.
# if(NOT DEFINED BUILTINS_NATIVE_EXCLUDE)
#   # xml stays out of this list: lib/xml.js is a same-named JS polyfill that would
#   # silently shadow the native module instead of failing to load if the native one
#   # isn't registered as a builtin.
#   set(BUILTINS_NATIVE_EXCLUDE "arraybuffer-sink;child-process;lexer;location;repeater;tree-walker;virtual")
# endif(NOT DEFINED BUILTINS_NATIVE_EXCLUDE)

# set(BUILTINS_NATIVE_EXCLUDE "${BUILTINS_NATIVE_EXCLUDE}"
#     CACHE STRING "Native modules to build but not register as qjsm builtins by default (overridden by NATIVE_REQUIRED)")

# Enforce module dependencies: a native module required by any builtin (compiled JS or
# native) must itself be a builtin, however BUILTINS_NATIVE/BUILTINS_NATIVE_EXCLUDE are
# configured. Computed as the transitive closure over two existing dependency sources
# (no separate list to keep in sync):
#   - ${name}_MODULES  -- a compiled JS builtin's own import list (e.g. fs_MODULES
#     already lists syscallerror/directory/misc/textcode; assert_MODULES lists
#     deep/inspect), filtered down to entries that are themselves native modules.
#   - ${name}_LIBRARIES -- a native module's own C-level link dependencies on other
#     native modules (e.g. deep_LIBRARIES=qjs-pointer;qjs-predicate, because
#     quickjs-deep.c calls js_pointer_wrap()/js_predicate_class_id() directly, not
#     just via JS import).
set(NATIVE_REQUIRED ${BUILTINS_NATIVE})

foreach(LIB_MODULE ${BUILTINS_COMPILED})
  foreach(DEP ${${LIB_MODULE}_MODULES})
    string(REGEX REPLACE "[-_]" "-" DEP2 "${DEP}")
    string(REGEX REPLACE ".*:" "" DEP "${DEP2}")
    #if(NOT DEP STREQUAL DEP2)
    #endif()
    if(NOT DEP IN_LIST BUILTINS_COMPILED AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-${DEP}.c")
      list(APPEND NATIVE_REQUIRED "${DEP}")
    endif()
  endforeach()
endforeach()

set(_native_required_changed TRUE)
while(_native_required_changed)
  set(_native_required_changed FALSE)
  list(REMOVE_DUPLICATES NATIVE_REQUIRED)
  foreach(MOD ${NATIVE_REQUIRED})
    foreach(LIBDEP ${${MOD}_LIBRARIES})
      if(LIBDEP MATCHES "^qjs-(.+)$")
        if(NOT CMAKE_MATCH_1 IN_LIST NATIVE_REQUIRED)
          list(APPEND NATIVE_REQUIRED "${CMAKE_MATCH_1}")
          set(_native_required_changed TRUE)
        endif()
      endif()
    endforeach()
  endforeach()
endwhile()

set(NATIVE_BUILTIN_HEADER "")
set(QJSM_STATIC_LIBS "")


set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}/* native C builtins */\n")

# BUILTINS_NATIVE plus the native modules they link (module_depends), transitively
set(_native_all "")
foreach(NATIVE_BUILTIN ${BUILTINS_NATIVE})
  string(REPLACE "_" "-" NATIVE_BUILTIN "${NATIVE_BUILTIN}")
  list(APPEND _native_all "${NATIVE_BUILTIN}")
endforeach()
set(_native_changed TRUE)
while(_native_changed)
  set(_native_changed FALSE)
  foreach(_nm ${_native_all})
    string(REPLACE "-" "_" _nm_var "${_nm}")
    foreach(_nd ${${_nm_var}_LIBRARIES})
      if(_nd MATCHES "^qjs-(.+)$" AND NOT CMAKE_MATCH_1 IN_LIST _native_all
         AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/quickjs-${CMAKE_MATCH_1}.c")
        list(APPEND _native_all "${CMAKE_MATCH_1}")
        set(_native_changed TRUE)
      endif()
    endforeach()
  endforeach()
endwhile()

foreach(NATIVE_BUILTIN ${_native_all})
  string(REPLACE "_" "-" NATIVE_SOURCE "${NATIVE_BUILTIN}")
  string(REPLACE "-" "_" NATIVE_MODULE_VAR "${NATIVE_BUILTIN}")
  # system libraries of the module (not the qjs-* targets, whose sources are compiled in)
  foreach(_ld_lib ${${NATIVE_BUILTIN}_LIBRARIES} ${${NATIVE_MODULE_VAR}_LIBRARIES})
    if(NOT _ld_lib MATCHES "^qjs-")
      list(APPEND QJSM_LDADD "${_ld_lib}")
    endif()
  endforeach()
  list(APPEND QJSM_SOURCES quickjs-${NATIVE_SOURCE}.c)
  string(REGEX REPLACE "-" "_" NATIVE_MODULE "${NATIVE_BUILTIN}")
  set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}BUILTIN_NATIVE(${NATIVE_MODULE})\n")
endforeach()



#[[set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}\n/* static native C builtins */\n")

# Modules built as qjs-<name>-static libraries (BUILD_STATIC) that aren't
# already compiled directly into qjsm via BUILTINS_NATIVE above: link the library in
# and register it, unless excluded via BUILTINS_NATIVE_EXCLUDE -- except a module in
# NATIVE_REQUIRED always gets registered regardless of BUILTINS_NATIVE_EXCLUDE, since
# something already included depends on it.
foreach(STATIC_MODULE_NAME ${STATIC_MODULE_NAMES})
  list(FIND BUILTINS_NATIVE "${STATIC_MODULE_NAME}" _NATIVE_BUILTIN_INDEX)
  list(FIND BUILTINS_NATIVE_EXCLUDE "${STATIC_MODULE_NAME}" _NATIVE_EXCLUDED_INDEX)
  if(_NATIVE_BUILTIN_INDEX EQUAL -1 AND (_NATIVE_EXCLUDED_INDEX EQUAL -1 OR STATIC_MODULE_NAME IN_LIST NATIVE_REQUIRED))
    string(REGEX REPLACE "-" "_" STATIC_MODULE_SYMBOL "${STATIC_MODULE_NAME}")
    set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}BUILTIN_NATIVE(${STATIC_MODULE_SYMBOL})\n")
    list(APPEND QJSM_STATIC_LIBS "qjs-${STATIC_MODULE_NAME}-static")
  endif()
endforeach()]]

#[[set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}\n/* external native C builtins */")

foreach(EXTERNAL_MODULE ${EXTERNAL_MODULES})
  string(REGEX REPLACE "[-_]" "_" EXTERNAL_MODULE "${EXTERNAL_MODULE}")
  string(REGEX REPLACE ".*:" "" EXTERNAL_MODULE_SYMBOL "${EXTERNAL_MODULE}")

  set(NATIVE_BUILTIN_HEADER "${NATIVE_BUILTIN_HEADER}BUILTIN_NATIVE(${EXTERNAL_MODULE_SYMBOL})\n")

  if(DEFINED ${EXTERNAL_MODULE}_MODULE)
    list(APPEND QJSM_STATIC_LIBS "${${EXTERNAL_MODULE}_MODULE}")
  else()
    message(
      WARNING
        "EXTERNAL_MODULES contains '${EXTERNAL_MODULE}' but ${EXTERNAL_MODULE}_MODULE is not set -- qjsm will fail to link"
    )
  endif()
endforeach()]]

if(BUILTIN_HEADER)
  message(STATUS "Builtin modules:\n${BUILTIN_HEADER}")
endif()

set(BUILTIN_HEADER "#ifndef BUILTIN_NATIVE\n#define BUILTIN_NATIVE(x)\n#endif\n")
set(BUILTIN_HEADER "${BUILTIN_HEADER}\n#ifndef BUILTIN_COMPILED\n#define BUILTIN_COMPILED(x)\n#endif\n")

file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-builtins.h"
     "${BUILTIN_HEADER}\n${NATIVE_BUILTIN_HEADER}\n${COMPILED_BUILTIN_HEADER}")
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

if(LIBDL)
  link_libraries(${LIBDL})
endif(LIBDL)

link_directories(${QJSM_LIBDIR})

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${QJSM_LIBDIR}")
set_target_properties(qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT};-L${QJSM_LIBDIR}" COMPILE_FLAGS "-w")

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
endmacro(qjsm_add_executable)
