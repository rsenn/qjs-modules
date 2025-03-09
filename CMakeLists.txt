project(qjs-modules C)

#message("Building project [1;31m${PROJECT_NAME}[0m")

cmake_minimum_required(VERSION 3.2)

set(CMAKE_C_STANDARD 11)

#option(BUILD_PIGPIO "Build pigpio library" OFF)
option(DEBUG_OUTPUT "Debug output" OFF)
option(DEBUG_ALLOC "Debug allocation" OFF)
option(DO_TESTS "Perform tests" ON)
option(USE_SPAWN "Use POSIX spawn()" OFF)
option(MODULE_ARCHIVE "Build 'archive' module" ON)
option(MODULE_MAGIC "Build 'magic' module" ON)
option(MODULE_MYSQL "Build 'mysql' module" ON)
option(MODULE_PGSQL "Build 'pgsql' module" ON)
option(MODULE_SERIAL "Build 'serial' module" ON)

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
#message("System: ${CMAKE_SYSTEM_NAME}")

if(DEBUG_OUTPUT)
  add_definitions(-DDEBUG_OUTPUT)
endif(DEBUG_OUTPUT)

if(DEBUG_ALLOC)
  add_definitions(-DDEBUG_ALLOC)
endif(DEBUG_ALLOC)

include(FindPkgConfig)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

#message(STATUS "Finding QuickJS...")

find_quickjs()
configure_quickjs()

list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
#list(APPEND CMAKE_REQUIRED_LIBRARIES "${QUICKJS_LIBRARY}")
list(APPEND CMAKE_REQUIRED_LIBRARIES quickjs)

include(CheckLibraryExists)

check_function_def(HAVE_JS_PROMISE_STATE JS_PromiseState)
check_library_exists(quickjs JS_PromiseState "${QUICKJS_LIBRARY_DIR}"
                     HAVE_JS_PROMISE_STATE_2)
check_library_exists(quickjs JS_GetModuleLoaderFunc "${QUICKJS_LIBRARY_DIR}"
                     HAVE_JS_GETMODULELOADERFUNC)
if(HAVE_JS_GETMODULELOADERFUNC)
  add_definitions(-DHAVE_JS_GETMODULELOADERFUNC)
endif(HAVE_JS_GETMODULELOADERFUNC)

check_library_exists(quickjs JS_GetModuleLoaderOpaque "${QUICKJS_LIBRARY_DIR}"
                     HAVE_JS_GETMODULELOADEROPAQUE)
if(HAVE_JS_GETMODULELOADEROPAQUE)
  add_definitions(-DHAVE_JS_GETMODULELOADEROPAQUE)
endif(HAVE_JS_GETMODULELOADEROPAQUE)

check_library_exists(quickjs js_debugger_build_backtrace
                     "${QUICKJS_LIBRARY_DIR}" HAVE_JS_DEBUGGERBUILDBACKTRACE)
if(HAVE_JS_DEBUGGERBUILDBACKTRACE)
  add_definitions(-DHAVE_JS_DEBUGGER_BUILD_BACKTRACE)
endif(HAVE_JS_DEBUGGERBUILDBACKTRACE)

if(NOT QUICKJS_LIBRARY_DIR AND NOT QUICKJS_INCLUDE_DIRS)
  if(EXISTS "${CMAKE_CURRENT_BINARY_DIR}/../quickjs-config.h")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.h")
      if(NOT QJS)
        set(QJS "${CMAKE_CURRENT_BINARY_DIR}/../qjs")
      endif(NOT QJS)
      if(NOT QJSC)
        set(QJSC "${CMAKE_CURRENT_BINARY_DIR}/../qjsc")
      endif(NOT QJSC)
    endif()
  endif()
endif()

include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceRuns)

set(CMAKE_REQUIRED_QUIET OFF)

check_functions_def(vfork fork waitpid execvpe setsid)

if(NOT HAVE_VFORK)
  if(HAVE_FORK)
    add_definitions(-DHAVE_VFORK -Dvfork=fork)
  endif(HAVE_FORK)
endif(NOT HAVE_VFORK)

check_functions_def(execve execvpe)
check_include_def(sys/wait.h)

check_function_and_include(posix_spawnp spawn.h HAVE_SPAWN)

if(HAVE_SPAWN AND USE_SPAWN)
  add_definitions(-DPOSIX_SPAWN=1)
endif(HAVE_SPAWN AND USE_SPAWN)

check_include_def(threads.h)
check_include_def(sys/mman.h)

check_function_def(mmap)

check_include_def(termios.h)
check_function_def(ioctl)
check_function_def(realpath)
check_function_def(link)
check_function_def(lstat)
check_function_def(fstat)
check_function_def(strverscmp)
check_function_def(memmem)
check_function_def(fmemopen)
check_functions(fcntl wordexp inotify_init1)
check_includes(fcntl.h alloca.h wordexp.h sys/inotify.h)

check_functions_def(access fnmatch daemon)

#message("Have daemon() ${HAVE_DAEMON}")

check_include_file(fnmatch.h HAVE_FNMATCH_H_2)
#check_include_def(fnmatch.h)

check_function_and_include(fcntl fcntl.h)
check_function_and_include(fnmatch fnmatch.h)

check_function_and_include(glob glob.h)
check_function_and_include(wordexp wordexp.h)

check_function_and_include(inotify_init1 sys/inotify.h HAVE_INOTIFY)

if(HAVE_INOTIFY)
  add_definitions(-DHAVE_INOTIFY)
endif(HAVE_INOTIFY)

#message("Have inotify_init1 ${HAVE_INOTIFY_INIT1}")
#message("Have sys/inotify.h ${HAVE_SYS_INOTIFY_H}")
#message("Enable inotify ${HAVE_INOTIFY}")

check_function_and_include(sysinfo sys/sysinfo.h)

#message("Have sysinfo() ${HAVE_SYSINFO}")

check_function_def(gettid)
check_function_def(uname)
check_function_def(tempnam)

if(HAVE_TEMPNAM)
  add_definitions(-DUSE_TEMPNAM=1)
endif(HAVE_TEMPNAM)

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

include_directories(
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include
  ${CMAKE_CURRENT_BINARY_DIR} ${QUICKJS_INCLUDE_DIRS})
link_directories(${QUICKJS_LIBRARY_DIR})

set(QUICKJS_MODULES
    bjson blob deep directory lexer list location misc path pointer predicate
    queue repeater textcode sockets stream syscallerror inspect tree-walker xml)

if(MODULE_MAGIC)
  list(APPEND QUICKJS_MODULES magic)
endif(MODULE_MAGIC)

if(MODULE_SERIAL)
  option(BUILD_LIBSERIALPORT "Build serialport library" OFF)

  list(APPEND QUICKJS_MODULES serial)
endif(MODULE_SERIAL)

if(MODULE_ARCHIVE)
  option(BUILD_LIBARCHIVE "Build archive library" OFF)

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
endif(MODULE_ARCHIVE)

if(MODULE_MAGIC)
  include(CheckLibraryExists)
  check_library_exists(magic magic_open "${LIBMAGIC_LIBRARY_DIR}" HAVE_LIBMAGIC)
  set(old_REQUIRED_INCLUDES "${CMAKE_REQUIRED_INCLUDES}")
  if(LIBMAGIC_INCLUDE_DIR)
    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBMAGIC_INCLUDE_DIR}")
  endif(LIBMAGIC_INCLUDE_DIR)
  check_include_file(magic.h HAVE_MAGIC_H)
  set(CMAKE_REQUIRED_INCLUDES "${old_REQUIRED_INCLUDES}")

  if(HAVE_LIBMAGIC AND HAVE_MAGIC_H)
    add_definitions(-DHAVE_LIBMAGIC=1)
    set(LIBMAGIC_LIBRARY magic CACHE STRING "libmagic library")
    set(magic_LIBRARIES ${LIBMAGIC_LIBRARY})
    set(magic_MODULE magic)
  else(HAVE_LIBMAGIC AND HAVE_MAGIC_H)
    set(magic_MODULE)
  endif(HAVE_LIBMAGIC AND HAVE_MAGIC_H)

  if(EXISTS /usr/share/file/magic)
    set(LIBMAGIC_DB /usr/share/file/magic)
  endif(EXISTS /usr/share/file/magic)

endif(MODULE_MAGIC)

if(MODULE_MAGIC)
  set(magic_MODULE magic)
endif(MODULE_MAGIC)

if(MODULE_MYSQL)
  pkg_check_modules(MARIADB libmariadb mariadb)

  if(pkgcfg_lib_MARIADB_mariadb)
    set(LIBMARIADB_LIBRARY "${pkgcfg_lib_MARIADB_mariadb}"
        CACHE STRING "MariaDB client library")
    unset(pkgcfg_lib_MARIADB_mariadb CACHE)
  else(pkgcfg_lib_MARIADB_mariadb)
    check_library_exists(mariadb mysql_init "${LIBMARIADB_LIBRARY_DIR}"
                         HAVE_LIBMARIADB)

    if(HAVE_LIBMARIADB)
      set(LIBMARIADB_LIBRARY mariadb CACHE STRING "MariaDB client library")
    endif(HAVE_LIBMARIADB)
  endif(pkgcfg_lib_MARIADB_mariadb)

  if(LIBMARIADB_LIBRARY)
    list(APPEND QUICKJS_MODULES mysql)
    set(mysql_LIBRARIES ${LIBMARIADB_LIBRARY})

    set(LIBMARIADB_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include"
        CACHE PATH "MariaDB client include directory")

    if(NOT EXISTS "${LIBMARIADB_INCLUDE_DIR}/mysql.h")
      if(EXISTS "${LIBMARIADB_INCLUDE_DIR}/mariadb"
         AND EXISTS "${LIBMARIADB_INCLUDE_DIR}/mariadb/mysql.h")
        set(LIBMARIADB_INCLUDE_DIR "${LIBMARIADB_INCLUDE_DIR}/mariadb")
      endif()
    endif()

    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBMARIADB_INCLUDE_DIR}")
    check_include_def(mysql.h)

    if(HAVE_MYSQL_H OR EXISTS "${LIBMARIADB_INCLUDE_DIR}")
      include_directories(${LIBMARIADB_INCLUDE_DIR})
    endif()
  endif(LIBMARIADB_LIBRARY)
endif(MODULE_MYSQL)

if(MODULE_PGSQL)
  pkg_check_modules(LIBPQ libpq)

  if(pkgcfg_lib_LIBPQ_pq)
    set(LIBPQ_LIBRARY "${pkgcfg_lib_LIBPQ_pq}"
        CACHE STRING "PostgreSQL client library")
    unset(pkgcfg_lib_LIBPQ_pq CACHE)
  else(pkgcfg_lib_LIBPQ_pq)
    check_library_exists(pq PQconnectdb "${LIBPQ_LIBRARY_DIR}" HAVE_LIBPQCLIENT)

    if(HAVE_LIBPQCLIENT)
      set(LIBPQ_LIBRARY pq CACHE STRING "PostgreSQL client library")
    endif(HAVE_LIBPQCLIENT)
  endif(pkgcfg_lib_LIBPQ_pq)

  if(LIBPQ_LIBRARY)
    list(APPEND QUICKJS_MODULES pgsql)
    set(pgsql_LIBRARIES ${LIBPQ_LIBRARY})

    if("${LIBPQ_LIBRARY}" MATCHES "[/\\]")
      string(REGEX REPLACE "/lib.*" "/include" LIBPQ_INCLUDE_DIR
                           "${LIBPQ_LIBRARY}")
    else("${LIBPQ_LIBRARY}" MATCHES "[/\\]")
      set(LIBPQ_INCLUDE_DIR "${CMAKE_INSTALL_PREFIX}/include")
    endif("${LIBPQ_LIBRARY}" MATCHES "[/\\]")

    if(NOT EXISTS "${LIBPQ_INCLUDE_DIR}/libpq-fe.h")
      if(EXISTS "${LIBPQ_INCLUDE_DIR}/postgresql"
         AND EXISTS "${LIBPQ_INCLUDE_DIR}/postgresql/libpq-fe.h")
        set(LIBPQ_INCLUDE_DIR "${LIBPQ_INCLUDE_DIR}/postgresql")
      endif()
    endif()

    set(LIBPQ_INCLUDE_DIR "${LIBPQ_INCLUDE_DIR}"
        CACHE PATH "PostgresSQL client include directory")

    list(APPEND CMAKE_REQUIRED_INCLUDES "${LIBPQ_INCLUDE_DIR}")
    check_include_def(libpq-fe.h)

    if(HAVE_LIBPQ_FE_H OR EXISTS "${LIBPQ_INCLUDE_DIR}")
      include_directories(${LIBPQ_INCLUDE_DIR})
    endif()
    unset(pkgcfg_lib_LIBPQ_pq CACHE)
  endif(LIBPQ_LIBRARY)
endif(MODULE_PGSQL)

if(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
else(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  if(WIN32 OR MINGW)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
  endif(WIN32 OR MINGW)
endif(HAVE_SYS_WAIT_H AND HAVE_WAITPID)

if(EXISTS "${QUICKJS_H}")
  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()

check_function_def(strverscmp)

if(NOT HAVE_STRVERSCMP)
  set(utils_SOURCES ${utils_SOURCES} src/strverscmp.c)
endif(NOT HAVE_STRVERSCMP)

check_function_def(memmem)

if(NOT HAVE_MEMMEM)
  set(utils_SOURCES ${utils_SOURCES} src/memmem.c)
endif(NOT HAVE_MEMMEM)

set(deep_LIBRARIES qjs-predicate qjs-pointer)
#[[set(deep_LIBRARIES)
unset(deep_LIBRARIES)]]
set(xml_LIBRARIES qjs-location qjs-inspect)

if(WIN32 OR MINGW)
  set(path_SOURCES ${path_SOURCES} src/readlink.c)
endif(WIN32 OR MINGW)

set(lexer_LIBRARIES qjs-location)

if(WIN32 OR MINGW OR "${CMAKE_SYSTEM_NAME}" STREQUAL Windows)
  set(sockets_LIBRARIES mswsock ws2_32)
  set(sockets_SOURCES ${sockets_SOURCES} src/socketpair_win32.c)
  set(HAVE_INET_PTON TRUE)

  check_include_def(afunix.h)
else()
  check_function_exists(inet_pton HAVE_INET_PTON)
endif()

#set(CMAKE_REQUIRED_LIBRARIES ${sockets_LIBRARIES})
#unset(CMAKE_REQUIRED_LIBRARIES)

#include(CheckSymbolExists)
#check_library_exists(ws2_32 inet_pton "" HAVE_INET_PTON)

#if(NOT HAVE_INET_PTON)
#  check_symbol_exists(<inet_pton "winsock2.h" HAVE_INET_PTON)
#endif(NOT HAVE_INET_PTON)

if(HAVE_INET_PTON)
  #message(STATUS "Have inet_pton()")
  add_definitions(-DHAVE_INET_PTON)
else(HAVE_INET_PTON)
  #message(STATUS "Don't have inet_pton()")
endif(HAVE_INET_PTON)

if(NOT HAVE_INET_PTON)
  set(sockets_SOURCES ${sockets_SOURCES} src/inet_pton.c)
endif(NOT HAVE_INET_PTON)
if(NOT HAVE_INET_NTOP)
  set(sockets_SOURCES ${sockets_SOURCES} src/inet_ntop.c)
endif(NOT HAVE_INET_NTOP)

list(APPEND sockets_LIBRARIES qjs-syscallerror)
list(APPEND misc_LIBRARIES qjs-syscallerror)

file(GLOB tutf8e_SOURCES tutf8e/include/*.h tutf8e/include/tutf8e/*.h
     tutf8e/src/*.c)
file(GLOB libutf_SOURCES libutf/src/*.c libutf/include/*.h)
file(GLOB libbcrypt_SOURCES libbcrypt/*.c libbcrypt/*/*.c libbcrypt/*/*.S)

if(HAVE_SYS_MMAN_H)
  set(gpio_SOURCES src/gpio.c include/gpio.h)
endif(HAVE_SYS_MMAN_H)

# if(pigpio_LIBRARY)
#   set(gpio_LIBRARIES ${pigpio_LIBRARY})
# endif(pigpio_LIBRARY)

include_directories(
  ${LibArchive_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/libutf/include
  ${CMAKE_CURRENT_SOURCE_DIR}/tutf8e/include)
set(archive_LIBRARIES ${LibArchive_LIBRARIES})

if(BUILD_LIBSERIALPORT)
  include(ExternalProject)
  ExternalProject_Add(
    libserialport
    SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/libserialport
    BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libserialport
    CMAKE_CACHE_ARGS
      "-DCMAKE_INSTALL_LIBDIR:PATH=${CMAKE_INSTALL_LIBDIR}"
      "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
      "-DCMAKE_C_FLAGS:STRING=-w -fPIC"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
    CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE"
    INSTALL_COMMAND cmake -E echo "Skipping install step.")

  link_directories("${CMAKE_CURRENT_BINARY_DIR}/libserialport")
  set(LIBSERIALPORT_LIBRARY serialport)
  set(LIBSERIALPORT_LIBRARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libserialport)
  if(WIN32)
    list(APPEND LIBSERIALPORT_LIBRARY setupapi)
  endif(WIN32)
endif(BUILD_LIBSERIALPORT)

set(serial_DEPS libserialport)
set(serial_LIBRARIES ${LIBSERIALPORT_LIBRARY})

if(HAVE_MMAP)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
else(HAVE_MMAP)
  if(WIN32 OR MINGW OR MSYS)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
    set(mmap_SOURCES ${mmap_SOURCES} src/mmap-win32.c)
  endif()
endif(HAVE_MMAP)

if(CACHE{CMAKE_BUILD_TYPE})
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel
                                               RelWithDebInfo)
endif(CACHE{CMAKE_BUILD_TYPE})

set(CMAKE_REQUIRED_QUIET TRUE)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

check_flags(
  "-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-function"
  CMAKE_C_FLAGS)

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

list(APPEND CMAKE_REQUIRED_LIBRARIES ${QUICKJS_LIBRARY})
list(APPEND CMAKE_REQUIRED_INCLUDES ${QUICKJS_INCLUDE_DIR})

check_function_def(js_debugger_build_backtrace)

file(GLOB TESTS tests/test_*.js)
relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

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
        QUICKJS_MODULE_PATH="${CMAKE_BINARY_DIR};${CMAKE_CURRENT_BINARY_DIR}"
        "${QJSM}" --bignum "${TEST}")
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

unset(QJSM_LDADD)

set(QJSM_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT WASI)
  if(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(CMAKE_IMPORT_LIBRARY_PREFIX "")
    set(CMAKE_IMPORT_LIBRARY_SUFFIX ".bc")
  endif(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
endif(NOT WASI)

file(GLOB LIBRARY_SOURCES include/*.h src/*.c)
list(REMOVE_ITEM LIBRARY_SOURCES src/qjsm.c)
if(HAVE_INET_PTON)
  list(REMOVE_ITEM LIBRARY_SOURCES src/inet_pton.c)
endif(HAVE_INET_PTON)
if(HAVE_INET_NTOP)
  list(REMOVE_ITEM LIBRARY_SOURCES src/inet_ntop.c)
endif(HAVE_INET_NTOP)
if(NOT mmap_SOURCES)
  list(REMOVE_ITEM LIBRARY_SOURCES src/mmap-win32.c)
endif(NOT mmap_SOURCES)

link_directories(${QJSM_LIBDIR} ${LIBSERIALPORT_LIBRARY_DIR})

if(LIBM)
  set(predicate_LIBRARIES ${LIBM})
endif(LIBM)

set(deep_LIBRARIES qjs-pointer qjs-predicate ${LIBM})
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
list(REMOVE_ITEM LIBJS lib/std.js)
list(REMOVE_ITEM LIBJS lib/os.js)

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

set(LIB_MODULES
    console db events fs io
    #lexer/csv lexer/ecmascript
    parser perf_hooks process repl require stack tty util)

string(REGEX REPLACE "[^A-Za-z0-9_]" ";" BUILTIN_LIBS "${LIB_MODULES}")

list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")

set(COMMON_MODULES inspect util)
set(console_MODULES)
set(db_MODULES list)
set(fs_MODULES events path mmap misc)
set(io_MODULES misc)
set(parser_MODULES path list)
set(process_MODULES path misc)
set(repl_MODULES path io process)
set(require_MODULES path)
set(perf_hooks_MODULES misc)
set(stack_MODULES location)
set(util_MODULES path misc)

foreach(LIB_MODULE ${LIB_MODULES})
  #message("Compile Module: ${LIB_MODULE}")

  set(MODLIST ${COMMON_MODULES} ${${LIB_MODULE}_MODULES})

  compile_module(lib/${LIB_MODULE}.js
                 "${CMAKE_BINARY_DIR}/modules/${LIB_MODULE}.c" ${MODLIST})
endforeach(LIB_MODULE ${LIB_MODULES})

if("${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
    set(QUICKJS_SOURCES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
  endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
endif("${QUICKJS_SOURCES_ROOT}" STREQUAL "")

set(QUICKJS_STRUCT_SOURCES ${QUICKJS_SOURCES_ROOT}/quickjs.c
                           ${QUICKJS_SOURCES_ROOT}/quickjs-libc.c)

#if(EXISTS ${QUICKJS_SOURCES_ROOT}/quickjs-debugger.h)
#  set(QUICKJS_STRUCT_SOURCES ${QUICKJS_SOURCES_ROOT}/quickjs-debugger.h ${QUICKJS_STRUCT_SOURCES})
#endif(EXISTS ${QUICKJS_SOURCES_ROOT}/quickjs-debugger.h)

execute_process(
  COMMAND
    sed -n
    "/^typedef.*;$/ { /sig_t/! p }; /^enum [^ ]\\+ {/ { :lp0; N; /\\n}/! b lp0; /JS_CLASS/ { s,enum,enum JSClassIds, }; s,^,\\n,; p }; /^enum {/ { :lp1; N; /\\n}/! b lp1; /JS_CLASS/ { s,enum,enum JSClassIds, }; s,^,\\n,; p }; /^struct .\\+ {/ { :lp2; N; /\\n}/! b lp2; s,^,\\n,; p }; /^typedef enum .*{/ { :lp3; N; /\\n}/! b lp3; s,^,\\n,; p }; /^typedef struct {/ { :lp4; N; /\\n}/! b lp4; s,^,\\n,; p }; /^typedef struct .\\+ {/ { :lp5; N; /\\n}/! b lp5; s,^,\\n,; p }"
    ${QUICKJS_STRUCT_SOURCES}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  OUTPUT_FILE "${CMAKE_CURRENT_BINARY_DIR}/internal.h"
  #OUTPUT_VARIABLE INTERNAL
)

file(READ "${CMAKE_CURRENT_BINARY_DIR}/internal.h" INTERNAL)

file(
  WRITE "${CMAKE_CURRENT_BINARY_DIR}/quickjs-internal.h"
  "#ifndef QJS_MODULES_INTERNAL_H\n#define QJS_MODULES_INTERNAL_H\n\n#include <list.h>\n#include <cutils.h>\n#include <quickjs.h>\n#include <stddef.h>\n#include <sys/types.h>\n#include <quickjs-config.h>\n\n#ifdef CONFIG_BIGNUM\n#include <libbf.h>\n#endif\n#ifdef CONFIG_DEBUGGER\n#include <quickjs-debugger.h>\n#endif\n#ifdef USE_WORKER\n#include <pthread.h>\n#endif\n#ifndef SHORT_OPCODES\n#define SHORT_OPCODES 0\n#endif\n${INTERNAL}\nextern const JSOpCode js_opcodes[OP_COUNT + (OP_TEMP_END - OP_TEMP_START)];\n\n#endif /* defined(QJS_MODULES_INTERNAL_H) */\n"
)

add_library(
  modules STATIC ${LIBRARY_SOURCES} quickjs-internal.c ${tutf8e_SOURCES}
                 ${libutf_SOURCES} ${libbcrypt_SOURCES})
set_target_properties(modules PROPERTIES COMPILE_FLAGS -fPIC)
add_library(compiled STATIC ${COMPILED_MODULES})
set(COMMON_LIBRARIES modules compiled)

foreach(JS_MODULE ${QUICKJS_MODULES})
  make_module(${JS_MODULE})
  string(REGEX REPLACE "-" "_" QJSM_MODULE "${JS_MODULE}")
  list(APPEND QJSM_LIBS "${QJSM_MODULE}")
  #list(APPEND QJSM_LDADD "${QJSM_LIBDIR}/quickjs-${QJSM_MODULE}${LIBRARY_SUFFIX}")
endforeach(JS_MODULE ${QUICKJS_MODULES})

if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
  set(QUICKJS_SOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

  include(${QUICKJS_SOURCES_ROOT}/defs.cmake)
  include(${QUICKJS_SOURCES_ROOT}/cmake/check-libraries.cmake)

  unset(QUICKJS_INCLUDE_DIR CACHE)
  set(QUICKJS_INCLUDE_DIR "${QUICKJS_SOURCES_ROOT}")

  include_directories(${QUICKJS_INCLUDE_DIR})
endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)

set(QJSM_SOURCES src/qjsm.c)
#set(QJSM_LDADD qjs-syscallerror-static)

set(NATIVE_BUILTINS child-process deep inspect lexer location misc path pointer
                    predicate repeater tree-walker xml)
list(APPEND NATIVE_BUILTINS syscallerror)

foreach(NATIVE_BUILTIN ${NATIVE_BUILTINS})

  list(APPEND QJSM_SOURCES quickjs-${NATIVE_BUILTIN}.c)
endforeach()

add_executable(qjsm ${QJSM_SOURCES})

if(COMPILED_TARGETS)
  add_dependencies(qjsm ${COMPILED_TARGETS})
endif(COMPILED_TARGETS)

if(LIBDL)
  link_libraries(${LIBDL})
endif(LIBDL)

link_directories(${QJSM_LIBDIR})

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -L${QJSM_LIBDIR}")
set_target_properties(
  qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT};-L${QJSM_LIBDIR}" COMPILE_FLAGS
                                                                 "-w")

if(NOT WASI AND NOT EMSCRIPTEN)
  set_target_properties(
    qjsm
    PROPERTIES RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}"
               INSTALL_RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}")
endif(NOT WASI AND NOT EMSCRIPTEN)

target_link_directories(qjsm PUBLIC ${CMAKE_CURRENT_BINARY_DIR}
                        ${QUICKJS_LIBRARY_DIR})

target_link_libraries(
  qjsm PUBLIC ${QJSM_LDADD} ${COMMON_LIBRARIES} ${QUICKJS_LIBRARY}
              ${LIBPTHREAD} ${LIBM} ${LIBWS2_32})

target_compile_definitions(
  qjsm
  PRIVATE _GNU_SOURCE=1 CONFIG_VERSION="${QUICKJS_VERSION}"
          QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}"
          LIBMAGIC_DB="${LIBMAGIC_DB}" CONFIG_BIGNUM=1)
install(TARGETS qjsm DESTINATION bin)

file(GLOB INSTALL_SCRIPTS [!.]*.js)

install(FILES ${INSTALL_SCRIPTS} DESTINATION bin
        PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE
                    GROUP_READ WORLD_EXECUTE WORLD_READ)

symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/list-imports.js")
symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/check-imports.js")
symlink(extract-structs.js "${CMAKE_INSTALL_PREFIX}/bin/extract-bindings.js")
