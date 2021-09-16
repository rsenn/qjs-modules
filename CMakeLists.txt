project(modules C)

cmake_minimum_required(VERSION 3.2)

set(CMAKE_C_STANDARD 11)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

option(BUILD_LIBARCHIVE "Build archive library" OFF)
option(BUILD_PIGPIO "Build pigpio library" OFF)
option(DEBUG_OUTPUT "Debug output" OFF)

if(DEBUG_OUTPUT)
  add_definitions(-DDEBUG_OUTPUT)
endif(DEBUG_OUTPUT)

find_quickjs()

if(BUILD_PIGPIO)
  include(ExternalProject)
  ExternalProject_Add(
    pigpio SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pigpio BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/pigpio
    CMAKE_CACHE_ARGS "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_C_FLAGS:STRING=-w -fPIC" "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}" "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
                     "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}" "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}" CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE")

  link_directories("${CMAKE_CURRENT_BINARY_DIR}/pigpio")

else(BUILD_PIGPIO)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Findpigpio.cmake)
endif(BUILD_PIGPIO)

if(BUILD_LIBARCHIVE)
  include(ExternalProject)
  ExternalProject_Add(
    pigpio SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libarchive BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libarchive
    CMAKE_CACHE_ARGS "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}" "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}" "-DCMAKE_C_FLAGS:STRING=-w"
                     "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}" "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}" CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE")

  link_directories("${CMAKE_CURRENT_BINARY_DIR}/pigpio")

else(BUILD_LIBARCHIVE)
  include(FindLibArchive)
endif(BUILD_LIBARCHIVE)

link_directories(${CMAKE_CURRENT_BINARY_DIR})

if(NOT LibArchive_FOUND)
  message(ERROR "libarchive not found")
endif(NOT LibArchive_FOUND)

set(QUICKJS_MODULES archive bjson blob child-process deep gpio lexer location misc mmap path pointer predicate repeater stringdecoder sockets inspect syscallerror tree-walker xml)

if(EXISTS "${QUICKJS_H}")

  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()
set(predicate_SOURCES predicate.c predicate.h utils.c utils.h)
set(util_SOURCES utils.c utils.h char-utils.c char-utils.h buffer-utils.c buffer-utils.h)
set(syscallerror_SOURCES quickjs-syscallerror.c quickjs-syscallerror.h)
set(child_process_SOURCES child-process.c child-process.h property-enumeration.c property-enumeration.h ${util_SOURCES} vector.c vector.h qsort_r.c)
set(deep_SOURCES vector.c vector.h pointer.c virtual-properties.c property-enumeration.c property-enumeration.h ${util_SOURCES} ${predicate_SOURCES} pointer.h virtual-properties.h qsort_r.c)
# set(deep_LIBRARIES qjs-pointer qjs-predicate)
set(inspect_SOURCES vector.c vector.h iteration.h ${util_SOURCES} property-enumeration.c property-enumeration.h quickjs-internal.h qsort_r.c)
set(tree_walker_SOURCES vector.c vector.h property-enumeration.c property-enumeration.h ${util_SOURCES})
set(xml_SOURCES vector.c vector.h property-enumeration.c property-enumeration.h ${util_SOURCES} qsort_r.c)
set(path_SOURCES path.c path.h ${util_SOURCES})
set(predicate_SOURCES predicate.c predicate.h vector.c vector.h ${util_SOURCES})
set(pointer_SOURCES pointer.c pointer.h ${util_SOURCES})
set(lexer_SOURCES quickjs-lexer.h location.c location.h ${util_SOURCES} vector.c vector.h lexer.c lexer.h)
# set(lexer_LIBRARIES quickjs-predicate)
set(mmap_SOURCES ${util_SOURCES})
set(repeater_SOURCES ${util_SOURCES})
set(sockets_SOURCES ${util_SOURCES})
#set(sockets_LIBRARIES qjs-syscallerror)
set(location_SOURCES location.c location.h quickjs-location.c quickjs-location.h utils.c utils.h)
set(blob_SOURCES vector.c vector.h ${util_SOURCES})
set(stringdecoder_SOURCES quickjs-stringdecoder.c quickjs-stringdecoder.h ringbuffer.c ringbuffer.h ${util_SOURCES})

set(misc_SOURCES base64.c base64.h quickjs-internal.c quickjs-internal.h ${location_SOURCES} ${syscallerror_SOURCES})
set(misc_LIBRARIES ${CMAKE_CURRENT_BINARY_DIR}/syscallerror.so)
set(misc_LIBRARIES -L${CMAKE_CURRENT_BINARY_DIR} -lsyscallerror)

set(gpio_SOURCES gpio.c gpio.h)
set(syscallerror_SOURCES ${util_SOURCES} char-utils.c char-utils.h)
if(pigpio_LIBRARY)
  set(gpio_LIBRARIES ${pigpio_LIBRARY})
endif(pigpio_LIBRARY)

include_directories(${LibArchive_INCLUDE_DIRS})
set(archive_LIBRARIES ${LibArchive_LIBRARIES})

set(QJSM_EXTRA_SOURCES ${location_SOURCES})

set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)

message(
  "QuickJS
  install
  directory:
  ${QUICKJS_PREFIX}")

set(CMAKE_REQUIRED_QUIET TRUE)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

# dump(VISIBILITY_HIDDEN) dump(MODULE_COMPILE_FLAGS)

check_flags("-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-but-set-variable;-Wno-unused-function;-Wno-cast-function-type" CMAKE_C_FLAGS)

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
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb" CACHE STRING "C compiler options" FORCE)
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra" CACHE STRING "C compiler options" FORCE)
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
check_include_file(threads.h HAVE_THREADS_H)
if(HAVE_THREADS_H)
  add_definitions(-DHAVE_THREADS_H)
endif(HAVE_THREADS_H)

file(GLOB TESTS tests/test_*.js)

relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(NAME "${BASE}" COMMAND env QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} "${QJS}" --bignum "${TEST}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    add_custom_target("${BASE}" ALL COMMAND env QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} "${QJSC}" -fbignum -M std -M os -M ${NAME} -o "${BASE}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST}"
                      WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}" SOURCES "${TEST}")
  endforeach(TEST ${TESTS})
endif(DO_TESTS)

check_c_source_runs(
  " # include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b,
    # c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n return
    # 0;\n}" HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
  add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)

addprefix(SHARED_TARGETS "qjs-" ${QUICKJS_MODULES})
addsuffix(STATIC_TARGETS "-static" ${SHARED_TARGETS})

# dump(STATIC_TARGETS)
unset(QJSM_LDADD)

set(QJSM_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}")

foreach(JS_MODULE ${QUICKJS_MODULES})
  make_module(${JS_MODULE})
  string(REGEX REPLACE "-" "_" QJSM_MODULE "${JS_MODULE}")
  list(APPEND QJSM_LDADD "${QJSM_MODULE}.a")
endforeach(JS_MODULE ${QUICKJS_MODULES})

##dump(QJSM_LDADD QJSM_LIBDIR)

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
  add_test(NAME "${TEST_NAME}" COMMAND qjsm --bignum "${TEST_SOURCE}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")

endforeach(TEST_SOURCE ${TESTS_SOURCES})

file(GLOB LIBJS "${CMAKE_CURRENT_SOURCE_DIR}/lib/*.js")
list(FILTER LIBJS EXCLUDE REGEX "lib/require.js|lib/fs.js")

install(FILES ${LIBJS} DESTINATION "${QUICKJS_LIBRARY_DIR}/quickjs")

set(LIB_MODULES console events fs perf_hooks process repl require tty util)

string(REGEX REPLACE "[^A-Za-z0-9_]" ";" BUILTIN_LIBS "${LIB_MODULES}")

# target_compile_definitions(qjsm -DBUILTIN_MODULES=\\\"${BUILTIN_LIBS}\\\") message("${BUILTIN_LIBS}"

list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")

foreach(LIB_MODULE ${LIB_MODULES})
  # message("Compile Module: ${LIB_MODULE}")
  compile_module(lib/${LIB_MODULE}.js)
endforeach(LIB_MODULE ${LIB_MODULES})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
  set(QUICKJS_SOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

  include(${QUICKJS_SOURCES_ROOT}/defs.cmake)
  include(${QUICKJS_SOURCES_ROOT}/cmake/check-libraries.cmake)

  add_custom_command(OUTPUT ${MODULES_DIR}/qjscalc.c COMMAND ${QJSC} -fbignum -c -o ${MODULES_DIR}/qjscalc.c -m ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js DEPENDS ${QJSC_DEPS} WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
                     COMMENT "Generate from qjscalc.js using qjs compiler" SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js)

  set(QJSM_SOURCES qjsm.c vector.c ${MODULES_DIR}/repl.c ${MODULES_DIR}/qjscalc.c ${MODULES_DIR}/console.c ${MODULES_DIR}/events.c ${MODULES_DIR}/require.c ${MODULES_DIR}/fs.c ${MODULES_DIR}/perf_hooks.c ${MODULES_DIR}/process.c
                   ${MODULES_DIR}/tty.c ${MODULES_DIR}/util.c ${QJSM_EXTRA_SOURCES})

  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)
    if(QUICKJS_DEBUGGER)
      set(QJSM_SOURCES ${QJSM_SOURCES} ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger-transport-${TRANSPORT_PLATFORM}.c)
    endif(QUICKJS_DEBUGGER)
  endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)

  ##dump(QJSM_SOURCES)
  add_executable(qjsm ${QJSM_SOURCES})

  # dump(QJS_MODULES_STATIC)
  if(QJS_MODULES_STATIC)
    add_dependencies(qjsm ${QJS_MODULES_STATIC})
  endif(QJS_MODULES_STATIC)

  # target_link_libraries(qjsm quickjs ${QJSM_LDADD} ${LIBPTHREAD} ${LIBM} ${LIBDL} ${LIBWS2_32})

  # target_link_libraries(qjs ${QUICKJS_LIBRARY})
  target_link_directories(qjsm PUBLIC ${QJSM_LIBDIR} ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(qjsm quickjs ${QJSM_LDADD} ${LIBPTHREAD} ${LIBM} ${LIBDL} ${LIBWS2_32})
  set_target_properties(qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT}" COMPILE_FLAGS "-w" RPATH "${QUICKJS_INSTALL_PREFIX}/lib/quickjs")

  target_compile_definitions(qjsm PRIVATE CONFIG_VERSION="${QUICKJS_VERSION}" _GNU_SOURCE=1 CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}" CONFIG_BIGNUM=1)
  install(TARGETS qjsm DESTINATION bin)

endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
