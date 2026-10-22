include(CheckFunctionExists)
include(CheckSymbolExists)
include(CheckLibraryExists)
include(CheckTypeSize)
include(CheckCCompilerFlag)

#
# check_cflag <FLAG> <OUTPUT_VAR> [VAR_NAME]
#
function(check_cflag FLAG OUTPUT_VAR)
  if(${ARGC} LESS 3)
    set(VAR_NAME CMAKE_C_FLAGS)
  else()
    set(VAR_NAME ${ARGV2})
  endif()

  message(CHECK_START "Checking compiler flag ${FLAG}")
  set(CMAKE_REQUIRED_QUIET TRUE)
  check_c_compiler_flag("${FLAG}" RESULT)
  set(CMAKE_REQUIRED_QUIET FALSE)

  if(RESULT)
    message(CHECK_PASS "supported")
    add_cflags("${FLAG}" "${VAR_NAME}")
  else()
    message(CHECK_FAIL "not supported")
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif(OUTPUT_VAR)
endfunction()

#
# check_flag <FLAG> <VAR> [FLAG-VARS...]
#
# If the C compiler accepts FLAG, cache the result in VAR (derived from FLAG if
# empty) and append FLAG to each of FLAG-VARS.
#
function(check_flag FLAG VAR)
  if(NOT VAR OR VAR STREQUAL "")
    string(TOUPPER "${FLAG}" TMP)
    string(REGEX REPLACE "[^0-9A-Za-z]" _ VAR "${TMP}")
  endif()

  set(CMAKE_REQUIRED_QUIET ON)
  check_c_compiler_flag("${FLAG}" "${VAR}")
  set(CMAKE_REQUIRED_QUIET OFF)

  if(${VAR})
    append_vars(${FLAG} ${ARGN})
    message(STATUS "Compiler flag ${FLAG}: supported")
  else()
    message(STATUS "Compiler flag ${FLAG}: not supported")
  endif()
endfunction()

#
# check_flags <FLAGS>
#
# Run check_flag for every flag in the list FLAGS, appending the supported
# ones to the variables in ARGN (a macro).
#
macro(check_flags FLAGS)
  foreach(FLAG ${FLAGS})
    check_flag(${FLAG} "" ${ARGN})
  endforeach()
endmacro()

# Append FLAG to CMAKE_EXE_LINKER_FLAGS if a test executable links with it. A
# macro, not a function, so the result reaches the calling scope.
#
# check_ldflags <FLAG> <VAR>
#
# FLAG     the linker flag, driver-style ("-Wl,--gc-sections") VAR      cache
# variable the probe result is stored in
# -----------------------------------------------------------------------
macro(check_ldflag FLAG VAR)
  set(CHECK_LDFLAG_SAVED "${CMAKE_EXE_LINKER_FLAGS}")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAG}")

  set(CMAKE_REQUIRED_QUIET TRUE)
  check_c_source_compiles("int main(void) { return 0; }" ${VAR})
  set(CMAKE_REQUIRED_QUIET FALSE)

  set(CMAKE_EXE_LINKER_FLAGS "${CHECK_LDFLAG_SAVED}")
  message(CHECK_START "Linker flag ${FLAG}")

  if(${VAR})
    message(CHECK_PASS "supported")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} ${FLAG}")
  else()
    message(CHECK_FAIL "not supported")
  endif()
endmacro()

#
# check_inline [OUTPUT-VAR]
#
function(check_inline)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR INLINE_KEYWORD)
  endif()

  unset(RESULT)
  unset(RESULT CACHE)

  foreach(KEYWORD "__inline__" "__inline" "inline")
    if(NOT RESULT)
      set(CMAKE_REQUIRED_DEFINITIONS "-DTESTKEYWORD=${KEYWORD}")
      check_c_source_compiles(
        "typedef int foo_t;\nstatic TESTKEYWORD foo_t static_foo(){return 0;}\nfoo_t foo(){return 0;}\nint main(int argc, char *argv[]){return 0;}\n"
        HAVE_${KEYWORD})

      if(HAVE_${KEYWORD})
        set(RESULT "${KEYWORD}")
      endif()
    endif()
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_compile <RESULT_VAR> <SOURCE> [DESCRIPTION] [NAME VALUE]...
#
# DESCRIPTION is the log text, "Checking <DESCRIPTION>"; it defaults to RESULT_VAR.
#
function(check_compile RESULT_VAR SOURCE)
  set(DESC "${ARGV2}")
  if(${ARGC} GREATER 2)
    list(REMOVE_AT ARGN 0)
  endif()
  assign_named_items(${ARGN})

  string(RANDOM LENGTH 6 ALPHABET "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" C_NAME)
  string(REPLACE SUPPORT_ "" NAME "${RESULT_VAR}")
  string(REPLACE _ - NAME "${NAME}")
  string(TOLOWER "${NAME}" C_NAME)
  set(C_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/try-${C_NAME}.c")
  string(REPLACE "\\" "\\\\" SOURCE "${SOURCE}")
  file(WRITE "${C_SOURCE}" "${SOURCE}")

  if(NOT DESC)
    string(REGEX REPLACE "^(have|support)-" "" DESC "${C_NAME}")
    string(REPLACE - " " DESC "${DESC}")
  endif()

  message(CHECK_START "Checking ${DESC}")

  try_compile(
    COMPILE_RESULT "${CMAKE_CURRENT_BINARY_DIR}" "${C_SOURCE}" OUTPUT_VARIABLE "OUTPUT" CMAKE_FLAGS "${CMAKE_FLAGS}"
    COMPILE_DEFINITIONS "${COMPILE_DEFINITIONS}" LINK_OPTIONS "${LINK_OPTIONS}" LINK_LIBRARIES "${LINK_LIBRARIES}")

  file(REMOVE "${C_SOURCE}")

  if(COMPILE_RESULT)
    message(CHECK_PASS "yes")
  else()
    set(COMPILE_LOG "${CMAKE_CURRENT_BINARY_DIR}/compile-${C_NAME}.log")
    relative_paths(COMPILE_LOG "${CMAKE_CURRENT_SOURCE_DIR}" "${COMPILE_LOG}")

    message(CHECK_FAIL "no (see ${COMPILE_LOG})")
    file(WRITE "${COMPILE_LOG}" "${OUTPUT}")
    string(REPLACE "\n" ";" OUTPUT "${OUTPUT}")
    list(FILTER OUTPUT INCLUDE REGEX "error")
  endif()

  if(RESULT_VAR)
    set("${RESULT_VAR}" "${COMPILE_RESULT}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_run <RESULT_VAR> <SOURCE>
#
macro(check_run RESULT_VAR SOURCE)
  string(RANDOM LENGTH 6 ALPHABET "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" C_NAME)
  string(REPLACE SUPPORT_ "" NAME "${RESULT_VAR}")
  string(REPLACE _ - NAME "${NAME}")
  string(TOLOWER "${NAME}" C_NAME)
  set(C_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/try-${C_NAME}.c")
  string(REPLACE "\\" "\\\\" SOURCE "${SOURCE}")
  file(WRITE "${C_SOURCE}" "${SOURCE}")

  string(REGEX REPLACE "^(have|support)-" "" DESC "${C_NAME}")
  string(REPLACE - " " DESC "${DESC}")
  message(CHECK_START "Checking ${DESC} (run)")
  try_run(RUN_RESULT COMPILE_RESULT "${CMAKE_CURRENT_BINARY_DIR}" "${C_SOURCE}"
          COMPILE_OUTPUT_VARIABLE "OUTPUT" LINK_LIBRARIES "${ARGN}")
  file(REMOVE "${C_SOURCE}")

  if(COMPILE_RESULT AND RUN_RESULT)
    message(CHECK_PASS "yes")
  else()
    set(COMPILE_LOG "${CMAKE_CURRENT_BINARY_DIR}/run-${C_NAME}.log")
    message(CHECK_FAIL "no (see ${COMPILE_LOG})")
    file(WRITE "${COMPILE_LOG}" "${OUTPUT}")
    string(REPLACE "\n" ";" OUTPUT "${OUTPUT}")
    list(FILTER OUTPUT INCLUDE REGEX "error")
  endif()

  if(RESULT_VAR)
    set("${RESULT_VAR}" "${COMPILE_RESULT}" CACHE BOOL "Support ${NAME}")
  endif()
endmacro()

#
# check_include_def <INCLUDE> [RESULT-VAR] [PREPROC_DEF]
#
macro(check_include_def INC)
  if(${ARGC} GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
    set(PREPROC_DEF "${ARGV2}")
  else()
    clean_name("${INC}" INC_D)
    string(TOUPPER "HAVE_${INC_D}" RESULT_VAR)
    string(TOUPPER "HAVE_${INC_D}" PREPROC_DEF)
  endif()

  check_include_file("${INC}" "${RESULT_VAR}")

  set("${RESULT_VAR}" "${${RESULT_VAR}}" CACHE INTERNAL "Define this if you have the '${INC}' header file")

  set_add(CHECKED_INCLUDES "${INC}")
endmacro()

#
# check_includes <INCLUDE-FILES...>
#
macro(check_includes)
  foreach(INC ${ARGN})
    clean_name("HAVE_${INC}" RESULT_VAR)
    check_include_def("${INC}" "${RESULT_VAR}")
  endforeach()
endmacro()

#
# check_includes_def <INCLUDE-FILES...>
#
macro(check_includes_def)
  foreach(INC ${ARGN})
    check_include_def("${INC}")
  endforeach()
endmacro()

#
# check_include_cxx_def <INCLUDE> [RESULT-VARIABLE] [PREPROCESSOR-DEFINITION]
#
# Check for the C++ header INCLUDE; on success cache RESULT-VARIABLE and add
# HAVE_<INCLUDE> for config.h (a macro).
#
macro(check_include_cxx_def INC)
  if(ARGC GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
    set(PREPROC_DEF "${ARGV2}")
  else(ARGC GREATER_EQUAL 2)
    clean_name("${INC}" INC_D)
    string(TOUPPER "HAVE_${INC_D}" RESULT_VAR)
    string(TOUPPER "HAVE_${INC_D}" PREPROC_DEF)
  endif(ARGC GREATER_EQUAL 2)

  check_include_file_cxx("${INC}" "${RESULT_VAR}")
  if(${${RESULT_VAR}})
    set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${INC}' header file")

  endif(${${RESULT_VAR}})
endmacro()

#
# check_function_def <FUNC> [RESULT_VAR] [PREPROC_DEF]
#
macro(check_function_def FUNC)
  if(${ARGC} GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
    set(PREPROC_DEF "${ARGV2}")
  else()
    string(TOUPPER "HAVE_${FUNC}" RESULT_VAR)
    string(TOUPPER "HAVE_${FUNC}" PREPROC_DEF)
  endif()
  check_function_exists("${FUNC}" "${RESULT_VAR}")

  set("${RESULT_VAR}" "${${RESULT_VAR}}" TRUE CACHE BOOL "Define this if you have the '${FUNC}' function")

endmacro()

#
# check_functions [FUNCTION-NAMES...]
#
macro(check_functions)
  foreach(FUNC ${ARGN})
    string(TOUPPER "HAVE_${FUNC}" RESULT_VAR)
    check_function_def("${FUNC}" "${RESULT_VAR}")
  endforeach()
endmacro()

#
# check_functions_def [FUNCTION-NAMES...]
#
macro(check_functions_def)
  foreach(FUNC ${ARGN})
    check_function_def("${FUNC}")
  endforeach()
endmacro()

#
# check_function_and_include <FUNCTION> <INCLUDE>
#
macro(check_function_and_include FUNC INC)
  clean_name("HAVE_${INC}" INC_RESULT)
  clean_name("HAVE_${FUNC}" FUNC_RESULT)
  check_include_def("${INC}" "${INC_RESULT}")

  if(${${INC_RESULT}})
    check_function_def("${FUNC}" "${FUNC_RESULT}")
  endif()
endmacro()

#
# check_external <NAME> <LIBS> <LINKER-FLAGS> <OUTPUT-VARIABLE>
#
# Check that a program using the external function NAME compiles and links
# against LIBS, storing the result in OUTPUT-VARIABLE.
#
function(check_external NAME LIBS LDFLAGS OUTPUT_VAR)
  try_code("test-${NAME}.c" "\n  extern int ${NAME}(void);\n  int main() {\n    ${NAME}();\n    return 0;\n  }\n  "
           "${OUTPUT_VAR}" OUT "${LIBS}" "${LDFLAGS}")
endfunction()

#
# check_source_definitions [TYPES...]
#
macro(check_source_definitions)
  set(SOURCE_DEFINITIONS)

  if(${ARGC} GREATER_EQUAL 1)
    set(SOURCE_TYPES ${ARGN})
  else()
    set(SOURCE_TYPES "ATFILE;GNU;LARGEFILE;LARGE_FILE;LARGEFILE64;POSIX;POSIX_C;XOPEN;XOPEN_EXTENDED")
  endif()

  foreach(TYPE ${SOURCE_TYPES})
    set_add(SOURCE_DEFINITIONS "-D${TYPE}_SOURCE")
  endforeach()

  list(APPEND CMAKE_REQUIRED_DEFINITIONS ${SOURCE_DEFINITIONS})
endmacro()

#
# check_sys_siglist_declaration
#
function(check_sys_siglist_declaration)
  if(${ARGC} LESS 1)
    set(OUTPUT_VAR HAVE_SYS_SIGLIST_DECLARATION)
  else()
    set(OUTPUT_VAR "${ARGV0}")
  endif()

  check_symbol_exists(sys_siglist "signal.h" SYM_EXISTS)

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${SYM_EXISTS}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_warn_flag <FLAG> <RESULT_VAR>
#
# Logs "Checking warning flag <FLAG>" and appends FLAG to WERROR_FLAG when the compiler takes it.
#
macro(check_warn_flag FLAG RESULT_VAR)
  message(CHECK_START "Checking warning flag ${FLAG}")

  set(CMAKE_REQUIRED_QUIET TRUE)
  check_c_compiler_flag("${FLAG}" ${RESULT_VAR})
  set(CMAKE_REQUIRED_QUIET FALSE)

  if(${RESULT_VAR})
    message(CHECK_PASS "supported")
    set(WERROR_FLAG "${WERROR_FLAG} ${FLAG}")
  else()
    message(CHECK_FAIL "not supported")
  endif()
endmacro()

#
# check_no_unused_warn_flags
#
macro(check_no_unused_warn_flags)
  check_warn_flag("-Wno-unused-variable" WARN_NO_UNUSED_VARIABLE)
  check_warn_flag("-Wno-unused-function" WARN_NO_UNUSED_FUNCTION)
  check_warn_flag("-Wno-error=unused-but-set-variable" WARN_NO_ERROR_UNUSED_BUT_SET_VARIABLE)
endmacro()

# MinSizeRel means bytes over everything else: every flag below is probed before
# use, and each one trades speed, hardening or diagnostics for size.
#
# check_falign_flags <FLAG-VAR>
#
#   -f*-unwind-tables    .eh_frame is 15% of an untuned binary; nothing unwinds
#   -fno-jump-tables     switch tables become compare chains -f*-sections only pays off together with --gc-sections
#
macro(check_fno_optim_flags FLAG_VAR)
  set(FLAGS "-fno-asynchronous-unwind-tables" "-fno-unwind-tables" "-fno-stack-protector" "-fno-jump-tables" "-fno-plt"
            "-fno-ident" "-fmerge-all-constants" "-ffunction-sections" "-fdata-sections")

  foreach(FLAG ${FLAGS})
    string(MAKE_C_IDENTIFIER "F${FLAG}" FLAG_VAR)
    check_c_compiler_flag("${FLAG}" ${FLAG_VAR})
    if(${FLAG_VAR})
      add_cflags("${FLAG}")
    endif()
  endforeach()

  foreach(FLAG "-Wl,--gc-sections" "-Wl,--as-needed" "-Wl,--build-id=none" "-Wl,-z,norelro" "-Wl,-z,noseparate-code"
               "-Wl,--hash-style=gnu")
    string(MAKE_C_IDENTIFIER "LD${FLAG}" FLAG_VAR)
    check_ldflag("${FLAG}" ${FLAG_VAR})
  endforeach()
endmacro()

#
# check_windows [OUTPUT-VAR]
#
function(check_windows_native)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR WINDOWS_NATIVE)
  endif()

  if((WIN32 OR WIN64 OR MSVC OR MINGW OR WINDOWS) AND NOT CYGWIN AND NOT CMAKE_SYSTEM_NAME MATCHES "MSYS"
     AND NOT CMAKE_C_COMPILER MATCHES "msys")
    set(RESULT TRUE)
  else()
    set(RESULT FALSE)
  endif()

  set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# check_winsock2 [OUTPUT-VAR] [LIBRARY-VAR]
#
function(check_winsock2)
  if(${ARGC} LESS 1)
    set(OUTPUT_VAR HAVE_WINSOCK2)
  else()
    set(OUTPUT_VAR "${ARGV0}")
  endif()

  if(${ARGC} LESS 2)
    set(LIBRARY_VAR WINSOCK2_LIBRARY)
  else()
    set(LIBRARY_VAR "${ARGV1}")
  endif()

  check_windows_native(WINDOWS)

  if(WINDOWS)
    # List of Winsock libraries to check in order of preference
    foreach(lib ws2_32 wsock32 mswsock)
      set(CMAKE_REQUIRED_LIBRARIES ${lib})
      check_symbol_exists(socket "winsock2.h" HAVE_WINSOCK)
      unset(CMAKE_REQUIRED_LIBRARIES)

      if(HAVE_WINSOCK)
        set(LIBRARY ${lib})
        break()
      endif()
    endforeach()
  endif()

  if(LIBRARY_VAR)
    set("${LIBRARY_VAR}" "${LIBRARY}" PARENT_SCOPE)
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${HAVE_WS2_32}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_socket_library [RESULT-VAR]
#
function(check_socket_library)
  if(${ARGC} GREATER_EQUAL 1)
    set(RESULT_VAR "${ARGV0}")
  else()
    set(RESULT_VAR SOCKET_LIBRARY)
  endif()

  check_windows_native(WINDOWS)

  if(WINDOWS)
    check_winsock2(HAVE_WINSOCK RESULT)
  else()
    check_library_exists(socket "connect" /usr/lib HAVE_LIBSOCKET)

    if(HAVE_LIBSOCKET)
      set(RESULT "socket")
    else()
      set(RESULT "")
    endif()
  endif()

  set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# check_lowfat [OUTPUT-VAR]
#
function(check_lowfat)
  if(${ARGC} GREATER_EQUAL 1)
    set(RESULT_VAR "${ARGV0}")
  else()
    set(RESULT_VAR HAVE_LIBOWFAT)
  endif()

  if(${ARGC} GREATER_EQUAL 2)
    set(LIB_VAR "${ARGV1}")
  else()
    set(LIB_VAR LIBOWFAT_LIBRARY)
  endif()

  check_library_exists(owfat_debug buffer_init /usr/lib64 RESULT)
  if(RESULT)
    set(LIB owfat_debug)
  endif()

  if(NOT RESULT)
    check_library_exists(owfat buffer_init /usr/lib64 RESULT)

    if(RESULT)
      set(LIB owfat)
    endif()
  endif()

  set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
  set("${LIB_VAR}" "${LIB}" PARENT_SCOPE)
endfunction()

#
# check_crosscompiling
#
macro(check_crosscompiling)
  if(CMAKE_CROSSCOMPILING)
    message(STATUS "Cross compiling")
    message(STATUS "Host system name: ${CMAKE_HOST_SYSTEM_NAME}")
    message(STATUS "System name: ${CMAKE_SYSTEM_NAME}")
  endif()
endmacro()

#
# check_pointer_size [OUTPUT-VAR]
#
function(check_pointer_size)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR POINTER_SIZE)
  endif()

  set(CMAKE_REQUIRED_QUIET TRUE)
  message(CHECK_START "Checking for pointer size")
  check_type_size("void*" SIZEOF_POINTER)
  set(CMAKE_REQUIRED_QUIET FALSE)

  if(NOT SIZEOF_POINTER)
    foreach(n 4 8 2)
      check_c_source_compiles("typedef char t[sizeof(void*) == ${n} ? 1 : -1]; int main(void) { return 0; }"
                              POINTER_SIZE_IS_${n})

      if(POINTER_SIZE_IS_${n})
        set(SIZEOF_POINTER ${n})
        break()
      endif()
    endforeach()
  endif()

  if(NOT SIZEOF_POINTER STREQUAL "")
    math(EXPR POINTER_BITS "${SIZEOF_POINTER} * 8")
    message(CHECK_PASS "${POINTER_BITS}bit")
    set(POINTER_SIZE "${SIZEOF_POINTER}")
  else()
    message(CHECK_FAIL "failed")
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${POINTER_SIZE}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_type_exists <TYPE> [RESULT-VAR] [INCLUDES]
#
function(check_type_exists TYPE RESULT_VAR)
  if(NOT RESULT_VAR AND "${RESULT_VAR}" STREQUAL "")
    clean_name("HAVE_${TYPE}" RESULT_VAR)
  endif()

  assign_properties(
    "ADD_HEADERS;COMPILE_DEFINITIONS;COMPILE_FLAGS;LINK_DIRECTORIES;LINK_FLAGS;LINK_LIBRARIES;LINK_OPTIONS" ${ARGN})

  set(SRC "typedef ${TYPE} type_t;\nint main() {\n  type_t var;\n  return 0;\n}\n")

  foreach(INC ${ADD_HEADERS})
    set(SRC "#include <${INC}>\n${SRC}")
  endforeach()

  check_compile(RESULT "${SRC}" "for type ${TYPE}")

  set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# check_type_exists_def <TYPE> [RESULT-VAR] [DEF-NAME] [ADDITIONAL-INCLUDES]
#
function(check_type_exists_def TYPE)
  if(${ARGC} GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
  else()
    clean_name("HAVE_${TYPE}" RESULT_VAR)
  endif()

  list(SUBLIST ARGN 2 -1 REST)

  check_type_exists("${TYPE}" RESULT ${REST})

  set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# check_types [TYPES....]
#
macro(check_types)
  foreach(TYPE ${ARGN})
    clean_name("HAVE_${TYPE}_TYPE" RESULT_VAR)

    check_type_exists_def("${TYPE}" "${RESULT_VAR}")
  endforeach()
endmacro()

#
# check_posix_types
#
macro(check_posix_types)
  check_type_exists_def(ssize_t HAVE_SSIZE_T _SSIZE_T_)
  check_type_exists(sigset_t HAVE_SIGSET_T)
  check_type_exists(pid_t HAVE_PID_T ADD_HEADERS "sys/types.h")
  check_type_exists(uid_t HAVE_UID_T)
endmacro()

#
# check_alloca [OUTPUT-VAR]
#
function(check_alloca)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR HAVE_ALLOCA)
  endif()

  set(CMAKE_REQUIRED_QUIET TRUE)

  if(NOT DEFINED HAVE_ALLOCA_H)
    check_include_def(alloca.h HAVE_ALLOCA_H)
    set(HAVE_ALLOCA_H "${HAVE_ALLOCA_H}" PARENT_SCOPE)
  endif()

  if(HAVE_ALLOCA_H)
    set_add(CMAKE_REQUIRED_INCLUDES alloca.h)
    check_symbol_exists(alloca alloca.h HAVE_ALLOCA_SYMBOL)
  endif()

  check_compile(
    HAVE_ALLOCA_ALLOCA_H
    "#include <stdlib.h>\n#include <alloca.h>\n\n\nint main() {\n  char* c=alloca(23);\n  (void)c;\n  return 0;\n}"
    "alloca() in <alloca.h>")

  if(NOT HAVE_ALLOCA_ALLOCA_H)
    check_compile(
      HAVE_ALLOCA_MALLOC_H
      "#include <stdlib.h>\n#include <malloc.h>\n\n\nint main() {\n  char* c=alloca(23);\n  (void)c;\n  return 0;\n}"
      "alloca() in <malloc.h>")
  endif()

  if(HAVE_ALLOCA_ALLOCA_H OR HAVE_ALLOCA_MALLOC_H)
    set(RESULT TRUE)

    if(HAVE_ALLOCA_MALLOC_H)
      set(ALLOCA_HEADER "malloc.h" CACHE STRING "Header for alloca()")
    else()
      set(ALLOCA_HEADER "alloca.h" CACHE STRING "Header for alloca()")
    endif()

    set(ALLOCA_HEADER "${ALLOCA_HEADER}" PARENT_SCOPE)
  else()
    set(RESULT FALSE)
  endif()

  set(CMAKE_REQUIRED_QUIET FALSE)

  set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
endfunction()

#
# check_memory_mapping
#
macro(check_memory_mapping)
  check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
  if(HAVE_SYS_MMAN_H)
    set_add(CMAKE_EXTRA_INCLUDE_FILES sys/mman.h)
  endif()

  check_function_exists(mmap HAVE_MMAP_FUNC)
  check_function_exists(munmap HAVE_MUNMAP_FUNC)

  if(HAVE_SYS_MMAN_H AND HAVE_MMAP_FUNC AND HAVE_MUNMAP_FUNC)
    set(HAVE_MMAP_SUPPORT TRUE)
  elseif(WIN32 OR WIN64 OR MINGW OR WINDOWS OR CMAKE_SYSTEM_NAME STREQUAL "WASI") # WASI: -lwasi-emulated-mman
    set(HAVE_MMAP_SUPPORT TRUE)
  endif()

  if(USE_MMAP AND NOT HAVE_MMAP_SUPPORT)
    message(WARNING "USE_MMAP requested, but mmap()/munmap() aren't available on this platform -- disabling")
    set(USE_MMAP OFF CACHE BOOL "Use mmap() for memory-mapped file I/O" FORCE)
  endif()

  set(HAVE_MMAP "${HAVE_MMAP_SUPPORT}" CACHE BOOL "Have the mmap() function")
endmacro()

#
# check_emscripten
#
macro(check_emscripten)
  if(COMPILER_NAME MATCHES "em.*")
    set(EMSCRIPTEN TRUE)
    set(EMSCRIPTEN_EXE_SUFFIX "html")
  endif()

  if(CMAKE_SYSTEM_NAME STREQUAL "WASI")
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src/wasi)
    add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/src/wasi/wasi_compat.h)

    add_compile_options(-mllvm -wasm-enable-sjlj -mexception-handling)
    add_link_options(-mllvm -wasm-enable-sjlj -lsetjmp)
    add_compile_definitions(_WASI_EMULATED_SIGNAL _WASI_EMULATED_GETPID _WASI_EMULATED_PROCESS_CLOCKS
                            _WASI_EMULATED_MMAN)
    add_link_options(-lwasi-emulated-signal -lwasi-emulated-getpid -lwasi-emulated-process-clocks -lwasi-emulated-mman)
  endif()
endmacro()

#
# check_libmath [OUTPUT-VAR]
#
function(check_libmath)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR MATH_LIBRARY)
  endif()

  set(CMAKE_REQUIRED_QUIET TRUE)
  check_library_exists(m pow "" HAVE_LIBM)
  set(CMAKE_REQUIRED_QUIET FALSE)

  if(HAVE_LIBM)
    set(MATH_LIBRARY m)
  else()
    set(MATH_LIBRARY "")
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${MATH_LIBRARY}" PARENT_SCOPE)
  endif()
endfunction()

# check_function_exists(fork ...) alone isn't trustworthy on either of the two
# platform families below; both require the result to be hardcoded rather than
# relying on the check:
#
# Native Windows (WIN32/WIN64/MSVC/MINGW/WINDOWS, excluding Cygwin and MSYS,
# which provide a real POSIX fork() via their own C libraries) lacks a
# libc-provided fork() for the check to find. However, lib/unix/fork.c supplies
# one via RtlCloneUserProcess that job_fork() already links against. Therefore,
# the correct result here is TRUE despite what the automated check determines.
#
# Emscripten/WASI yields the opposite false result. Musl's libc ships a real,
# linkable fork() symbol—causing the check to find it (as seen in Looking for
# fork - found from emcmake cmake)—but the underlying syscall lacks
# implementation in a single-threaded WASM module and fails at runtime (ENOSYS).
# Thus, the correct result here is FALSE.
#
# Detection Workarounds: This is detected similarly to CMakeLists.txt's own
# EMSCRIPTEN variable by matching the compiler basename (em*) rather than
# relying on CMAKE_SYSTEM_NAME. This file is include()'d before that variable is
# set, and cfg-emscripten's toolchain file (cfg-cmake.sh's cfg-emscripten) never
# resolves due to a syntax bug (${EMSCRIPTEN:=dirname $(which emcc)} is missing
# parentheses around dirname, evaluating to the literal string "dirname
# /path/to/emcc" instead of executing). Consequently, CMAKE_SYSTEM_NAME isn't
# set to "Emscripten" in practice.
#
# WASI and MSYS Exceptions: CMAKE_SYSTEM_NAME=WASI (cfg-wasi) avoids this issue
# because it is passed directly as a CMake argument rather than depending on a
# toolchain file's existence check, allowing normal evaluation. Conversely, MSYS
# shares the same CMAKE_SYSTEM_NAME bug as Emscripten because its external
# toolchain file (msys64.cmake / msys32.cmake) is outside repository control;
# therefore, it must be cross-checked against the compiler path
# (x86_64-pc-msys-gcc / i686-pc-msys-gcc) as well.
#
# check_fork_function
#
function(check_fork_function)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR HAVE_FORK)
  endif()

  string(REGEX REPLACE ".*/" "" HAVE_FORK_COMPILER_NAME "${CMAKE_C_COMPILER}")

  check_windows_native(WIN_NATIVE)
  if(WIN_NATIVE)
    set(HAVE_FORK TRUE)
  elseif(HAVE_FORK_COMPILER_NAME MATCHES "^em" OR CMAKE_SYSTEM_NAME STREQUAL "WASI")
    set(HAVE_FORK FALSE)
  else()
    check_function_exists(fork HAVE_FORK)
  endif()
  unset(HAVE_FORK_COMPILER_NAME)

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${HAVE_FORK}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_termios_winsize
#
macro(check_termios_winsize)
  check_includes(termios.h sys/ioctl.h)

  if(HAVE_TERMIOS_H AND HAVE_SYS_IOCTL_H)
    set(GET_COLUMNS
        "#include <unistd.h>\n#include <fcntl.h>\n#include <termios.h>\n#include <sys/ioctl.h>\n#include <stdio.h>\n\nint\nmain() {\n\tstruct winsize sz;\n\tint fd = isatty(0) ? dup(0) : open(\"/dev/tty\", O_RDWR);\n\n\tif(!isatty(fd)) {\n\t\tfputs(\"not a tty\\n\", stderr);\n\t\tfflush(stderr);\n\t\treturn 1;\n\t}\n\n\tif(ioctl(fd, TIOCGWINSZ, &sz) == -1) {\n\t\tperror(\"ioctl\");\n\t\treturn 1;\n\t}\n\n\tclose(fd);\n\n\tprintf(\"%u\\n\", sz.ws_col);\n\treturn 0;\n}\n"
    )
    check_compile(HAVE_WINSIZE "${GET_COLUMNS}" "TIOCGWINSZ")
  endif()
endmacro()

#
# check_pw_functions
#
macro(check_pw_functions)
  check_includes(pwd.h grp.h)
  check_functions(getpwuid_r getpwuid getgrgid_r getgrgid)
endmacro()

#
# check_sig_functions
#
macro(check_sig_functions)
  check_function_exists(sigprocmask HAVE_SIGPROCMASK)

  if(NOT HAVE_SIGPROCMASK)
    check_functions(sigblock sigsetmask sigpause)
  endif()

  check_functions(sigaction setpgid)
endmacro()

#
# check_optimize_size_flags [OUTPUT-VAR]
#
function(check_optimize_size_flags)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR CMAKE_C_FLAGS_MINSIZEREL)
  endif()

  string(REGEX REPLACE "-O[1-9]" "-Os" FLAGS "${${OUTPUT_VAR}}")

  if(NOT "${FLAGS}" STREQUAL "${${OUTPUT_VAR}}")
    if(OUTPUT_VAR)
      set("${OUTPUT_VAR}" "${FLAGS}" PARENT_SCOPE)
    endif()
  endif()
endfunction()

#
# check_falign_flags [OUTPUT-VAR]
#
function(check_falign_flags)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR CMAKE_C_FLAGS_MINSIZEREL)
  endif()

  set(FLAGS "")

  check_cflag("-falign-functions=1" F_ALIGN_FUNCTIONS FLAGS)
  check_cflag("-falign-jumps=1" F_ALIGN_JUMPS FLAGS)
  check_cflag("-falign-labels=1" F_ALIGN_LABELS FLAGS)
  check_cflag("-falign-loops=1" F_ALIGN_LOOPS FLAGS)

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${FLAGS}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_no_warnings_flags [OUTPUT-VAR]
#
function(check_no_warnings_flags)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR CMAKE_C_FLAGS)
  endif()

  check_cflag("-w" F_NOWARN FLAGS)

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${FLAGS}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_debug_symbols_flags [OUTPUT-VAR]
#
function(check_debug_symbols_flags)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR CMAKE_C_FLAGS_DEBUG)
  endif()

  set(FLAGS "${${OUTPUT_VAR}}")

  check_cflag("-O0" F_OPT_NONE FLAGS)
  check_cflag("-ggdb" F_G_GDB FLAGS)

  if(NOT F_G_GDB)
    check_cflag("-g3" F_G3 FLAGS)

    if(NOT F_G3)
      check_cflag("-g" F_G FLAGS)
    endif()
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${FLAGS}" PARENT_SCOPE)
  endif()
endfunction()
