project(qjs-modules C)

message("Building project [1;31m${PROJECT_NAME}[0m")

cmake_minimum_required(VERSION 3.2)

set(CMAKE_C_STANDARD 11)

option(BUILD_LIBARCHIVE "Build archive library" OFF)
#option(BUILD_PIGPIO "Build pigpio library" OFF)
option(DEBUG_OUTPUT "Debug output" OFF)
option(DEBUG_ALLOC "Debug allocation" OFF)
option(DO_TESTS "Perform tests" ON)
option(USE_SPAWN "Use POSIX spawn()" OFF)
option(USE_LIBARCHIVE "Use libarchive" ON)

if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
  option(BUILD_SHARED_MODULES "Build shared modules" OFF)
  set(BUILD_SHARED_LIBS FALSE)
else(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  option(BUILD_SHARED_MODULES "Build shared modules" ON)
endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")

if(WIN32 OR MINGW)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif(WIN32 OR MINGW)

if(ANDROID)
  add_definitions(-DANDROID)
endif(ANDROID)
message("System: ${CMAKE_SYSTEM_NAME}")

if(DEBUG_OUTPUT)
  add_definitions(-DDEBUG_OUTPUT)
endif(DEBUG_OUTPUT)

if(DEBUG_ALLOC)
  add_definitions(-DDEBUG_ALLOC)
  #add_definitions(-include lib/debug.h)
endif(DEBUG_ALLOC)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

message(STATUS "Finding QuickJS...")

find_quickjs()
configure_quickjs()

if(NOT QUICKJS_LIBRARY_DIR AND NOT QUICKJS_INCLUDE_DIRS)
  if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
      if(NOT QJS)
        set(QJS "${CMAKE_CURRENT_BINARY_DIR}/../qjs")
      endif(NOT QJS)
      if(NOT QJSC)
        set(QJSC "${CMAKE_CURRENT_BINARY_DIR}/../qjsc")
      endif(NOT QJSC)
      #set(QUICKJS_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/.." CACHE PATH "QuickJS library directory")
      #set(QUICKJS_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/..;${CMAKE_CURRENT_BINARY_DIR}/.." CACHE STRING "QuickJS include directories")
    endif()
  endif()
endif()

include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceRuns)

set(CMAKE_REQUIRED_QUIET OFF)

check_functions(vfork fork waitpid execvpe setsid )
if(HAVE_VFORK)
  add_definitions(-DHAVE_VFORK)
else(HAVE_VFORK)
  if(HAVE_FORK)
    add_definitions(-DHAVE_VFORK -Dvfork=fork)
  endif(HAVE_FORK)
endif(HAVE_VFORK)
if(HAVE_FORK)
  add_definitions(-DHAVE_FORK)
endif(HAVE_FORK)
if(HAVE_SETSID)
  add_definitions(-DHAVE_SETSID)
endif(HAVE_SETSID)

if(HAVE_EXECVPE)
  add_definitions(-DHAVE_EXECVPE=1)
else(HAVE_EXECVPE)
  check_functions(execve)
endif(HAVE_EXECVPE)

#[[if(ANDROID)
  option(USE_SPAWN "Use POSIX spawn()" OFF)
else()
  option(USE_SPAWN "Use POSIX spawn()" ON)
endif()
]]
check_include_file(sys/wait.h HAVE_SYS_WAIT_H)
if(HAVE_SYS_WAIT_H)
  add_definitions(-DHAVE_SYS_WAIT_H)
endif(HAVE_SYS_WAIT_H)

if(HAVE_WAITPID)
  add_definitions(-DHAVE_WAITPID=1)
endif(HAVE_WAITPID)

if(NOT HAVE_SPAWN)
  if(NOT HAVE_EXECVPE AND NOT HAVE_WAITPID)

    check_include_file(spawn.h HAVE_SPAWN_H)
    if(HAVE_SPAWN_H)
      list(APPEND CMAKE_REQUIRED_INCLUDES spawn.h)

      check_function_exists(posix_spawnp HAVE_POSIX_SPAWNP)
      message(STATUS "HAVE_POSIX_SPAWNP: ${HAVE_POSIX_SPAWNP}")

      if(HAVE_POSIX_SPAWNP)
        set(HAVE_SPAWN ON)
      endif(HAVE_POSIX_SPAWNP)
    endif(HAVE_SPAWN_H)
  endif(NOT HAVE_EXECVPE AND NOT HAVE_WAITPID)
endif(NOT HAVE_SPAWN)
#dump(USE_SPAWN)

if(HAVE_SPAWN AND USE_SPAWN)
  add_definitions(-DPOSIX_SPAWN=1)
endif(HAVE_SPAWN AND USE_SPAWN)

check_include_file(threads.h HAVE_THREADS_H)
if(HAVE_THREADS_H)
  add_definitions(-DHAVE_THREADS_H)
endif(HAVE_THREADS_H)

check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
if(HAVE_SYS_MMAN_H)
  add_definitions(-DHAVE_SYS_MMAN_H)
endif(HAVE_SYS_MMAN_H)

check_function_exists(mmap HAVE_MMAP)
if(HAVE_MMAP)
  add_definitions(-DHAVE_MMAP=1)
endif(HAVE_MMAP)

check_include_file(termios.h HAVE_TERMIOS_H)
if(HAVE_TERMIOS_H)
  add_definitions(-DHAVE_TERMIOS_H)
endif(HAVE_TERMIOS_H)

#check_function_exists(isatty HAVE_ISATTY)
#if(HAVE_ISATTY)
#  add_definitions(-DHAVE_ISATTY=1)
#endif(HAVE_ISATTY)

check_function_exists(realpath HAVE_REALPATH)
if(HAVE_REALPATH)
  add_definitions(-DHAVE_REALPATH=1)
endif(HAVE_REALPATH)

check_function_exists(link HAVE_LINK)
if(HAVE_LINK)
  add_definitions(-DHAVE_LINK=1)
endif(HAVE_LINK)

check_function_exists(lstat HAVE_LSTAT)
if(HAVE_LSTAT)
  add_definitions(-DHAVE_LSTAT=1)
endif(HAVE_LSTAT)

check_function_exists(strverscmp HAVE_STRVERSCMP)
if(HAVE_STRVERSCMP)
  add_definitions(-DHAVE_STRVERSCMP=1)
endif(HAVE_STRVERSCMP)

check_function_exists(memmem HAVE_MEMMEM)
if(HAVE_MEMMEM)
  add_definitions(-DHAVE_MEMMEM=1)
endif(HAVE_MEMMEM)

check_function_exists(fmemopen HAVE_FMEMOPEN)
if(HAVE_FMEMOPEN)
  add_definitions(-DHAVE_FMEMOPEN=1)
endif(HAVE_FMEMOPEN)

check_functions(fnmatch glob wordexp inotify_init1 inet_pton daemon)
check_includes(alloca.h fnmatch.h glob.h wordexp.h sys/inotify.h)

if(HAVE_INET_PTON)
  add_definitions(-DHAVE_INET_PTON=1)
endif(HAVE_INET_PTON)

if(HAVE_FNMATCH AND HAVE_FNMATCH_H)
  add_definitions(-DHAVE_FNMATCH=1)
endif(HAVE_FNMATCH AND HAVE_FNMATCH_H)

if(HAVE_DAEMON)
  add_definitions(-DHAVE_DAEMON=1)
endif(HAVE_DAEMON)

if(HAVE_GLOB AND HAVE_GLOB_H)
  add_definitions(-DHAVE_GLOB=1)
endif(HAVE_GLOB AND HAVE_GLOB_H)

if(HAVE_WORDEXP AND HAVE_WORDEXP_H)
  add_definitions(-DHAVE_WORDEXP=1)
endif(HAVE_WORDEXP AND HAVE_WORDEXP_H)

if(HAVE_INOTIFY_INIT1 AND HAVE_SYS_INOTIFY_H)
  add_definitions(-DHAVE_INOTIFY=1)
endif(HAVE_INOTIFY_INIT1 AND HAVE_SYS_INOTIFY_H)

check_functions(sysinfo)

if(HAVE_SYSINFO)
  check_includes(sys/sysinfo.h)

  if(HAVE_SYS_SYSINFO_H)
    add_definitions(-DHAVE_SYSINFO)
  else(HAVE_SYSINFO_H)
    set(HAVE_SYSINFO FALSE)
  endif(HAVE_SYS_SYSINFO_H)

endif(HAVE_SYSINFO)

message("Have sysinfo() ${HAVE_SYSINFO}")
# if(BUILD_PIGPIO)
#   include(ExternalProject)
#   ExternalProject_Add(
#     pigpio
#     SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/pigpio
#     BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/pigpio
#     CMAKE_CACHE_ARGS
#       "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_C_FLAGS:STRING=-w -fPIC"
#       "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}" "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
#       "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}" "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
#     CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE")

#   link_directories("${CMAKE_CURRENT_BINARY_DIR}/pigpio")

# else(BUILD_PIGPIO)
#   include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Findpilib/gpio.cmake)
# endif(BUILD_PIGPIO)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/include ${QUICKJS_INCLUDE_DIRS})
link_directories(${QUICKJS_LIBRARY_DIR})

set(QUICKJS_MODULES
    bjson blob deep directory lexer list location misc path pointer predicate
    repeater textcode sockets stream syscallerror inspect tree-walker xml)

if(USE_LIBARCHIVE)
  if(BUILD_LIBARCHIVE)
    include(ExternalProject)
    ExternalProject_Add(
      libarchive
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libarchive
      BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libarchive
      CMAKE_CACHE_ARGS
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
        "-DCMAKE_C_FLAGS:STRING=-w"
        "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
        "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE")

    link_directories("${CMAKE_CURRENT_BINARY_DIR}/libarchive")

  else(BUILD_LIBARCHIVE)
    include(FindLibArchive)
  endif(BUILD_LIBARCHIVE)

  #if(NOT LibArchive_FOUND)
  #  message(FATAL_ERROR "libarchive not found")
  #endif(NOT LibArchive_FOUND)

  if(LibArchive_FOUND)
    set(QUICKJS_MODULES archive ${QUICKJS_MODULES})
  endif(LibArchive_FOUND)
endif(USE_LIBARCHIVE)

if(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
else(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  if(WIN32 OR MINGW)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
  endif(WIN32 OR MINGW)
endif(HAVE_SYS_WAIT_H AND HAVE_WAITPID)

set(LIB_MODULES console events fs lexer/csv lexer/ecmascript parser perf_hooks
                process repl require stack tty util)

if(EXISTS "${QUICKJS_H}")

  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()
set(buffer_utils_SOURCES src/buffer-utils.c include/buffer-utils.h)
set(COMMON_SOURCES src/debug.c include/debug.h)
set(vector_SOURCES src/vector.c include/vector.h ${buffer_utils_SOURCES})
set(libutf_SOURCES libutf/include/libutf.h libutf/src/libutf.c)
set(char_utils_SOURCES src/char-utils.c include/char-utils.h ${libutf_SOURCES})
set(queue_SOURCES src/queue.c include/queue.h)
set(utils_SOURCES src/utils.c include/utils.h ${char_utils_SOURCES}
                  src/clock_gettime.c src/strverscmp.c)
set(js_utils_SOURCES src/js-utils.c include/js-utils.h)
set(utils_SOURCES ${utils_SOURCES} src/qsort_r.c)
set(stream_SOURCES quickjs-stream.c ${queue_SOURCES} ${js_utils_SOURCES}
                   ${utils_SOURCES} ${buffer_utils_SOURCES})
set(predicate_SOURCES src/predicate.c include/predicate.h ${utils_SOURCES})
if(NOT HAVE_STRVERSCMP)
  set(utils_SOURCES ${utils_SOURCES} src/strverscmp.c)
endif(NOT HAVE_STRVERSCMP)
if(NOT HAVE_MEMMEM)
  set(utils_SOURCES ${utils_SOURCES} src/memmem.c)
endif(NOT HAVE_MEMMEM)
set(token_SOURCES src/token.c include/token.h)
set(location_SOURCES
    src/location.c include/location.h quickjs-location.c quickjs-location.h
    ${utils_SOURCES} ${buffer_utils_SOURCES})

set(syscallerror_SOURCES quickjs-syscallerror.c quickjs-syscallerror.h)
set(blob_SOURCES quickjs-blob.c ${buffer_utils_SOURCES} ${utils_SOURCES})

set(child_process_SOURCES
    src/child-process.c include/child-process.h src/property-enumeration.c
    include/property-enumeration.h ${utils_SOURCES} ${vector_SOURCES})
set(deep_SOURCES
    ${vector_SOURCES} src/pointer.c src/virtual-properties.c
    src/property-enumeration.c include/property-enumeration.h ${utils_SOURCES}
    ${predicate_SOURCES} include/pointer.h include/virtual-properties.h)
set(deep_LIBRARIES qjs-predicate qjs-pointer)
#[[set(deep_LIBRARIES)
unset(deep_LIBRARIES)]]
set(inspect_SOURCES
    ${vector_SOURCES} include/iteration.h ${utils_SOURCES}
    src/property-enumeration.c include/property-enumeration.h
    quickjs-internal.h)
set(tree_walker_SOURCES ${vector_SOURCES} src/property-enumeration.c
                        include/property-enumeration.h ${utils_SOURCES})
set(xml_SOURCES
    ${vector_SOURCES} src/property-enumeration.c include/property-enumeration.h
    ${utils_SOURCES} #${location_SOURCES} ${inspect_SOURCES}
    src/virtual-properties.c src/location.c)
set(xml_LIBRARIES qjs-location qjs-inspect)
set(getdents_SOURCES src/getdents.c include/getdents.h)
set(path_SOURCES src/path.c include/path.h ${utils_SOURCES}
                 ${buffer_utils_SOURCES} ${getdents_SOURCES})
if(WIN32 OR MINGW)
  set(path_SOURCES ${path_SOURCES} src/readlink.c)
endif(WIN32 OR MINGW)

set(predicate_SOURCES src/predicate.c include/predicate.h ${vector_SOURCES}
                      ${utils_SOURCES})
set(pointer_SOURCES src/pointer.c include/pointer.h ${utils_SOURCES}
                    ${buffer_utils_SOURCES})
set(lexer_SOURCES
    quickjs-lexer.h ${location_SOURCES} ${token_SOURCES} ${utils_SOURCES}
    ${vector_SOURCES} src/lexer.c include/lexer.h)
set(lexer_LIBRARIES qjs-location)
set(mmap_SOURCES ${utils_SOURCES} ${buffer_utils_SOURCES})
set(repeater_SOURCES ${queue_SOURCES} ${utils_SOURCES} ${buffer_utils_SOURCES})
set(sockets_SOURCES ${queue_SOURCES} ${utils_SOURCES} ${buffer_utils_SOURCES})
if(NOT WIN32)
  if(NOT HAVE_INET_PTON)
    set(sockets_SOURCES ${sockets_SOURCES} src/inet_pton.c)
  endif(NOT HAVE_INET_PTON)
  if(NOT HAVE_INET_NTOP)
    set(sockets_SOURCES ${sockets_SOURCES} src/inet_ntop.c)
  endif(NOT HAVE_INET_NTOP)
endif(NOT WIN32)
if(WIN32 OR MINGW)
  set(sockets_LIBRARIES mswsock ws2_32)
  set(sockets_SOURCES ${sockets_SOURCES} src/socketpair_win32.c)
endif(WIN32 OR MINGW)

file(GLOB tutf8e_SOURCES tutf8e/include/*.h tutf8e/include/tutf8e/*.h
     tutf8e/src/*.c)
file(GLOB libutf_SOURCES libutf/src/*.c libutf/include/*.h)
set(textcode_SOURCES
    quickjs-textcode.c quickjs-textcode.h src/ringbuffer.c include/ringbuffer.h
    ${libutf_SOURCES} ${tutf8e_SOURCES} ${buffer_utils_SOURCES}
    ${utils_SOURCES} ${vector_SOURCES})

set(misc_SOURCES src/base64.c include/base64.h quickjs-internal.c
                 quickjs-internal.h ${path_SOURCES} ${vector_SOURCES})

set(directory_SOURCES
    quickjs-directory.c ${getdents_SOURCES} ${char_utils_SOURCES}
    ${utils_SOURCES} ${buffer_utils_SOURCES})

if(HAVE_SYS_MMAN_H)
  set(gpio_SOURCES src/gpio.c include/gpio.h)
endif(HAVE_SYS_MMAN_H)

set(syscallerror_SOURCES ${utils_SOURCES} ${buffer_utils_SOURCES})
set(list_SOURCES quickjs-list.c quickjs-list.h ${utils_SOURCES} ${buffer_utils_SOURCES})

# if(pigpio_LIBRARY)
#   set(gpio_LIBRARIES ${pigpio_LIBRARY})
# endif(pigpio_LIBRARY)

include_directories(
  ${LibArchive_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/libutf/include
  ${CMAKE_CURRENT_SOURCE_DIR}/tutf8e/include)
set(archive_SOURCES ${utils_SOURCES} ${buffer_utils_SOURCES})
set(archive_LIBRARIES ${LibArchive_LIBRARIES})

set(QJSM_EXTRA_SOURCES ${vector_SOURCES} ${buffer_utils_SOURCES})

if(HAVE_SYS_MMAN_H AND HAVE_MMAP)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} gpio mmap)
else(HAVE_SYS_MMAN_H AND HAVE_MMAP)
  if(WIN32 OR MINGW)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
    set(mmap_SOURCES ${mmap_SOURCES} src/mmap-win32.c)
  endif(WIN32 OR MINGW)
endif(HAVE_SYS_MMAN_H AND HAVE_MMAP)

if(CACHE{CMAKE_BUILD_TYPE})
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel
                                               RelWithDebInfo)
endif(CACHE{CMAKE_BUILD_TYPE})

set(CMAKE_REQUIRED_QUIET TRUE)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

# dump(VISIBILITY_HIDDEN) dump(MODULE_COMPILE_FLAGS)

check_flags(
  "-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-function"
  CMAKE_C_FLAGS)

#check_flags("-Wno-cast-function-type" CMAKE_C_FLAGS)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0"
          CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
  endif(O_OPT_NONE)
  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb"
          CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra"
          CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
  endif(G_WARN_EXTRA)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

file(GLOB TESTS tests/test_*.js) relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(
      NAME "${BASE}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      COMMAND
        env
        QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR}
        "${QJSM}" --bignum "${TEST}")
    #[[ add_custom_target(
      "${BASE}" ALL
      COMMAND
        env QUICKJS_MODULE_PATH=${CMAKE_CURRENT_SOURCE_DIR}:${CMAKE_CURRENT_BINARY_DIR} "${QJSC}"
        -fbignum -M std -M os -M ${NAME} -o "${BASE}" "${CMAKE_CURRENT_SOURCE_DIR}/${TEST}"
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
      SOURCES "${TEST}")]]
  endforeach(TEST ${TESTS})
endif(DO_TESTS)

check_c_source_runs(
  " # include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b,
    # c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n return
    # 0;\n}"
  HAVE__BUILTIN_MUL_OVERFLOW)
if(HAVE__BUILTIN_MUL_OVERFLOW)
  add_definitions(-DHAVE__BUILTIN_MUL_OVERFLOW)
endif(HAVE__BUILTIN_MUL_OVERFLOW)

addprefix(SHARED_TARGETS "qjs-" ${QUICKJS_MODULES})
addsuffix(STATIC_TARGETS "-static" ${SHARED_TARGETS})

# dump(STATIC_TARGETS)
unset(QJSM_LDADD)

set(QJSM_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT WASI)
  if(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(CMAKE_IMPORT_LIBRARY_PREFIX "")
    set(CMAKE_IMPORT_LIBRARY_SUFFIX ".bc")
  endif(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
endif(NOT WASI)

foreach(JS_MODULE ${QUICKJS_MODULES})
  make_module(${JS_MODULE})
  string(REGEX REPLACE "-" "_" QJSM_MODULE "${JS_MODULE}")
  list(APPEND QJSM_LIBS "${QJSM_MODULE}")
  #list(APPEND QJSM_LDADD "${QJSM_LIBDIR}/lib${QJSM_MODULE}${CMAKE_STATIC_LIBRARY_SUFFIX}")
  list(APPEND QJSM_LDADD
       "${QJSM_LIBDIR}/quickjs-${QJSM_MODULE}${LIBRARY_SUFFIX}")
endforeach(JS_MODULE ${QUICKJS_MODULES})

link_directories(${QJSM_LIBDIR})

#dump(QJSM_LDADD)

if(LIBM)
  set(predicate_LIBRARIES ${LIBM})
endif(LIBM)

set(sockets_LIBRARIES qjs-syscallerror)
set(deep_LIBRARIES qjs-pointer qjs-predicate ${LIBM})
set(lexer_LIBRARIES qjs-predicate)
set(lexer_DEPS qjs-predicate)

file(GLOB TESTS_SOURCES tests/test_*.js)
list(REMOVE_ITEM TESTS_SOURCES "test_lexer.js")
source_group(TESTS_GROUP FILES ${TESTS_SOURCES})

set(QJSM "${CMAKE_CURRENT_BINARY_DIR}/qjsm"
    CACHE FILEPATH "qjsm (QuickJS modular shell) interpreter")

include(CTest)

foreach(TEST_SOURCE ${TESTS_SOURCES})
  file(RELATIVE_PATH TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SOURCE}")
  basename(TEST_NAME ${TEST_SOURCE} .js)
  add_test(NAME "${TEST_NAME}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
           COMMAND ${QJSM} --bignum "${TEST_SOURCE}")

endforeach(TEST_SOURCE ${TESTS_SOURCES})

file(GLOB LIBJS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.js)
file(GLOB LIBLEXER ${CMAKE_CURRENT_SOURCE_DIR}/lib/lexer/*.js)
file(GLOB LIBXML ${CMAKE_CURRENT_SOURCE_DIR}/lib/xml/*.js)
list(REMOVE_ITEM LIBJS lib/require.js)
list(REMOVE_ITEM LIBJS lib/fs.js)

list(
  FILTER
  LIBJS
  EXCLUDE
  REGEX
  "/(archive|bjson|blob|child_process|deep|gpio|inspect|lexer|location|misc|mmap|path|pointer|predicate|repeater|sockets|stream|syscallerror|textcode|tree_walker|xml)\.js$"
)

install(FILES ${LIBJS} DESTINATION "${QUICKJS_JS_MODULE_DIR}")
install(FILES ${LIBLEXER} DESTINATION "${QUICKJS_JS_MODULE_DIR}/lexer")
install(FILES ${LIBXML} DESTINATION "${QUICKJS_JS_MODULE_DIR}/xml")

string(REGEX REPLACE "[^A-Za-z0-9_]" ";" BUILTIN_LIBS "${LIB_MODULES}")

# target_compile_definitions(qjsm -DBUILTIN_MODULES=\\\"${BUILTIN_LIBS}\\\") message("${BUILTIN_LIBS}"

list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")

foreach(LIB_MODULE ${LIB_MODULES})
  #message("Compile Module: ${LIB_MODULE}")
  compile_module(lib/${LIB_MODULE}.js)
endforeach(LIB_MODULE ${LIB_MODULES})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
  set(QUICKJS_SOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

  include(${QUICKJS_SOURCES_ROOT}/defs.cmake)
  include(${QUICKJS_SOURCES_ROOT}/cmake/check-libraries.cmake)

  unset(QUICKJS_INCLUDE_DIR CACHE)
  set(QUICKJS_INCLUDE_DIR "${QUICKJS_SOURCES_ROOT}")

  #dump(QUICKJS_INCLUDE_DIR)

  include_directories(${QUICKJS_INCLUDE_DIR})
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)

add_custom_command(
  OUTPUT ${MODULES_DIR}/qjscalc.c
  COMMAND ${QJSC} -fbignum -c -o ${MODULES_DIR}/qjscalc.c -m
          ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js
  DEPENDS ${QJSC_DEPS}
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Generate from qjscalc.js using qjs compiler" SOURCES
          ${CMAKE_CURRENT_SOURCE_DIR}/../qjscalc.js)

#dump(COMPILED_MODULES)

set(QJSM_SOURCES
    src/qjsm.c
    src/path.c
    src/readlink.c
    ${MODULES_DIR}/repl.c
    ${MODULES_DIR}/qjscalc.c
    ${MODULES_DIR}/console.c
    ${MODULES_DIR}/events.c
    ${MODULES_DIR}/require.c
    ${MODULES_DIR}/fs.c
    ${MODULES_DIR}/perf_hooks.c
    ${MODULES_DIR}/process.c
    ${MODULES_DIR}/tty.c
    ${MODULES_DIR}/util.c
    ${QJSM_EXTRA_SOURCES})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)
  if(QUICKJS_DEBUGGER)
    set(QJSM_SOURCES
        ${QJSM_SOURCES}
        ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c
        ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger-transport-${TRANSPORT_PLATFORM}.c
    )
  endif(QUICKJS_DEBUGGER)
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../quickjs-debugger.c)

if(NOT BUILD_STATIC_MODULES)
  set(QJSM_SOURCES
      ${QJSM_SOURCES}
      ${archive_SOURCES}
      ${blob_SOURCES}
      ${buffer_utils_SOURCES}
      ${char_utils_SOURCES}
      ${child_process_SOURCES}
      ${deep_SOURCES}
      ${directory_SOURCES}
      ${gpio_SOURCES}
      ${inspect_SOURCES}
      ${js_utils_SOURCES}
      ${lexer_SOURCES}
      ${list_SOURCES}
      ${libutf_SOURCES}
      ${location_SOURCES}
      ${list_SOURCES}
      ${misc_SOURCES}
      ${mmap_SOURCES}
      ${path_SOURCES}
      ${pointer_SOURCES}
      ${predicate_SOURCES}
      ${queue_SOURCES}
      ${repeater_SOURCES}
      # ${sockets_SOURCES}
      ${stream_SOURCES}
      ${syscallerror_SOURCES}
      ${textcode_SOURCES}
      ${token_SOURCES}
      ${tree_walker_SOURCES}
      ${utils_SOURCES}
      ${vector_SOURCES}
      ${xml_SOURCES})
endif(NOT BUILD_STATIC_MODULES)

##dump(QJSM_SOURCES)
add_executable(qjsm ${QJSM_SOURCES})

#dump(QUICKJS_LIBRARY)
if(BUILD_STATIC_MODULES)
  if(QJS_MODULES_STATIC)
    dump(QJS_MODULES_STATIC)
    add_dependencies(qjsm ${QJS_MODULES_STATIC})
  endif(QJS_MODULES_STATIC)
endif(BUILD_STATIC_MODULES)
if(COMPILED_MODULES)
  add_dependencies(qjsm ${COMPILED_MODULES})
endif(COMPILED_MODULES)

if(LIBDL)
  link_libraries(${LIBDL})
endif(LIBDL)

# target_link_libraries(qjsm quickjs ${QJSM_LDADD} ${LIBPTHREAD} ${LIBM} ${LIBDL} ${LIBWS2_32})

#target_link_directories(qjsm PUBLIC ${QJSM_LIBDIR} ${CMAKE_CURRENT_BINARY_DIR})
link_directories(${QJSM_LIBDIR})
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${QJSM_LIBDIR}")
set_target_properties(
  qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT};-L${QJSM_LIBDIR}" COMPILE_FLAGS
                                                                 "-w")
if(NOT WASI AND NOT EMSCRIPTEN)
  set_target_properties(qjsm PROPERTIES RPATH
                                        "${QUICKJS_INSTALL_PREFIX}/lib/quickjs")
endif(NOT WASI AND NOT EMSCRIPTEN)
target_link_directories(qjsm PUBLIC ${QUICKJS_LIBRARY_DIR})

target_link_libraries(qjsm PUBLIC ${QJSM_LDADD} ${QUICKJS_LIBRARY}
                                  ${LIBPTHREAD} ${LIBM} ${LIBWS2_32})

target_compile_definitions(
  qjsm PRIVATE _GNU_SOURCE=1 CONFIG_VERSION="${QUICKJS_VERSION}"
               CONFIG_PREFIX="${QUICKJS_INSTALL_PREFIX}" CONFIG_BIGNUM=1)
install(TARGETS qjsm DESTINATION bin)

file(GLOB INSTALL_SCRIPTS [!.]*.js)

install(FILES ${INSTALL_SCRIPTS} DESTINATION bin
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE
                    GROUP_READ WORLD_EXECUTE WORLD_READ)

symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/list-imports.js")
symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/check-imports.js")
symlink(extract-structs.js "${CMAKE_INSTALL_PREFIX}/bin/extract-bindings.js")
