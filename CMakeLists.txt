project(qjs-modules C ASM)

#message("Building project [1;31m${PROJECT_NAME}[0m")

cmake_minimum_required(VERSION 3.5)

set(CMAKE_C_STANDARD 11)

if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten" OR DIET)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten" OR DIET)

option(BUILD_SHARED_MODULES "Build shared modules" ${BUILD_SHARED_LIBS})

#option(BUILD_PIGPIO "Build pigpio library" OFF)
option(DEBUG_OUTPUT "Debug output" OFF)
option(DEBUG_ALLOC "Debug allocation" OFF)
option(DO_TESTS "Perform tests" ON)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/functions.cmake)

#dump(CMAKE_CROSSCOMPILING)

if(NOT CMAKE_CROSSCOMPILING)
  run_code(
    "tryposixspawn.c"
    "#include <spawn.h>\nint\nmain(void) {\n  pid_t pid;\n  posix_spawn_file_actions_t actions;\n  posix_spawnattr_t attr;\n  char* const argv[2] = {\"/bin/true\", 0};\n  char* const envp[1] = {0};\n  posix_spawnattr_setflags(&attr, 0);\n  posix_spawn_file_actions_init(&actions);\n  return posix_spawn(&pid, argv[0], 0, 0, argv, envp);\n}\n"
    RUN_RESULT
    OUTPUT
    ""
    "")
  if("${RUN_RESULT}" STREQUAL "0")
    set(HAVE_POSIX_SPAWN TRUE)
  endif()
  #dump(HAVE_POSIX_SPAWN)
endif(NOT CMAKE_CROSSCOMPILING)

if(HAVE_POSIX_SPAWN)
  option(USE_SPAWN "Use POSIX spawn()" ON)
else(HAVE_POSIX_SPAWN)
  option(USE_SPAWN "Use POSIX spawn()" OFF)
endif(HAVE_POSIX_SPAWN)

option(MODULE_ARCHIVE "Build 'archive' module" ON)
option(MODULE_MAGIC "Build 'magic' module" ON)
option(MODULE_BCRYPT "Build 'bcrypt' module" ON)
option(MODULE_MYSQL "Build 'mysql' module" ON)
option(MODULE_PGSQL "Build 'pgsql' module" ON)
option(MODULE_SERIAL "Build 'serial' module" OFF)
#option(MODULE_WASM "Build 'wasm' module" ON)
option(OPENBSD_GLOB "OpenBSD glob()" ON)

set(LIB_MODULES
    console db events fs io
    #lexer/csv lexer/ecmascript
    parser perf_hooks process repl require stack tty util)

include(FindPkgConfig)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/check-flags.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindQuickJS.cmake)

if(WASI OR EMSCRIPTEN)
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif(WASI OR EMSCRIPTEN)

#if(NOT BUILD_SHARED_LIBS)
#  option(BUILD_SHARED_MODULES "Build shared modules" OFF)
#else(NOT BUILD_SHARED_LIBS)
#  option(BUILD_SHARED_MODULES "Build shared modules" ON)
#endif(NOT BUILD_SHARED_LIBS)

#dump(BUILD_SHARED_LIBS BUILD_SHARED_MODULES)

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

#message(STATUS "Finding QuickJS...")

find_quickjs()
configure_quickjs()

#dump(QUICKJS_LIBRARY_DIR)
list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
#list(APPEND CMAKE_REQUIRED_LIBRARIES "${QUICKJS_LIBRARY}")
#list(APPEND CMAKE_REQUIRED_LIBRARIES quickjs)
#dump(CMAKE_REQUIRED_LINK_DIRECTORIES)

list(APPEND CMAKE_REQUIRED_LIBRARIES ${QUICKJS_LIBRARY})
list(APPEND CMAKE_REQUIRED_INCLUDES ${QUICKJS_INCLUDE_DIR})

include(CheckLibraryExists)

#check_function_def(JS_PROMISE_STATE JS_PromiseState)

check_library_exists(m atan2 "" HAVE_LIBMATH)
if(HAVE_LIBMATH)
  link_libraries(m)
endif(HAVE_LIBMATH)

check_library_exists(quickjs js_std_file "${QUICKJS_LIBRARY_DIR}" HAVE_JS_STD_FILE)

var2define(HAVE_JS_STD_FILE)

check_library_exists(quickjs JS_PromiseState "${QUICKJS_LIBRARY_DIR}" HAVE_JS_PROMISE_STATE)
if(HAVE_JS_PROMISE_STATE)
  add_definitions(-DHAVE_JS_PROMISE_STATE)
endif(HAVE_JS_PROMISE_STATE)

check_library_exists(quickjs JS_PromiseResult "${QUICKJS_LIBRARY_DIR}" HAVE_JS_PROMISE_RESULT)
if(HAVE_JS_PROMISE_RESULT)
  add_definitions(-DHAVE_JS_PROMISE_RESULT)
endif(HAVE_JS_PROMISE_RESULT)

#dump(QUICKJS_LIBRARY_DIR)

set(CMAKE_REQUIRED_LINK_DIRECTORIES_OLD "${CMAKE_REQUIRED_LINK_DIRECTORIES}")
set(CMAKE_REQUIRED_LIBRARIES_OLD "${CMAKE_REQUIRED_LIBRARIES}")
list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
list(APPEND CMAKE_REQUIRED_LIBRARIES quickjs)

check_function_def(JS_GetModuleLoaderFunc)
check_function_def(JS_GetModuleLoaderOpaque)
check_function_def(js_debugger_build_backtrace HAVE_JS_DEBUGGERBUILDBACKTRACE)

#dump(HAVE_JS_GETMODULELOADERFUNC HAVE_JS_GETMODULELOADEROPAQUE HAVE_JS_DEBUGGERBUILDBACKTRACE)

set(CMAKE_REQUIRED_LIBRARIES "${CMAKE_REQUIRED_LIBRARIES_OLD}")
set(CMAKE_REQUIRED_LINK_DIRECTORIES "${CMAKE_REQUIRED_LINK_DIRECTORIES_OLD}")

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
check_include_def(sys/syscall.h)

if(USE_SPAWN)
  check_function_and_include(posix_spawnp spawn.h HAVE_SPAWN)
  if(HAVE_SPAWN)
    add_definitions(-DPOSIX_SPAWN=1)
  endif(HAVE_SPAWN)
endif(USE_SPAWN)

check_includes(linux/ipx.h linux/x25.h linux/can.h linux/nfc.h linux/ax25.h linux/netlink.h linux/atalk.h
               linux/phonet.h linux/if_alg.h)

if(HAVE_LINUX_IPX_H)
  add_definitions(-DHAVE_LINUX_NETWORK_HEADERS)
endif(HAVE_LINUX_IPX_H)

check_include_def(threads.h)
check_include_def(sys/mman.h)

check_function_def(mmap)

check_includes_def(sys/param.h sys/stat.h errno.h unistd.h dirent.h)

check_functions_def(issetugid)

if(NOT ANDROID)
  check_include_def(pwd.h)
  check_functions_def(getpwuid getpwnam)
endif(NOT ANDROID)

check_external(getdents64 "" "" HAVE_GETDENTS64)
#check_function_exists(getdents64 HAVE_GETDENTS64)
if(NOT HAVE_GETDENTS64)
  check_external(getdents "" "" HAVE_GETDENTS)
  #check_function_exists(getdents HAVE_GETDENTS)
  if(HAVE_GETDENTS)
    add_definitions(-DHAVE_GETDENTS)
  endif(HAVE_GETDENTS)
else(NOT HAVE_GETDENTS64)
  add_definitions(-DHAVE_GETDENTS64)
endif(NOT HAVE_GETDENTS64)

#dump(HAVE_GETDENTS64 HAVE_GETDENTS)

check_include_def(termios.h)
check_function_def(ioctl)
check_function_def(realpath)
check_functions_def(link linkat symlink symlinkat)
check_function_def(strverscmp)
check_function_def(memmem)
check_function_def(fmemopen)
check_functions(fcntl wordexp inotify_init1)
check_includes(fcntl.h alloca.h wordexp.h sys/inotify.h)

check_functions_def(lstat fstat)
check_function_def(access)
check_functions_def(fnmatch daemon)

#dump(HAVE_ACCESS HAVE_FSTAT)

check_functions_def(chmod fchmod)
check_functions_def(chown fchown lchown)
check_functions_def(fsync fdatasync)
check_functions_def(truncate ftruncate)
check_functions_def(utime utimes futimes lutimes)
check_include_def(sys/time.h)

#message("Have daemon() ${HAVE_DAEMON}")

check_include_file(fnmatch.h HAVE_FNMATCH_H_2)
#check_include_def(fnmatch.h)

check_function_and_include(fcntl fcntl.h)
check_function_and_include(fnmatch fnmatch.h)

if(NOT OPENBSD_GLOB)
  check_function_and_include(glob glob.h)
endif(NOT OPENBSD_GLOB)

#dump(HAVE_GLOB)

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
  option(USE_TEMPNAM ON "Use tempnam()")
endif(HAVE_TEMPNAM)

if(USE_TEMPNAM)
  add_definitions(-DUSE_TEMPNAM=1)
endif(USE_TEMPNAM)

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

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}
                    ${QUICKJS_INCLUDE_DIRS})
link_directories(${QUICKJS_LIBRARY_DIR})

set(QUICKJS_MODULES arraybuffer-sink bjson blob deep directory json lexer list location misc path pointer predicate
                    queue repeater textcode sockets stream syscallerror inspect tree-walker virtual xml)

if(MODULE_MAGIC)
  list(APPEND QUICKJS_MODULES magic)
endif(MODULE_MAGIC)

if(MODULE_BCRYPT)
  list(APPEND QUICKJS_MODULES bcrypt)
endif(MODULE_BCRYPT)

if(MODULE_SERIAL)
  option(BUILD_LIBSERIALPORT "Build serialport library" ON)

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
        "-DENABLE_TEST:BOOL=OFF"
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}"
        "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
        "-DCMAKE_C_FLAGS:STRING=-w"
        "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
        "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      CMAKE_CACHE_DEFAULT_ARGS "-DENABLE_TEST:BOOL=OFF" "-DBUILD_SHARED_LIBS:BOOL=FALSE")

    link_directories("${CMAKE_CURRENT_BINARY_DIR}/libarchive")

  else(BUILD_LIBARCHIVE)
    include(FindLibArchive)
  endif(BUILD_LIBARCHIVE)

  if(LibArchive_FOUND)
    set(QUICKJS_MODULES archive ${QUICKJS_MODULES})
  endif(LibArchive_FOUND)
endif(MODULE_ARCHIVE)

if(MODULE_MAGIC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindLibMagic.cmake)
  find_libmagic()

  set(magic_MODULE magic)

  if(NOT LIBMAGIC_LIBRARY)
    set(magic_LIBRARIES magic)
  endif(NOT LIBMAGIC_LIBRARY)
endif(MODULE_MAGIC)

if(MODULE_MYSQL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindMariaDB.cmake)

  find_mariadb()
endif(MODULE_MYSQL)

if(MODULE_PGSQL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/FindPgSQL.cmake)
  find_pgsql()
endif(MODULE_PGSQL)

if(MODULE_WASM)
  option(BUILD_LIBWASM "Build wasm library" OFF)

  if(BUILD_WASM3)
    include(ExternalProject)
    ExternalProject_Add(
      wasm3
      SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3
      BINARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/third_party/wasm3
      CMAKE_CACHE_ARGS
        "-DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}" "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
        "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}" "-DCMAKE_C_FLAGS:STRING=-w"
        "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}" "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE")

    set(WASM3_LIBRARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/third_party/wasm30/source")
    link_directories("${WASM3_LIBRARY_DIR}")
    set(WASM3_LIBRARY m3)

  else(BUILD_WASM3)
    check_library_exists(m3 m3_ParseModule "${WASM3_LIBRARY_DIR}" HAVE_WASM3)
    if(HAVE_WASM3)
      set(WASM3_LIBRARY m3)
    endif(HAVE_WASM3)
  endif(BUILD_WASM3)

  if(WASM3_LIBRARY)
    set(QUICKJS_MODULES wasm ${QUICKJS_MODULES})
  endif(LibArchive_FOUND)
endif(MODULE_WASM)

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

#set(stream_SOURCES ${stream_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/stream.c")

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

file(GLOB tutf8e_SOURCES tutf8e/include/*.h tutf8e/include/tutf8e/*.h tutf8e/src/*.c)
file(GLOB libutf_SOURCES libutf/src/*.c libutf/include/*.h)
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)")
  set(X86 TRUE)
else()
  set(X86 FALSE)
endif()
if(X86)
  file(GLOB libbcrypt_SOURCES libbcrypt/*.c libbcrypt/*/*.c libbcrypt/*/*.S)
else(X86)
  file(GLOB libbcrypt_SOURCES libbcrypt/*.c libbcrypt/*/*.c)
endif(X86)

if(HAVE_SYS_MMAN_H)
  set(gpio_SOURCES src/gpio.c include/gpio.h)
endif(HAVE_SYS_MMAN_H)

# if(pigpio_LIBRARY)
#   set(gpio_LIBRARIES ${pigpio_LIBRARY})
# endif(pigpio_LIBRARY)

include_directories(${LibArchive_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/libutf/include
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
      "-DCMAKE_C_FLAGS:STRING=${MODULE_COMPILE_FLAGS}"
      "-DCMAKE_SYSROOT:PATH=${CMAKE_SYSROOT}"
      "-DCMAKE_TOOLCHAIN_FILE:FILEPATH=${CMAKE_TOOLCHAIN_FILE}"
      "-DCMAKE_VERBOSE_MAKEFILE:BOOL=${CMAKE_VERBOSE_MAKEFILE}"
      "-DCMAKE_BUILD_TYPE:STRING=${CMAKE_BUILD_TYPE}"
      "-DNO_PUBLIC_API:BOOL=TRUE"
    CMAKE_CACHE_DEFAULT_ARGS "-DBUILD_SHARED_LIBS:BOOL=FALSE" "-DNO_PUBLIC_API:BOOL=TRUE"
    INSTALL_COMMAND cmake -E echo "Skipping install step.")

  #link_directories("${CMAKE_CURRENT_BINARY_DIR}/libserialport")

  set(LIBSERIALPORT_LIBRARY serialport)
  set(LIBSERIALPORT_LIBRARY_DIR ${CMAKE_CURRENT_BINARY_DIR}/libserialport)
  set(LIBSERIALPORT_LIBRARY_FILE
      ${LIBSERIALPORT_LIBRARY_DIR}/${CMAKE_STATIC_LIBRARY_PREFIX}serialport${CMAKE_STATIC_LIBRARY_SUFFIX})

  add_library(Serial::Port STATIC IMPORTED GLOBAL)

  #dump(LIBSERIALPORT_LIBRARY_FILE CMAKE_STATIC_LIBRARY_PREFIX CMAKE_STATIC_LIBRARY_SUFFIX)

  set_target_properties(Serial::Port PROPERTIES IMPORTED_LOCATION "${LIBSERIALPORT_LIBRARY_FILE}"
                                                IMPORTED_IMPLIB "${LIBSERIALPORT_LIBRARY_FILE}")

  link_directories(${LIBSERIALPORT_LIBRARY_DIR})

  if(WIN32)
    list(APPEND LIBSERIALPORT_LIBRARY setupapi)
  endif(WIN32)
endif(BUILD_LIBSERIALPORT)

set(serial_DEPS libserialport)

if(HAVE_MMAP)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
else(HAVE_MMAP)
  if(WIN32 OR MINGW OR MSYS)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
    set(mmap_SOURCES ${mmap_SOURCES} src/mmap-win32.c)
  endif()
endif(HAVE_MMAP)

if(CACHE{CMAKE_BUILD_TYPE})
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif(CACHE{CMAKE_BUILD_TYPE})

set(CMAKE_REQUIRED_QUIET TRUE)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

#dump(VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)

check_flags("-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-function" CMAKE_C_FLAGS)

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  check_c_compiler_flag("-O0" O_OPT_NONE)
  if(O_OPT_NONE)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -O0" CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-O0")
  endif(O_OPT_NONE)

  check_c_compiler_flag("-ggdb" G_OPT_GDB)
  if(G_OPT_GDB)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -ggdb" CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-ggdb")
  endif(G_OPT_GDB)
  check_c_compiler_flag("-Wextra" G_WARN_EXTRA)
  if(G_WARN_EXTRA)
    if(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
      set(CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG} -Wextra" CACHE STRING "C compiler options")
    endif(NOT "${CMAKE_C_FLAGS_DEBUG}" MATCHES "-Wextra")
  endif(G_WARN_EXTRA)
endif(CMAKE_BUILD_TYPE STREQUAL "Debug")

check_function_def(js_debugger_build_backtrace)

#dump(CHECKED_FUNCTIONS CHECKED_INCLUDES)

file(GLOB TESTS tests/test_*.js)
relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

if(DO_TESTS)
  include(CTest)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    add_test(NAME "${BASE}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             COMMAND env QUICKJS_MODULE_PATH="${CMAKE_CURRENT_BINARY_DIR}" "${QJSM}" --bignum "${TEST}")
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

set(QJSM "${CMAKE_CURRENT_BINARY_DIR}/qjsm" CACHE FILEPATH "qjsm (QuickJS modular shell) interpreter")

include(CTest)

foreach(TEST_SOURCE ${TESTS_SOURCES})
  file(RELATIVE_PATH TEST_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}" "${TEST_SOURCE}")
  basename(TEST_NAME ${TEST_SOURCE} .js)
  add_test(NAME "${TEST_NAME}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}" COMMAND ${QJSM} --bignum
                                                                                       "${TEST_SOURCE}")

endforeach(TEST_SOURCE ${TESTS_SOURCES})

file(GLOB LIBJS ${CMAKE_CURRENT_SOURCE_DIR}/lib/*.js)
file(GLOB LIBLEXER ${CMAKE_CURRENT_SOURCE_DIR}/lib/lexer/*.js)
file(GLOB LIBXML ${CMAKE_CURRENT_SOURCE_DIR}/lib/xml/*.js)
list(REMOVE_ITEM LIBJS lib/require.js)
list(REMOVE_ITEM LIBJS lib/fs.js)
list(REMOVE_ITEM LIBJS lib/std.js)
list(REMOVE_ITEM LIBJS lib/os.js)
list(REMOVE_ITEM LIBJS lib/stream.js)

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

list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")

set(COMMON_MODULES util)
set(console_MODULES inspect)
set(db_MODULES list)
set(fs_MODULES syscallerror stream events path mmap misc textcode)
set(io_MODULES misc)
set(parser_MODULES path list)
set(process_MODULES path misc)
set(repl_MODULES inspect path io process)
set(require_MODULES path)
set(perf_hooks_MODULES misc)
set(stack_MODULES inspect location)
set(util_MODULES inspect path misc)
set(inspect_MODULES pointer)

foreach(LIB_MODULE ${LIB_MODULES})
  #message("Compile Module: ${LIB_MODULE}")

  set(MODLIST ${COMMON_MODULES} ${${LIB_MODULE}_MODULES})

  compile_module(lib/${LIB_MODULE}.js "${CMAKE_BINARY_DIR}/modules/${LIB_MODULE}.c" ${MODLIST})
endforeach(LIB_MODULE ${LIB_MODULES})

set(QUICKJS_INTERNAL FALSE)

#[[function(WATCH_INTERNAL_MODIFIED VAR VALUE UNKNOWN)
  set(OLD_VALUE "${${VAR}_VALUE}")
  message(STATUS "[1;34mMODIFIED[0m ${VAR} VALUE=${VALUE} OLD_VALUE=${OLD_VALUE} ${UNKNOWN}")
  set("${VAR}_VALUE" "${VALUE}" PARENT_SCOPE)
endfunction(WATCH_INTERNAL_MODIFIED VAR VALUE UNKNOWN)

function(WATCH_INTERNAL_READ VAR VALUE UNKNOWN)
  set(OLD_VALUE "${${VAR}_VALUE}")
  if(NOT "${OLD_VALUE}" STREQUAL "${VALUE}")
    set("${VAR}_VALUE" "${VALUE}" PARENT_SCOPE)
    #message(STATUS "[1;32mREAD[0m     ${VAR} VALUE=${VALUE} OLD_VALUE=${OLD_VALUE}")
  endif(NOT "${OLD_VALUE}" STREQUAL "${VALUE}")
endfunction(WATCH_INTERNAL_READ VAR VALUE UNKNOWN)

function(WATCH_INTERNAL_DEFINED VAR VALUE UNKNOWN)
  set(OLD_VALUE "${${VAR}_VALUE}")
  if(NOT "${OLD_VALUE}" STREQUAL "${VALUE}")
    set("${VAR}_VALUE" "${VALUE}" PARENT_SCOPE)
    message(STATUS "[1;32mDEINED[0m     ${VAR} VALUE=${VALUE} OLD_VALUE=${OLD_VALUE}")
  endif(NOT "${OLD_VALUE}" STREQUAL "${VALUE}")
endfunction(WATCH_INTERNAL_DEFINED VAR VALUE UNKNOWN)

function(WATCH_INTERNAL_REMOVED VAR VALUE UNKNOWN)
  set("${VAR}_VALUE" "" PARENT_SCOPE)
  message(STATUS "[1;31mREMOVED [0m     ${VAR}")
endfunction(WATCH_INTERNAL_REMOVED VAR VALUE UNKNOWN)

function(WATCH_INTERNAL VAR ACCESS VALUE)
  string(REPLACE "_ACCESS" "" B "${ACCESS}")
  string(REPLACE "UNKNOWN_" "" A "${B}")
  set(UNKNOWN "")
  if(NOT "${A}" STREQUAL "${B}")
    set(UNKNOWN "UNKNOWN")
  endif(NOT "${A}" STREQUAL "${B}")
  cmake_language(CALL "WATCH_INTERNAL_${A}" "${VAR}" "${VALUE}" "${UNKNOWN}")
endfunction(WATCH_INTERNAL VAR ACCESS VALUE)

variable_watch(QUICKJS_SOURCES_ROOT watch_internal)]]

if(NOT DEFINED QUICKJS_SOURCES_ROOT)
  if("${QUICKJS_SOURCES_ROOT}" STREQUAL "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
      set(QUICKJS_SOURCES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
  endif("${QUICKJS_SOURCES_ROOT}" STREQUAL "")
endif(NOT DEFINED QUICKJS_SOURCES_ROOT)

set(QUICKJS_INTERNAL FALSE)

if(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  if(EXISTS ${QUICKJS_SOURCES_ROOT}/quickjs.c)
    set(QUICKJS_INTERNAL TRUE)
  endif(EXISTS ${QUICKJS_SOURCES_ROOT}/quickjs.c)
endif(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")

set(QUICKJS_INTERNAL "${QUICKJS_INTERNAL}" CACHE BOOL "Generate quickjs-internal.h" FORCE)

if(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  set(QUICKJS_SOURCES_ROOT "${QUICKJS_SOURCES_ROOT}" CACHE PATH "QuickJS source directory" FORCE)
else(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  unset(QUICKJS_SOURCES_ROOT)
  unset(QUICKJS_SOURCES_ROOT CACHE)
  unset(QUICKJS_INTERNAL)
  unset(QUICKJS_INTERNAL CACHE)
endif(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")

dump(QUICKJS_SOURCES_ROOT QUICKJS_INTERNAL)

if(QUICKJS_INTERNAL)
  add_definitions(-DQUICKJS_INTERNAL=1)
else(QUICKJS_INTERNAL)
  add_definitions(-DQUICKJS_INTERNAL=0)
endif(QUICKJS_INTERNAL)

if(QUICKJS_INTERNAL)
  set(QUICKJS_STRUCT_SOURCES ${QUICKJS_SOURCES_ROOT}/quickjs.c ${QUICKJS_SOURCES_ROOT}/quickjs-libc.c)

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
  list(APPEND LIBRARY_SOURCES quickjs-internal.c)

  if(EXISTS "${QUICKJS_SOURCES_ROOT}/qjscalc.js")
    set(HAVE_QJSCALC TRUE CACHE BOOL "Have qjscalc.js")
    var2define(HAVE_QJSCALC)

    if(HAVE_QJSCALC)
      relative_path(QJSCALC_PATH "${CMAKE_CURRENT_SOURCE_DIR}" "${QUICKJS_SOURCES_ROOT}/qjscalc.js")
      compile_module("${QJSCALC_PATH}")
    endif(HAVE_QJSCALC)
  endif(EXISTS "${QUICKJS_SOURCES_ROOT}/qjscalc.js")
endif(QUICKJS_INTERNAL)

add_library(modules STATIC ${LIBRARY_SOURCES} ${tutf8e_SOURCES} ${libutf_SOURCES} ${libbcrypt_SOURCES})
set_target_properties(modules PROPERTIES COMPILE_FLAGS "-fPIC ${MODULE_COMPILE_FLAGS}")
add_library(compiled STATIC ${COMPILED_MODULES})

set(COMMON_LIBRARIES modules compiled)

get_directory_property(CURRENT_LINK_DIRS LINK_DIRECTORIES)

foreach(JS_MODULE ${QUICKJS_MODULES})
  make_module(${JS_MODULE})

  string(REGEX REPLACE "-" "_" QJSM_MODULE "${JS_MODULE}")

  list(APPEND QJSM_LIBS "${QJSM_MODULE}")
endforeach(JS_MODULE ${QUICKJS_MODULES})

if(TARGET qjs-inspect)
  target_link_libraries(qjs-inspect PRIVATE qjs-pointer)
endif(TARGET qjs-inspect)

if(TARGET qjs-tree-walker)
  target_link_libraries(qjs-tree-walker PRIVATE qjs-pointer)
endif(TARGET qjs-tree-walker)

if(TARGET qjs-xml)
  target_link_libraries(qjs-xml PRIVATE qjs-pointer)
endif(TARGET qjs-xml)

# XXX: why?
if(TARGET qjs-child-process)
  target_link_libraries(qjs-child-process PRIVATE qjs-pointer)
endif(TARGET qjs-child-process)

if(TARGET qjs-serial)
  target_link_directories(qjs-serial PRIVATE ${LIBSERIALPORT_LIBRARY_DIR})
  target_link_libraries(qjs-serial PRIVATE Serial::Port)
endif(TARGET qjs-serial)

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

set(NATIVE_BUILTINS child-process deep inspect lexer location misc path pointer predicate repeater tree-walker xml)
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
set_target_properties(qjsm PROPERTIES LINK_OPTIONS "${LINK_EXPORT};-L${QJSM_LIBDIR}" COMPILE_FLAGS "-w")

if(BUILD_SHARED_LIBS)
  set_target_properties(qjsm PROPERTIES RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}"
                                        INSTALL_RPATH "${QUICKJS_INSTALL_PREFIX}/${QUICKJS_LIBRARY_DIR}")
endif(BUILD_SHARED_LIBS)

target_link_directories(qjsm PUBLIC ${CMAKE_CURRENT_BINARY_DIR} ${QUICKJS_LIBRARY_DIR})

target_link_libraries(qjsm PUBLIC ${QJSM_LDADD} ${COMMON_LIBRARIES} ${QUICKJS_LIBRARY} ${LIBPTHREAD} ${LIBM}
                                  ${LIBWS2_32})

target_compile_definitions(
  qjsm PRIVATE _GNU_SOURCE=1 CONFIG_VERSION="${QUICKJS_VERSION}" QUICKJS_PREFIX="${QUICKJS_INSTALL_PREFIX}"
               LIBMAGIC_DB="${LIBMAGIC_DB}" CONFIG_BIGNUM=1)
install(TARGETS qjsm DESTINATION bin)

file(GLOB INSTALL_SCRIPTS [!.]*.js)

install(FILES ${INSTALL_SCRIPTS} DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE
                                                             GROUP_READ WORLD_EXECUTE WORLD_READ)

symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/list-imports.js")
symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/check-imports.js")
symlink(extract-structs.js "${CMAKE_INSTALL_PREFIX}/bin/extract-bindings.js")
