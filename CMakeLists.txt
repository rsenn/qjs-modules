project(qjs-modules C)

cmake_minimum_required(VERSION 3.2)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

find_quickjs()

set(QUICKJS_MODULES bjson child-process deep lexer misc mmap path pointer predicate repeater
                    inspect tree-walker xml)

if(EXISTS "${QUICKJS_H}")

  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()
set(child_process_SOURCES child-process.c child-process.h property-enumeration.c
                          property-enumeration.h utils.c utils.h vector.c vector.h)
set(deep_SOURCES
    vector.c vector.h pointer.c virtual-properties.c property-enumeration.c property-enumeration.h
    utils.c utils.h predicate.c predicate.h pointer.h virtual-properties.h)
set(deep_LIBRARIES qjs-pointer qjs-predicate)
set(inspect_SOURCES vector.c vector.h iteration.h utils.c utils.h property-enumeration.c
                    property-enumeration.h quickjs-internal.h)
set(tree_walker_SOURCES vector.c vector.h property-enumeration.c property-enumeration.h utils.c
                        utils.h)
set(xml_SOURCES vector.c vector.h property-enumeration.c property-enumeration.h utils.c utils.h)
set(path_SOURCES path.c path.h utils.c utils.h)
set(predicate_SOURCES predicate.c predicate.h vector.c vector.h utils.c utils.h)
set(pointer_SOURCES pointer.c pointer.h utils.c utils.h)
set(lexer_SOURCES quickjs-lexer.h utils.c utils.h vector.c vector.h lexer.c lexer.h)
set(lexer_LIBRARIES quickjs-predicate)
set(mmap_SOURCES utils.c utils.h)
set(repeater_SOURCES utils.c utils.h)
set(util_SOURCES utils.c utils.h quickjs-inspect.c)

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

message("QuickJS install directory: ${QUICKJS_PREFIX}")

set(CMAKE_REQUIRED_QUIET TRUE)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

# dump(VISIBILITY_HIDDEN) dump(MODULE_COMPILE_FLAGS)

check_flags(
  "-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-cast-function-type"
  CMAKE_C_FLAGS)
# message("CMAKE_C_FLAGS: ${CMAKE_C_FLAGS}")

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0" CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
  endif(O_OPT_NONE)
  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb" CACHE STRING "C compiler options"
                                                                   FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra" CACHE STRING "C compiler options"
                                                                     FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
  endif(G_WARN_EXTRA)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

include(CheckFunctionExists)

include(CheckCSourceRuns)

set(CMAKE_REQUIRED_QUIET OFF)
check_include_file(spawn.h HAVE_SPAWN_H)

if(HAVE_SPAWN_H)
  list(APPEND CMAKE_REQUIRED_INCLUDES spawn.h)

  check_function_exists(posix_spawnp HAVE_POSIX_SPAWNP)
  message(STATUS "HAVE_POSIX_SPAWNP: ${HAVE_POSIX_SPAWNP}")

  if(HAVE_POSIX_SPAWNP)
    add_definitions(-DPOSIX_SPAWN=1)
  endif(HAVE_POSIX_SPAWNP)

endif(HAVE_SPAWN_H)

file(GLOB TESTS tests/test_*.js)

relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(NAME "${BASE}"
             COMMAND env QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}
                     "${QJS}" --bignum "${TEST}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_target(
      "${BASE}" ALL
      COMMAND
        env QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} "${QJSC}"
        -fbignum -M std -M os -M ${NAME} -o "${BASE}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}"
      SOURCES "${TEST}")
  endforeach(TEST ${TESTS})
endif(DO_TESTS)

check_c_source_runs(
  "#include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b, c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n   return 0;\n}"
  HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
  add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)

addprefix(SHARED_TARGETS "qjs-" ${QUICKJS_MODULES})
addsuffix(STATIC_TARGETS "-static" ${SHARED_TARGETS})
# dump(STATIC_TARGETS)

foreach(JS_MODULE ${QUICKJS_MODULES})
  message(STATUS "Building module '${JS_MODULE}'")
  make_module(${JS_MODULE})
endforeach(JS_MODULE ${QUICKJS_MODULES})

target_link_libraries(qjs-deep qjs-predicate)
target_link_libraries(qjs-lexer qjs-predicate)
add_dependencies(qjs-lexer qjs-predicate)

file(GLOB TESTS_SOURCES tests/test_*.js)
list(FILTER TESTS_SOURCES EXCLUDE REGEX "test_lexer.js")
source_group(TESTS_GROUP FILES ${TESTS_SOURCES})

include(CTest)

foreach(TEST_SOURCE ${TESTS_SOURCES})
  file(RELATIVE_PATH TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SOURCE}")
  basename(TEST_NAME ${TEST_SOURCE} .js)
  add_test(NAME "${TEST_NAME}" COMMAND qjsm --bignum "${TEST_SOURCE}"
           WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

endforeach(TEST_SOURCE ${TESTS_SOURCES})

file(GLOB LIBJS lib/*.js)
list(FILTER LIBJS EXCLUDE REGEX "lib/require.js|lib/fs.js")

install(FILES ${LIBJS} DESTINATION "${QUICKJS_LIBRARY_DIR}")

compile_module(lib/console.js)
compile_module(lib/require.js)
compile_module(lib/repl.js)
compile_module(lib/fs.js)
compile_module(lib/util.js)
compile_module(lib/perf_hooks.js)
compile_module(lib/process.js)

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
  set(quickjs_sources_root ${CMAKE_CURRENT_SOURCE_DIR}/..)

  include(${quickjs_sources_root}/defs.cmake)
  include(${quickjs_sources_root}/cmake/check-libraries.cmake)

  add_custom_command(
    OUTPUT qjscalc.c
    COMMAND ${QJSC} -fbignum -c -o ./qjscalc.c -m ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js
    DEPENDS ${QJSC_DEPS}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Generate from qjscalc.js using qjs compiler" SOURCES
            ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js)

  set(QJSM_SOURCES
      qjsm.c
      ${CMAKE_CURRENT_BINARY_DIR}/repl.c
      ${CMAKE_CURRENT_BINARY_DIR}/qjscalc.c
      ${CMAKE_CURRENT_BINARY_DIR}/console.c
      ${CMAKE_CURRENT_BINARY_DIR}/require.c
      ${CMAKE_CURRENT_BINARY_DIR}/fs.c
      ${CMAKE_CURRENT_BINARY_DIR}/perf_hooks.c
      ${CMAKE_CURRENT_BINARY_DIR}/process.c
      ${CMAKE_CURRENT_BINARY_DIR}/util.c)

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)
    set(QJSM_SOURCES
        ${QJSM_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger-transport-${TRANSPORT_PLATFORM}.c)
  endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)

  dump(QJSM_SOURCES)
  add_executable(qjsm ${QJSM_SOURCES})

  # target_link_libraries(qjs ${QUICKJS_LIBRARY})
  target_link_libraries(qjsm quickjs ${STATIC_TARGETS} ${LIBPTHREAD} ${LIBM} ${LIBDL} ${LIBWS2_32})
  set_target_properties(qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT}" COMPILE_FLAGS "-w"
                                        RPATH "${CMAKE_INSTALL_PREFIX}/lib/quickjs")

  target_compile_definitions(qjsm PRIVATE CONFIG_VERSION="${quickjs_version}" _GNU_SOURCE=1
                                          CONFIG_PREFIX="${CMAKE_INSTALL_PREFIX}" CONFIG_BIGNUM=1)
  install(TARGETS qjsm DESTINATION bin)

endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
