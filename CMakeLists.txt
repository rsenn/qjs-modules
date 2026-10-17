# qjs-modules: native C modules and JS libraries for QuickJS, and the qjsm shell that
# bundles them.
#
#    1. Project              5. QuickJS                 9. Tests
#    2. Options              6. System checks          10. Libraries, module targets
#    3. Helper scripts       7. Modules, dependencies  11. qjsm and its builtins
#    4. Platform checks      8. Sources, engine API    12. Install and summary

##############################################################################
# 1. Project
##############################################################################

cmake_minimum_required(VERSION 3.5)

project(qjs-modules VERSION 0.9 LANGUAGES C ASM)

message(STATUS "qjs-modules ${PROJECT_VERSION} for ${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}, "
               "${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION}, build type '${CMAKE_BUILD_TYPE}'")

# include-file checks link CMAKE_REQUIRED_LIBRARIES (libquickjs), as the function checks do
if(POLICY CMP0075)
  cmake_policy(SET CMP0075 NEW)
endif()

set(CMAKE_C_STANDARD 11)

# write compile_commands.json into the build dir (clangd, LSP); -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF disables it
if("${CMAKE_EXPORT_COMPILE_COMMANDS}" STREQUAL "")
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Write compile_commands.json to the build directory" FORCE)
endif()

##############################################################################
# 2. Options
# the switches of the project; MODULE_<NAME> turns an optional native module on or off
##############################################################################

# targets without a dynamic loader get static libraries only
if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten" OR DIET)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)
  message(STATUS "No shared libraries on this target: BUILD_SHARED_LIBS forced OFF")
endif(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten" OR DIET)

if(NOT BUILD_SHARED_LIBS)
  set(BUILD_STATIC_LIBS TRUE)
endif(NOT BUILD_SHARED_LIBS)

option(LINK_STATIC "Link libraries statically into the executables" OFF)
option(DEBUG_OUTPUT "Debug output" OFF)
option(DEBUG_ALLOC "Debug allocation" OFF)
option(DO_TESTS "Perform tests" ON)

# optional native modules
#
# archive/magic/mysql/pgsql/sqlite all bind to native system libraries
# (libarchive, libmagic, libmariadb, libpq, libsqlite3) that pkg-config
# happily "finds" on the host even while cross-compiling for wasm (it isn't
# sysroot-aware here), which would link host x86_64 .so files into an
# Emscripten build and fail. None of them have a wasm-usable counterpart set
# up in this tree, so default them off for WASI/Emscripten targets.
if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  option(MODULE_ARCHIVE "Build 'archive' module" OFF)
  option(MODULE_MAGIC "Build 'magic' module" OFF)
  option(MODULE_MYSQL "Build 'mysql' module" OFF)
  option(MODULE_PGSQL "Build 'pgsql' module" OFF)
  option(MODULE_SQLITE "Build 'sqlite' module" OFF)
else()
  option(MODULE_ARCHIVE "Build 'archive' module" ON)
  option(MODULE_MAGIC "Build 'magic' module" ON)
  option(MODULE_MYSQL "Build 'mysql' module" ON)
  option(MODULE_PGSQL "Build 'pgsql' module" ON)
  option(MODULE_SQLITE "Build 'sqlite' module" ON)
endif()
option(MODULE_BCRYPT "Build 'bcrypt' module" ON)
option(MODULE_SERIAL "Build 'serial' module" ON)
option(MODULE_GPIO "Build 'gpio' module (Raspberry Pi only)" OFF)

if(WASI OR EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
  # 'serial' (libserialport, needs termios/native tty ioctls) and 'gpio'
  # (raw /dev/mem + mmap register access) have no meaning in a wasm sandbox;
  # force them off even if the cache already has them enabled from a
  # previous non-wasm configure.
  set(MODULE_SERIAL OFF CACHE BOOL "Build 'serial' module" FORCE)
  set(MODULE_GPIO OFF CACHE BOOL "Build 'gpio' module (Raspberry Pi only)" FORCE)
endif()

# musl-gcc has no kernel headers, so libserialport's linux/serial.h is unreachable; forced off
# even if cached ON from a non-musl configure.
if(CMAKE_C_COMPILER MATCHES "musl")
  set(MODULE_SERIAL OFF CACHE BOOL "Build 'serial' module" FORCE)
endif()
option(MODULE_WASM "Build 'wasm' module" ON)
option(OPENBSD_GLOB "Use the bundled OpenBSD glob() (src/compat/glob.c) instead of the system glob()" ON)

##############################################################################
# 3. Helper scripts
# cmake/Functions.cmake  list/string/path helpers, message_table()
# cmake/Compat.cmake     small compatibility helpers
# cmake/Checks.cmake     check_*_def() wrappers that fill config.h
##############################################################################

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Functions.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Compat.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Checks.cmake)

##############################################################################
# 4. Platform checks
# headers, functions and compiler flags; the results end up in config.h
##############################################################################

message(STATUS "Probing platform headers, functions and compiler flags (results go to config.h)")

check_source_definitions(ATFILE GNU LARGEFILE LARGE_FILE LARGEFILE64 POSIX POSIX_C XOPEN XOPEN_EXTENDED)

check_no_unused_warn_flags()
check_no_warnings_flags(CMAKE_C_FLAGS_DEBUG)
check_debug_symbols_flags(CMAKE_C_FLAGS_DEBUG)

check_inline()

check_windows_native(WINDOWS_NATIVE)
var2define(WINDOWS_NATIVE)

check_functions(memrchr memmem)
check_functions(getegid geteuid getgid getpid getppid getsid getuid setegid seteuid setgid setuid)

check_functions(uname)

# posix_spawn() is only trusted when a test program using it runs
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
endif(NOT CMAKE_CROSSCOMPILING)

if(HAVE_POSIX_SPAWN)
  option(USE_SPAWN "Use POSIX spawn()" ON)
else(HAVE_POSIX_SPAWN)
  option(USE_SPAWN "Use POSIX spawn()" OFF)
endif(HAVE_POSIX_SPAWN)
if(HAVE_POSIX_SPAWN)
  message(STATUS "posix_spawn(): works (USE_SPAWN=${USE_SPAWN})")
else()
  message(STATUS "posix_spawn(): not usable (USE_SPAWN=${USE_SPAWN})")
endif()

##############################################################################
# 5. QuickJS
# find (or build, -DBUILD_QUICKJS=ON) the engine; probe what its library exports
##############################################################################

include(FindPkgConfig)

include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/UseMultiArch.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/QuickJSModule.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/qjsm.cmake)
quickjs_module_options(SHARED_DEFAULT ON STATIC_DEFAULT ON)
set(QUICKJS_FIND_QJSM OFF)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/FindQuickJS.cmake)

if(WASI)
  # WASI has no JS glue concept, so the build product is the raw .wasm
  # module (see the parent quickjs project's CMakeLists.txt for the same
  # reasoning). Emscripten, by contrast, wants its default .js suffix so
  # emcc emits the JS runtime glue alongside the .wasm -- that's what
  # actually makes qjsm runnable under Node or a browser.
  set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
endif(WASI)

if(WIN32 OR MINGW)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS TRUE)
endif(WIN32 OR MINGW)

find_quickjs()
configure_quickjs()

list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")

list(APPEND CMAKE_REQUIRED_LIBRARIES ${QUICKJS_LIBRARY})
list(APPEND CMAKE_REQUIRED_INCLUDES ${QUICKJS_INCLUDE_DIR})

include(CheckLibraryExists)

# The probes below cache their result per build dir, but the installed quickjs changes
# independently of it, so stale results must be dropped when the library or header changes.
set(QJS_PROBE_STAMP "")
foreach(QJS_PROBE_FILE "${QUICKJS_LIBRARY}" "${QUICKJS_INCLUDE_DIR}/quickjs.h")
  if(EXISTS "${QJS_PROBE_FILE}")
    file(SHA256 "${QJS_PROBE_FILE}" QJS_PROBE_HASH)
    string(APPEND QJS_PROBE_STAMP "${QJS_PROBE_HASH};")
  endif()
endforeach()

if(NOT "${QJS_PROBE_STAMP}" STREQUAL "${QUICKJS_PROBE_STAMP}")
  foreach(
    QJS_PROBE
    HAVE_JS_STD_FILE
    HAVE_JS_PROMISE_STATE
    HAVE_JS_PROMISE_RESULT
    HAVE_JS_GETMODULELOADERFUNC
    HAVE_JS_GETMODULELOADEROPAQUE
    HAVE_JS_GETCLASSID
    HAVE_JS_ISUNCATCHABLEERROR
    HAVE_JS_GETMODULESTATUS
    HAVE_JS_ISMODULEASYNC
    HAVE_JS_DEBUGGERBUILDBACKTRACE
    HAVE_DBUF_CLAIM
    HAVE_DBUF_REALLOC
    HAVE_QUICKJS_BIGNUM_EXT
    JS_MODULE_LOADER_SETTINGS
    JS_MODULE_LOADER_OLD)
    unset(${QJS_PROBE} CACHE)
  endforeach()

  set(QUICKJS_PROBE_STAMP "${QJS_PROBE_STAMP}" CACHE INTERNAL
                                                     "Hash of the quickjs library/header the probes ran against")
endif()

check_library_exists(m sqrt "" HAVE_LIBMATH)
if(HAVE_LIBMATH)
  set(LIBM m)
  link_libraries(m)
  message(STATUS "Math library: -l${LIBM}")
endif(HAVE_LIBMATH)

# find_library() (rather than a bare "pthread" name) so CMAKE_FIND_ROOT_PATH
# confines the search to the cross-compilation sysroot when one is active
# (see the mingw LIBPTHREAD_LIBRARY use below): the box this is built on
# also has an older, non-sysroot mingw lib directory containing a
# same-named but outdated libpthread lacking the clock_gettime32/64
# symbols pthread_time.h expects, and it comes first in the compiler's
# own implicit link search order, so a bare "-lpthread" silently picks
# the wrong copy there.
find_library(LIBPTHREAD_LIBRARY NAMES pthread winpthread)
if(LIBPTHREAD_LIBRARY)
  message(STATUS "Thread library: ${LIBPTHREAD_LIBRARY}")
endif()

check_library_exists(quickjs JS_PromiseState "${QUICKJS_LIBRARY_DIR}" HAVE_JS_PROMISE_STATE)

check_library_exists(quickjs JS_PromiseResult "${QUICKJS_LIBRARY_DIR}" HAVE_JS_PROMISE_RESULT)

set(CMAKE_REQUIRED_LINK_DIRECTORIES_OLD "${CMAKE_REQUIRED_LINK_DIRECTORIES}")
set(CMAKE_REQUIRED_LIBRARIES_OLD "${CMAKE_REQUIRED_LIBRARIES}")

list(APPEND CMAKE_REQUIRED_LINK_DIRECTORIES "${QUICKJS_LIBRARY_DIR}")
list(APPEND CMAKE_REQUIRED_LIBRARIES quickjs)

# check_function_def() (check_function_exists() under the hood) only
# checks that a symbol of this name links against CMAKE_REQUIRED_LIBRARIES
# at all - not that it comes from THIS QUICKJS_LIBRARY. Its result is also
# CACHE INTERNAL, so once true it stays true across reconfigures even if
# QUICKJS_LIBRARY_DIR later points at a different, older quickjs lacking
# the symbol (src/utils.c calls these unconditionally on #if HAVE_..., so
# a stale true here is a link failure, not a harmless no-op). Check the
# actual QUICKJS_LIBRARY file instead.
check_library_exists("${QUICKJS_LIBRARY}" JS_GetModuleLoaderFunc "${QUICKJS_LIBRARY_DIR}" HAVE_JS_GETMODULELOADERFUNC)

check_library_exists("${QUICKJS_LIBRARY}" JS_GetModuleLoaderOpaque "${QUICKJS_LIBRARY_DIR}"
                     HAVE_JS_GETMODULELOADEROPAQUE)

check_function_def(js_debugger_build_backtrace HAVE_JS_DEBUGGERBUILDBACKTRACE)

check_library_exists("${QUICKJS_LIBRARY}" JS_GetClassID "${QUICKJS_LIBRARY_DIR}" HAVE_JS_GETCLASSID)

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

##############################################################################
# 6. System checks
# headers and functions the modules use; HAVE_* values land in config.h
##############################################################################

include(CheckFunctionExists)
include(CheckIncludeFile)
include(CheckCSourceRuns)

set(CMAKE_REQUIRED_QUIET OFF)

check_include_def(libbf.h)

# the BigFloat/BigDecimal extension was removed upstream (2025-04-26): CONFIG_BIGNUM stays off

set(CMAKE_REQUIRED_QUIET ON)

check_functions_def(vfork fork waitpid execvpe setsid)

check_functions_def(execve execvpe)
check_include_def(sys/wait.h)
check_include_def(sys/syscall.h)

if(USE_SPAWN)
  check_function_and_include(posix_spawnp spawn.h HAVE_SPAWN)
endif(USE_SPAWN)

check_includes_def(linux/ipx.h linux/x25.h linux/can.h linux/nfc.h linux/ax25.h linux/netlink.h linux/atalk.h
                   linux/phonet.h linux/if_alg.h)

check_include_def(threads.h)
check_include_def(sys/mman.h)
check_function_def(mmap)
check_includes_def(sys/param.h sys/stat.h unistd.h dirent.h)
check_functions_def(issetugid)

if(NOT ANDROID)
  check_include_def(pwd.h)
  check_functions_def(getpwuid getpwnam)
endif(NOT ANDROID)

check_external(getdents64 "" "" HAVE_GETDENTS64)
if(NOT HAVE_GETDENTS64)
  check_external(getdents "" "" HAVE_GETDENTS)
endif(NOT HAVE_GETDENTS64)

check_include_def(termios.h)
check_function_def(ioctl)
check_function_def(realpath)
check_functions_def(link linkat symlink symlinkat)
check_function_def(strverscmp)
check_function_def(fmemopen)
check_functions(fcntl wordexp inotify_init1)
check_includes_def(fcntl.h alloca.h wordexp.h sys/inotify.h)

check_functions_def(stat lstat fstat)
check_function_def(access)
check_functions_def(fnmatch daemon)

check_functions_def(chmod fchmod)
check_functions_def(chown fchown lchown)
check_functions_def(fsync fdatasync)
check_functions_def(truncate ftruncate)
check_functions_def(utime utimes futimes lutimes)
check_include_def(sys/time.h)

check_include_file(fnmatch.h HAVE_FNMATCH_H_2)

check_function_and_include(fcntl fcntl.h)
check_function_and_include(fnmatch fnmatch.h)

if(OPENBSD_GLOB)
  # HAVE_GLOB left undefined: src/compat/glob.c is compiled and quickjs-misc.c maps glob() to openbsd_glob()
  unset(HAVE_GLOB CACHE)
  unset(HAVE_GLOB_H CACHE)
  message(STATUS "glob(): bundled OpenBSD implementation (OPENBSD_GLOB=ON)")
else(OPENBSD_GLOB)
  check_function_and_include(glob glob.h)
  message(STATUS "glob(): system implementation (OPENBSD_GLOB=OFF)")
endif(OPENBSD_GLOB)

check_function_and_include(wordexp wordexp.h)

check_function_and_include(inotify_init1 sys/inotify.h HAVE_INOTIFY)

check_function_and_include(sysinfo sys/sysinfo.h)

if(NOT NO_TEMPNAM)
  check_function_def(tempnam)
else(NOT NO_TEMPNAM)
  set(HAVE_TEMPNAM OFF)
endif(NOT NO_TEMPNAM)

if(NOT NO_MKSTEMP)
  check_function_def(mkstemp)
else(NOT NO_MKSTEMP)
  set(HAVE_MKSTEMP OFF)
endif(NOT NO_MKSTEMP)

##############################################################################
# 7. Modules and their dependencies
# QUICKJS_MODULES is the list of native modules that get built; each optional one
# needs its library (system, or built from third_party/)
##############################################################################

message(STATUS "Selecting native modules and locating their libraries")

include_directories(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_CURRENT_BINARY_DIR}
                    ${CMAKE_CURRENT_SOURCE_DIR}/third_party ${QUICKJS_INCLUDE_DIRS})
link_directories(${QUICKJS_LIBRARY_DIR})

set(QUICKJS_MODULES arraybuffer-sink bjson blob deep directory json lexer list location misc path pointer predicate
                    queue repeater textcode sockets stream syscallerror inspect tree-walker virtual xml yaml)

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

if(MODULE_GPIO)
  if(NOT HAVE_SYS_MMAN_H)
    message(WARNING "MODULE_GPIO requires sys/mman.h, disabling")
  else(NOT HAVE_SYS_MMAN_H)
    list(APPEND QUICKJS_MODULES gpio)
  endif(NOT HAVE_SYS_MMAN_H)
endif(MODULE_GPIO)

if(MODULE_ARCHIVE)
  option(BUILD_LIBARCHIVE "Build archive library" OFF)

  if(BUILD_LIBARCHIVE)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/BuildLibArchive.cmake)

    # Built once per enabled module-linkage kind (BUILD_SHARED/BUILD_STATIC):
    # a shared qjs-archive (.so) needs a PIC libarchive, a static qjs-archive-static (.a)
    # doesn't, so a single non-PIC-by-default build can't safely serve both.
    if(BUILD_SHARED)
      build_libarchive(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} shared ON)
    endif(BUILD_SHARED)
    if(BUILD_STATIC)
      build_libarchive(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} static OFF)
    endif(BUILD_STATIC)

    set(LibArchive_LIBRARIES_shared "${LibArchive_LIBRARIES_shared}")
    set(LibArchive_LIBRARIES_static "${LibArchive_LIBRARIES_static}")
    set(LibArchive_INCLUDE_DIRS ${LibArchive_INCLUDE_DIRS_shared} ${LibArchive_INCLUDE_DIRS_static})
    list(REMOVE_DUPLICATES LibArchive_INCLUDE_DIRS)
    set(LibArchive_FOUND TRUE)
  else(BUILD_LIBARCHIVE)
    include(FindLibArchive)
  endif(BUILD_LIBARCHIVE)

  if(LibArchive_FOUND)
    set(QUICKJS_MODULES archive ${QUICKJS_MODULES})
  endif(LibArchive_FOUND)
endif(MODULE_ARCHIVE)

if(MODULE_MAGIC)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/FindLibMagic.cmake)
  find_libmagic()

  set(magic_MODULE magic)

  if(NOT (HAVE_LIBMAGIC AND HAVE_MAGIC_H))
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/BuildLibMagic.cmake)

    if(BUILD_SHARED)
      build_libmagic(${CMAKE_CURRENT_BINARY_DIR} shared ON)
      set(magic_LIBRARIES ${LibMagic_LIBRARIES_shared})
      set(magic_LIBRARIES_shared ${LibMagic_LIBRARIES_shared})
      include_directories(${LIBMAGIC_INCLUDE_DIR_shared})
    endif(BUILD_SHARED)
    if(BUILD_STATIC)
      build_libmagic(${CMAKE_CURRENT_BINARY_DIR} static OFF)
      if(NOT BUILD_SHARED)
        set(magic_LIBRARIES ${LibMagic_LIBRARIES_static})
      endif()
      set(magic_LIBRARIES_static ${LibMagic_LIBRARIES_static})
      include_directories(${LIBMAGIC_INCLUDE_DIR_static})
    endif(BUILD_STATIC)

    if(NOT LIBMAGIC_DB)
      if(BUILD_SHARED)
        set(LIBMAGIC_DB "${LIBMAGIC_DB_shared}")
      else()
        set(LIBMAGIC_DB "${LIBMAGIC_DB_static}")
      endif()
    endif()
  elseif(NOT LIBMAGIC_LIBRARY)
    set(magic_LIBRARIES magic)
  endif()
endif(MODULE_MAGIC)

if(MODULE_MYSQL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/FindMariaDB.cmake)

  find_mariadb()

endif(MODULE_MYSQL)

if(MODULE_PGSQL)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/FindPgSQL.cmake)
  find_pgsql()
endif(MODULE_PGSQL)

if(MODULE_SQLITE)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/FindSQLite.cmake)
  find_sqlite()

  if(NOT (LIBSQLITE_LIBRARY AND HAVE_SQLITE3_H))
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/BuildSQLite.cmake)

    if(BUILD_SHARED)
      build_sqlite(${CMAKE_CURRENT_BINARY_DIR} shared ON)
      set(sqlite_LIBRARIES ${SQLite_LIBRARIES_shared})
      set(sqlite_LIBRARIES_shared ${SQLite_LIBRARIES_shared})
      include_directories(${SQLITE_INCLUDE_DIR_shared})
    endif(BUILD_SHARED)
    if(BUILD_STATIC)
      build_sqlite(${CMAKE_CURRENT_BINARY_DIR} static OFF)
      if(NOT BUILD_SHARED)
        set(sqlite_LIBRARIES ${SQLite_LIBRARIES_static})
      endif()
      set(sqlite_LIBRARIES_static ${SQLite_LIBRARIES_static})
      include_directories(${SQLITE_INCLUDE_DIR_static})
    endif(BUILD_STATIC)

    if(NOT "sqlite" IN_LIST QUICKJS_MODULES)
      list(APPEND QUICKJS_MODULES sqlite)
    endif()
  endif()
endif(MODULE_SQLITE)

if(MODULE_WASM)
  # exactly one engine is linked: wasm3 (default, shares Memory objects) or wamr
  set(WASM_BACKEND "wasm3" CACHE STRING "WebAssembly engine for the 'wasm' module: wasm3 or wamr")
  set_property(CACHE WASM_BACKEND PROPERTY STRINGS wasm3 wamr)

  if(NOT WASM_BACKEND STREQUAL "wasm3" AND NOT WASM_BACKEND STREQUAL "wamr")
    message(FATAL_ERROR "WASM_BACKEND must be wasm3 or wamr, not '${WASM_BACKEND}'")
  endif()

  message(STATUS "WebAssembly backend: ${WASM_BACKEND}")

  if(WASM_BACKEND STREQUAL "wasm3")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/BuildWasm3.cmake)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/wasm3/source)

    if(BUILD_SHARED)
      build_wasm3(${CMAKE_CURRENT_BINARY_DIR} shared ON)
      set(wasm_LIBRARIES_shared ${WASM3_LIBRARIES_shared})
    endif(BUILD_SHARED)
    if(BUILD_STATIC)
      build_wasm3(${CMAKE_CURRENT_BINARY_DIR} static OFF)
      set(wasm_LIBRARIES_static ${WASM3_LIBRARIES_static})
    endif(BUILD_STATIC)

    set(wasm_SOURCES src/wasm-sections.c src/wasm3-backend.c)
  else()
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/cmake/wamr ${CMAKE_CURRENT_BINARY_DIR}/deps-wamr EXCLUDE_FROM_ALL)
    include_directories(${CMAKE_CURRENT_SOURCE_DIR}/third_party/wamr/core/iwasm/include)

    set(wasm_LIBRARIES_shared wamr_vmlib)
    set(wasm_LIBRARIES_static wamr_vmlib)
    set(wasm_SOURCES src/wasm-sections.c src/wamr-backend.c)
  endif()

  if(NOT "wasm" IN_LIST QUICKJS_MODULES)
    list(APPEND QUICKJS_MODULES wasm)
  endif()
endif(MODULE_WASM)

if(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
else(HAVE_SYS_WAIT_H AND HAVE_WAITPID)
  if(WIN32 OR MINGW)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} child-process)
  endif(WIN32 OR MINGW)
endif(HAVE_SYS_WAIT_H AND HAVE_WAITPID)

##############################################################################
# 8. Sources and engine API
# portable fallbacks for missing libc functions, the js_module_loader()/dbuf API of the
# linked QuickJS, and the sources of the helper libraries
##############################################################################

if(EXISTS "${QUICKJS_H}")
  set(COMMON_HEADERS ${CUTILS_H} ${QUICKJS_H})
endif()

check_function_def(strverscmp)

if(NOT HAVE_STRVERSCMP)
  set(utils_SOURCES ${utils_SOURCES} src/compat/strverscmp.c)
endif(NOT HAVE_STRVERSCMP)

check_function_def(memmem)

if(NOT HAVE_MEMMEM)
  set(utils_SOURCES ${utils_SOURCES} src/compat/memmem.c)
endif(NOT HAVE_MEMMEM)

if(WIN32 OR MINGW)
  set(path_SOURCES ${path_SOURCES} src/compat/readlink.c)
endif(WIN32 OR MINGW)

if(WIN32 OR MINGW OR "${CMAKE_SYSTEM_NAME}" STREQUAL Windows)
  set(sockets_LIBRARIES mswsock ws2_32)
  set(sockets_SOURCES ${sockets_SOURCES} src/win32/socketpair.c)
  set(HAVE_INET_PTON TRUE)
  set(HAVE_INET_NTOP TRUE)

  check_include_def(afunix.h)
else()
  check_functions(inet_pton inet_ntop)
endif()

set(CMAKE_REQUIRED_QUIET OFF)
set(CMAKE_REQUIRED_FLAGS "-DCOMPILE_DEFINITIONS=-I${QUICKJS_INCLUDE_DIR}")

compile_code(
  JS_MODULE_LOADER_SETTINGS
  "#include \"quickjs-libc.h\"\nJSContext* ctx = 0;\nint main(void) { JSModuleDef* m = js_module_loader(ctx, \"\", 0, JS_UNDEFINED); return 0; }"
)

if(NOT JS_MODULE_LOADER_SETTINGS)
  compile_code(
    JS_MODULE_LOADER_OLD
    "#include \"quickjs-libc.h\"\nJSContext* ctx = 0;\nint main(void) { JSModuleDef* m = js_module_loader(ctx, \"\", 0); return 0; }"
  )

  if(NOT JS_MODULE_LOADER_OLD)
    message(FATAL "Failed js_module_loader() arguments test!")
  endif(NOT JS_MODULE_LOADER_OLD)
endif(NOT JS_MODULE_LOADER_SETTINGS)

set(JS_MODULE_LOADER_SETTINGS "${JS_MODULE_LOADER_SETTINGS}" CACHE BOOL "New js_module_loader() with 4 arguments")
set(JS_MODULE_LOADER_OLD "${JS_MODULE_LOADER_OLD}" CACHE BOOL "Old js_module_loader() with 3 arguments")

# detected or derived, not for editing: hidden from ccmake/cmake-gui unless "advanced" is shown
foreach(ADVANCED_VAR JS_MODULE_LOADER_OLD JS_MODULE_LOADER_SETTINGS CONFIG_SHEXT CONFIG_VERSION OPENBSD_GLOB)
  if(DEFINED CACHE{${ADVANCED_VAR}})
    mark_as_advanced(${ADVANCED_VAR})
  endif()
endforeach(ADVANCED_VAR)

if(JS_MODULE_LOADER_SETTINGS)
  message(STATUS "js_module_loader(): 4 arguments (settings)")
else()
  message(STATUS "js_module_loader(): 3 arguments")
endif()

unset(CMAKE_REQUIRED_FLAGS)
unset(CMAKE_REQUIRED_INCLUDES)

check_library_exists("${QUICKJS_LIBRARY}" JS_IsUncatchableError "${QUICKJS_LIBRARY_DIR}" HAVE_JS_ISUNCATCHABLEERROR)

# module status / top-level-await accessors, listed by moduleList and getModule()
check_library_exists("${QUICKJS_LIBRARY}" JS_GetModuleStatus "${QUICKJS_LIBRARY_DIR}" HAVE_JS_GETMODULESTATUS)
check_library_exists("${QUICKJS_LIBRARY}" JS_IsModuleAsync "${QUICKJS_LIBRARY_DIR}" HAVE_JS_ISMODULEASYNC)

check_library_exists("${QUICKJS_LIBRARY}" dbuf_claim "${QUICKJS_LIBRARY_DIR}" HAVE_DBUF_CLAIM)

if(NOT HAVE_DBUF_CLAIM)
  check_library_exists("${QUICKJS_LIBRARY}" dbuf_realloc "${QUICKJS_LIBRARY_DIR}" HAVE_DBUF_REALLOC)
endif(NOT HAVE_DBUF_CLAIM)

# quickjs (between 2025-09-13 and 2026-06-04) renamed dbuf_realloc(buf, new_size) to
# dbuf_claim(buf, additional_size), where additional_size = new_size - buf->size. Whichever of
# the two -lquickjs actually exports, include/defines.h emulates the other one in terms of it
# (function-like macros can't be defined on the command line, so QUICKJS_DBUF_REALLOC /
# QUICKJS_DBUF_CLAIM just flag which one is present).

if(HAVE_DBUF_CLAIM)
  message(STATUS "dbuf API: dbuf_claim()")
else()
  message(STATUS "dbuf API: dbuf_realloc()")
endif()

check_library_exists("${socket_LIBRARIES}" recvmmsg "" HAVE_RECVMMSG)

check_library_exists("${socket_LIBRARIES}" recvmsg "" HAVE_RECVMSG)

check_library_exists("${socket_LIBRARIES}" sendmmsg "" HAVE_SENDMMSG)

check_library_exists("${socket_LIBRARIES}" sendmsg "" HAVE_SENDMSG)

check_library_exists("${socket_LIBRARIES}" accept4 "" HAVE_ACCEPT4)

if(NOT HAVE_INET_PTON)
  set(sockets_SOURCES ${sockets_SOURCES} src/compat/inet_pton.c)
endif(NOT HAVE_INET_PTON)

if(NOT HAVE_INET_NTOP)
  set(sockets_SOURCES ${sockets_SOURCES} src/compat/inet_ntop.c)
endif(NOT HAVE_INET_NTOP)

list(APPEND sockets_LIBRARIES qjs-syscallerror)
list(APPEND misc_LIBRARIES qjs-syscallerror)

file(GLOB tutf8e_SOURCES
  third_party/tutf8e/include/*.h
  third_party/tutf8e/include/tutf8e/*.h
  third_party/tutf8e/src/*.c
)

file(GLOB libutf_SOURCES
  third_party/libutf/src/*.c
  third_party/libutf/include/*.h
)

# CMAKE_SYSTEM_PROCESSOR alone isn't enough: it's left empty by the mingw
# cross toolchain files here (they never set it), so it always evaluated
# to X86=FALSE when cross-compiling, silently dropping libbcrypt's
# x86.S -- while crypt_blowfish.c, compiled by the same i686-w64-mingw32
# compiler, still declares BF_ASM=1 and references x86.S's
# _BF_body_r whenever the compiler itself defines __i386__, causing an
# undefined reference at link time. Ask the compiler directly instead,
# matching crypt_blowfish.c's own condition.
check_compile(HAVE_I386 "#ifndef __i386__\n#error not i386\n#endif\nint main(void) { return 0; }\n")
if(CMAKE_SYSTEM_PROCESSOR MATCHES "(x86)|(X86)" OR HAVE_I386)
  set(X86 TRUE)
else()
  set(X86 FALSE)
endif()

if(X86)
  file(GLOB libbcrypt_SOURCES third_party/libbcrypt/*.c third_party/libbcrypt/*/*.c third_party/libbcrypt/*/*.S)
else(X86)
  file(GLOB libbcrypt_SOURCES third_party/libbcrypt/*.c third_party/libbcrypt/*/*.c)
endif(X86)

if(HAVE_SYS_MMAN_H)
  set(gpio_SOURCES src/gpio.c include/gpio.h)
endif(HAVE_SYS_MMAN_H)

include_directories(${LibArchive_INCLUDE_DIRS} ${CMAKE_CURRENT_SOURCE_DIR}/third_party/libutf/include
                    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/tutf8e/include)
set(archive_LIBRARIES ${LibArchive_LIBRARIES})
if(LibArchive_LIBRARIES_shared)
  set(archive_LIBRARIES_shared ${LibArchive_LIBRARIES_shared})
endif()
if(LibArchive_LIBRARIES_static)
  set(archive_LIBRARIES_static ${LibArchive_LIBRARIES_static})
endif()

if(MODULE_SERIAL AND BUILD_LIBSERIALPORT)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/deps/BuildLibSerialPort.cmake)

  # Built once per enabled module-linkage kind: a shared qjs-serial (.so) needs a PIC
  # libserialport, a static qjs-serial-static (.a) doesn't -- see build_libserialport().
  if(BUILD_SHARED)
    build_libserialport(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} shared ON)
  endif()

  if(BUILD_STATIC)
    build_libserialport(${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR} static OFF)
  endif(BUILD_STATIC)

  # Kept for existing consumers expecting a single LIBSERIALPORT_LIBRARY_DIR (e.g. the
  # link_directories() call further down) -- either variant's directory works, since
  # target_link_libraries() against the IMPORTED Serial::Port::<suffix> targets below
  # resolves by absolute path regardless.
  if(LIBSERIALPORT_LIBRARY_DIR_shared)
    set(LIBSERIALPORT_LIBRARY_DIR "${LIBSERIALPORT_LIBRARY_DIR_shared}")
  else()
    set(LIBSERIALPORT_LIBRARY_DIR "${LIBSERIALPORT_LIBRARY_DIR_static}")
  endif()

  set(serial_LIBRARIES_shared ${LIBSERIALPORT_LIBRARY_shared})
  set(serial_LIBRARIES_static ${LIBSERIALPORT_LIBRARY_static})

  if(WIN32)
    list(APPEND serial_LIBRARIES_shared setupapi)
    list(APPEND serial_LIBRARIES_static setupapi)
  endif(WIN32)
endif()

if(HAVE_MMAP)
  set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
else(HAVE_MMAP)
  if(WIN32 OR MINGW OR MSYS)
    set(QUICKJS_MODULES ${QUICKJS_MODULES} mmap)
    set(mmap_SOURCES ${mmap_SOURCES} src/win32/mmap.c)
  endif()
endif(HAVE_MMAP)

if(CACHE{CMAKE_BUILD_TYPE})
  set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS Debug Release MinSizeRel RelWithDebInfo)
endif(CACHE{CMAKE_BUILD_TYPE})

set(CMAKE_REQUIRED_QUIET ON)

check_flag("-fvisibility=hidden" VISIBILITY_HIDDEN MODULE_COMPILE_FLAGS)
check_flags("-Wall;-Wno-unused-parameter;-Wno-unused-variable;-Wno-unused-function" CMAKE_C_FLAGS)

check_function_def(js_debugger_build_backtrace)

##############################################################################
# 9. Tests
# one ctest entry per tests/test_*.js, run with qjsm; tests marked @portable also run
# under node, bun and deno when installed
##############################################################################

# cmake-level checks: cmake -P scripts under tests/cmake/
add_test(NAME cmake-scan-computed-imports
         COMMAND ${CMAKE_COMMAND} -DCMAKE_CURRENT_BINARY_DIR=${CMAKE_CURRENT_BINARY_DIR}
                 -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/cmake/scan-computed-imports.cmake)

# C unit tests of engine-free headers: tests/c/test-*.c, each a program that exits non-zero on failure
file(GLOB C_TESTS tests/c/test-*.c)
foreach(C_TEST ${C_TESTS})
  get_filename_component(C_TEST_NAME "${C_TEST}" NAME_WE)
  add_executable(${C_TEST_NAME} "${C_TEST}")
  target_include_directories(${C_TEST_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
  string(REGEX REPLACE "^test-" "" C_TEST_SHORT "${C_TEST_NAME}")
  add_test(NAME "c-${C_TEST_SHORT}" COMMAND ${C_TEST_NAME})
endforeach(C_TEST)

# jread-positions needs the jread tokenizer, which uses Vector
add_executable(jread-positions tests/c/jread-positions.c src/json/jread.c)
target_include_directories(jread-positions PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(jread-positions PRIVATE modules ${QUICKJS_LIBRARY} m)
add_test(NAME c-jread-positions COMMAND jread-positions)

# events-sinks drives the writers through JsonEventSink and XmlEventSink
add_executable(events-sinks tests/c/events-sinks.c src/json/jevent.c src/json/jwrite.c src/xml/xevent.c src/xml/xwrite.c)
target_include_directories(events-sinks PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(events-sinks PRIVATE modules ${QUICKJS_LIBRARY} m)
add_test(NAME c-events-sinks COMMAND events-sinks)

# pump-roundtrip: every parser driving a writer through the sinks, at several chunk sizes
add_executable(pump-roundtrip tests/c/pump-roundtrip.c)
target_include_directories(pump-roundtrip PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(pump-roundtrip PRIVATE modules ${QUICKJS_LIBRARY} m)
add_test(NAME c-pump-roundtrip COMMAND pump-roundtrip)

file(GLOB TESTS tests/test_*.js tests/unittests/test-*.js)

# tests of modules that are off by default
if(NOT MODULE_GPIO)
  list(FILTER TESTS EXCLUDE REGEX "test-gpio\\.js$")
endif(NOT MODULE_GPIO)
relative_path(TESTS "${CMAKE_CURRENT_SOURCE_DIR}" ${TESTS})

set(QJSM "${CMAKE_CURRENT_BINARY_DIR}/qjsm" CACHE FILEPATH "qjsm (QuickJS modular shell) interpreter")

if(DO_TESTS)
  include(CTest)

  foreach(RUNTIME node bun deno)
    find_program(${RUNTIME}_EXECUTABLE ${RUNTIME})
  endforeach(RUNTIME node bun deno)

  foreach(TEST ${TESTS})
    basename(BASE "${TEST}")
    string(REPLACE "test_" "" NAME "${BASE}")
    if(CONFIG_BIGNUM)
      set(BIGNUM_ARG --bignum)
    else()
      set(BIGNUM_ARG)
    endif()
    add_test(NAME "${BASE}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
             COMMAND env "QUICKJS_MODULE_PATH=${CMAKE_CURRENT_BINARY_DIR};${CMAKE_CURRENT_SOURCE_DIR}/lib" "${QJSM}"
                     ${BIGNUM_ARG} "${TEST}")

    # a test marked `@portable` in its first lines also runs on the other runtimes
    # that are installed: <name>.node, <name>.bun, <name>.deno
    file(READ "${CMAKE_CURRENT_SOURCE_DIR}/${TEST}" TEST_HEAD LIMIT 512)
    if(TEST_HEAD MATCHES "@portable")
      foreach(RUNTIME node bun deno)
        if(NOT ${RUNTIME}_EXECUTABLE)
          continue()
        endif(NOT ${RUNTIME}_EXECUTABLE)
        if(RUNTIME STREQUAL "deno")
          set(RUNTIME_ARGS run -A "--import-map=tests/deno-import-map.json")
        else()
          set(RUNTIME_ARGS)
        endif()
        add_test(NAME "${BASE}.${RUNTIME}" WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                 COMMAND "${${RUNTIME}_EXECUTABLE}" ${RUNTIME_ARGS} "${TEST}")
      endforeach(RUNTIME node bun deno)
    endif(TEST_HEAD MATCHES "@portable")
  endforeach(TEST ${TESTS})
  list(LENGTH TESTS TEST_COUNT)
  message(STATUS "Tests: ${TEST_COUNT} registered with ctest, run with: ctest (or make test)")
else(DO_TESTS)
  message(STATUS "Tests: disabled (DO_TESTS=OFF)")
endif(DO_TESTS)

check_c_source_runs(
  " # include <stdbool.h>\nbool foo(int a, int b, int *c) {\n   return __builtin_mul_overflow(a, b,
    # c);\n}\nint main() {\n   int out;\n   if (foo(1, 2, &out)) {\n       return 0;\n   }\n return
    # 0;\n}" HAVE__BUILTIN_MUL_OVERFLOW)

##############################################################################
# 10. Libraries and module targets
# libmodules.a (shared helpers), then qjs-<name> (.so) and qjs-<name>-static (.a) per module
##############################################################################

addprefix(SHARED_TARGETS "qjs-" ${QUICKJS_MODULES})
addsuffix(STATIC_TARGETS "-static" ${SHARED_TARGETS})

set(QJSM_LIBDIR "${CMAKE_CURRENT_BINARY_DIR}")

if(NOT WASI)
  if(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
    set(CMAKE_IMPORT_LIBRARY_PREFIX "")
    set(CMAKE_IMPORT_LIBRARY_SUFFIX ".bc")
  endif(EMSCRIPTEN OR "${CMAKE_SYSTEM_NAME}" STREQUAL "Emscripten")
endif(NOT WASI)

file(GLOB LIBRARY_SOURCES include/*.h include/json/*.h include/xml/*.h include/compat/*.h src/*.c src/json/*.c src/xml/*.c src/compat/*.c)
list(REMOVE_ITEM LIBRARY_SOURCES src/qjsm.c)
foreach(F wasm-sections.c wasm3-backend.c wamr-backend.c)
  list(REMOVE_ITEM LIBRARY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/${F}")
endforeach()
if(HAVE_INET_PTON)
  list(REMOVE_ITEM LIBRARY_SOURCES src/compat/inet_pton.c)
endif(HAVE_INET_PTON)
if(HAVE_INET_NTOP)
  list(REMOVE_ITEM LIBRARY_SOURCES src/compat/inet_ntop.c)
endif(HAVE_INET_NTOP)
if(mmap_SOURCES)
  list(APPEND LIBRARY_SOURCES src/win32/mmap.c)
endif(mmap_SOURCES)
if(WIN32 OR MINGW OR "${CMAKE_SYSTEM_NAME}" STREQUAL Windows)
  list(APPEND LIBRARY_SOURCES src/win32/socketpair.c)
endif()
if(NOT WIN32 AND NOT CYGWIN AND NOT (CMAKE_SYSTEM_NAME STREQUAL "Linux") AND NOT ANDROID)
  # src/getdents.c's non-Windows backend only works on Linux/Android (it needs
  # a linkable getdents64()/getdents(), __dietlibc__, or the raw
  # syscall(SYS_getdents64, ...) fallback, none of which exist elsewhere, e.g.
  # on Emscripten's musl libc) - quickjs-directory.c provides its own portable
  # opendir()/readdir()-based Directory implementation for every other
  # platform instead of linking this file. See BUGS
  # (getdents-emscripten-no-syscall).
  list(REMOVE_ITEM LIBRARY_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/getdents.c")
endif()

link_directories(${QJSM_LIBDIR} ${LIBSERIALPORT_LIBRARY_DIR})

set(predicate_LIBRARIES ${LIBM})

set(lexer_DEPS qjs-predicate)

file(GLOB TESTS_SOURCES tests/test_*.js)
list(REMOVE_ITEM TESTS_SOURCES "test_lexer.js")
source_group(TESTS_GROUP FILES ${TESTS_SOURCES})

##############################################################################
# 11. qjsm and its builtins
# what is baked into the qjsm binary: native C modules, JS modules compiled to bytecode
# by qjsc, and external qjs-<name> modules; see cmake/qjsm.cmake
##############################################################################

qjsm_init_builtins()

qjsm_resolve_builtins()
list(APPEND CMAKE_FIND_LIBRARY_PREFIXES "")
qjsm_declare_imports()

if(NOT DEFINED QUICKJS_SOURCES_ROOT)
  if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)
    set(QUICKJS_SOURCES_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)

    include(${QUICKJS_SOURCES_ROOT}/defs.cmake)
    include(${QUICKJS_SOURCES_ROOT}/cmake/check-libraries.cmake)

    unset(QUICKJS_INCLUDE_DIR CACHE)
    set(QUICKJS_INCLUDE_DIR "${QUICKJS_SOURCES_ROOT}")

    include_directories(${QUICKJS_INCLUDE_DIR})
  endif(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/../defs.cmake)

  if("${QUICKJS_SOURCES_ROOT}" STREQUAL "")
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
      set(QUICKJS_SOURCES_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
    endif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../quickjs.c")
  endif("${QUICKJS_SOURCES_ROOT}" STREQUAL "")
endif(NOT DEFINED QUICKJS_SOURCES_ROOT)

if(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  set(QUICKJS_SOURCES_ROOT "${QUICKJS_SOURCES_ROOT}" CACHE PATH "QuickJS source directory" FORCE)
else(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")
  unset(QUICKJS_SOURCES_ROOT)
  unset(QUICKJS_SOURCES_ROOT CACHE)
endif(NOT "${QUICKJS_SOURCES_ROOT}" STREQUAL "")

qjsm_compile_builtins()

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake" "${CMAKE_CURRENT_BINARY_DIR}/config.h")
add_definitions(-DHAVE_CONFIG_H)

add_library(modules STATIC ${LIBRARY_SOURCES} ${tutf8e_SOURCES} ${libutf_SOURCES} ${libbcrypt_SOURCES})
set_target_properties(modules PROPERTIES COMPILE_FLAGS "-fPIC ${MODULE_COMPILE_FLAGS}")

target_link_libraries(modules PUBLIC m)

set(COMMON_LIBRARIES modules compiled)

get_directory_property(CURRENT_LINK_DIRS LINK_DIRECTORIES)

list(LENGTH QUICKJS_MODULES MODULE_COUNT)
message(STATUS "Native modules: ${MODULE_COUNT} (${QUICKJS_MODULES})")

foreach(JS_MODULE ${QUICKJS_MODULES})
  file(GLOB MODULE_SOURCE quickjs-${JS_MODULE}.[ch])
  string(REGEX REPLACE "-" "_" QJSM_MODULE "${JS_MODULE}")

  # a native module that is built into qjsm is not installed as a .so as well (doc/qjsm-layout.md 4)
  if(NOT QJSM_INSTALL_BUILTIN_NATIVE AND ("${QJSM_MODULE}" IN_LIST BUILTINS_NATIVE OR "${QJSM_MODULE}" IN_LIST BUILTINS_NEEDED_NATIVE))
    set(${QJSM_MODULE}_NO_INSTALL ON)
  endif()

  make_module(${JS_MODULE} ${MODULE_SOURCE})

  list(APPEND QJSM_LIBS "${QJSM_MODULE}")
endforeach(JS_MODULE ${QUICKJS_MODULES})

##
## Native C module dependencies
##
module_depends(deep predicate pointer location)
module_depends(inspect pointer)
module_depends(json location)
module_depends(lexer location)
module_depends(blob stream)
module_depends(tree-walker pointer)
module_depends(xml pointer location inspect)
module_depends(child-process pointer) # XXX: why?

if(TARGET qjs-predicate)
  target_link_libraries(qjs-predicate PRIVATE m)
endif(TARGET qjs-predicate)

# qjs-serial/qjs-serial-static's libserialport linkage no longer needs a manual patch
# here: serial_LIBRARIES_shared/serial_LIBRARIES_static (set in the BUILD_LIBSERIALPORT
# block above) are picked up automatically by make_module()'s generic per-variant
# ${VNAME}_LIBRARIES_shared/_static handling.

set(json_SOURCES ${json_SOURCES} src/json/json.c)
set(mysql_SOURCES ${mysql_SOURCES} src/async-closure.c)
set(mysql_LIBS ${mysql_LIBS} modules)

if(LINK_STATIC)
  link_libraries(modules)
endif(LINK_STATIC)

qjsm_process_builtins()

qjsm_layout()

qjsm_add_executable()

##############################################################################
# 12. Install and summary
##############################################################################

file(
  GLOB
  INSTALL_SCRIPTS
  utilities/extract-structs.js
  utilities/jsonpp.js
  utilities/list-exports.js
  utilities/resolve-imports.js
  utilities/xml-shell.js
  utilities/nm-symbols.js
  utilities/remove-dead-functions.js
  utilities/deadwood.js
  utilities/source-scanner.js
  utilities/unused-functions.js
  utilities/runtime-diff.js
  utilities/extract-c.js
  utilities/highlight.js
  utilities/extract-comments.js
  utilities/parse-ast.js
  examples/html2md.js)

install(FILES ${INSTALL_SCRIPTS} DESTINATION bin PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ GROUP_EXECUTE
                                                             GROUP_READ WORLD_EXECUTE WORLD_READ)

symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/list-imports.js")
symlink(resolve-imports.js "${CMAKE_INSTALL_PREFIX}/bin/check-imports.js")
symlink(extract-structs.js "${CMAKE_INSTALL_PREFIX}/bin/extract-bindings.js")
list(LENGTH INSTALL_SCRIPTS INSTALL_SCRIPT_COUNT)
message(STATUS "Install: ${INSTALL_SCRIPT_COUNT} utility scripts to ${CMAKE_INSTALL_PREFIX}/bin")

file(GLOB_RECURSE JS_SCRIPTS examples/*.js utilities/*.js)

add_custom_target(js SOURCES ${JS_SCRIPTS})

# the summary of this configure: what will be built, with what, and what was generated
set(DISABLED_MODULES "")
foreach(MODULE archive magic mysql pgsql sqlite bcrypt serial gpio wasm)
  string(TOUPPER "${MODULE}" MODULE_UPPER)
  if(NOT MODULE_${MODULE_UPPER})
    list(APPEND DISABLED_MODULES "${MODULE} (MODULE_${MODULE_UPPER}=OFF)")
  elseif(NOT "${MODULE}" IN_LIST QUICKJS_MODULES AND NOT "${MODULE}" IN_LIST INSTALLED_NATIVE)
    list(APPEND DISABLED_MODULES "${MODULE} (library not found)")
  endif()
endforeach()

set(MODULE_KINDS "")
if(BUILD_SHARED)
  list(APPEND MODULE_KINDS "shared (qjs-<name>${CMAKE_SHARED_LIBRARY_SUFFIX})")
endif()
if(BUILD_STATIC)
  list(APPEND MODULE_KINDS "static (quickjs-<name>.a)")
endif()

if(OPENBSD_GLOB)
  set(GLOB_IMPLEMENTATION "bundled OpenBSD glob()")
else()
  set(GLOB_IMPLEMENTATION "system glob()")
endif()

if(DO_TESTS)
  set(TEST_SUMMARY "${TEST_COUNT} registered with ctest")
else()
  set(TEST_SUMMARY "disabled (DO_TESTS=OFF)")
endif()

wrap_list(MODULE_LINES 60 ${QUICKJS_MODULES})
wrap_list(NOT_BUILT_LINES 60 ${DISABLED_MODULES})

message("")
message_table("qjs-modules ${PROJECT_VERSION}"
  "system" "${CMAKE_SYSTEM_NAME} ${CMAKE_SYSTEM_PROCESSOR}"
  "compiler" "${CMAKE_C_COMPILER} (${CMAKE_C_COMPILER_ID} ${CMAKE_C_COMPILER_VERSION})"
  "build type" "${CMAKE_BUILD_TYPE}"
  "install prefix" "${CMAKE_INSTALL_PREFIX}"
  "module kinds" "${MODULE_KINDS}"
  "native modules (${MODULE_COUNT})" "${MODULE_LINES}"
  "not built" "${NOT_BUILT_LINES}"
  "wasm backend" "${WASM_BACKEND}"
  "glob()" "${GLOB_IMPLEMENTATION}"
  "tests" "${TEST_SUMMARY}"
  "generated" "${CMAKE_CURRENT_BINARY_DIR}/config.h")

qjsm_report()

message(STATUS "Next: build with 'cmake --build ${CMAKE_BINARY_DIR}', install with 'cmake --install ${CMAKE_BINARY_DIR}'")
