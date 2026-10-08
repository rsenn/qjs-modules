include(CheckSymbolExists)
include(CheckTypeSize)

#
# check_cflag <FLAG> <OUTPUT_VAR> [VAR_NAME]
#
function(check_cflag FLAG OUTPUT_VAR)
  if(${ARGC} LESS 3)
    set(VAR_NAME CMAKE_C_FLAGS)
  else()
    set(VAR_NAME ${ARGV2})
  endif()

  message(CHECK_START "Compiler flag ${FLAG}")
   set(CMAKE_REQUIRED_QUIET TRUE)
  check_c_compiler_flag("${FLAG}" RESULT)
  set(CMAKE_REQUIRED_QUIET FALSE)

  #message_func("check_cflag" '${FLAG}' OUTPUT_VAR=${OUTPUT_VAR} VAR_NAME=${VAR_NAME} RESULT=${RESULT})

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
      check_c_source_compiles("typedef int foo_t;\nstatic TESTKEYWORD foo_t static_foo(){return 0;}\nfoo_t foo(){return 0;}\nint main(int argc, char *argv[]){return 0;}\n" HAVE_${KEYWORD})
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
# check_compile <RESULT_VAR> <SOURCE>
#
function(check_compile RESULT_VAR SOURCE)
  assign_named_items(${ARGN})

  string(RANDOM LENGTH 6 ALPHABET "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789" C_NAME)
  string(REPLACE SUPPORT_ "" NAME "${RESULT_VAR}")
  string(REPLACE _ - NAME "${NAME}")
  string(TOLOWER "${NAME}" C_NAME)
  set(C_SOURCE "${CMAKE_CURRENT_BINARY_DIR}/try-${C_NAME}.c")
  string(REPLACE "\\" "\\\\" SOURCE "${SOURCE}")
  file(WRITE "${C_SOURCE}" "${SOURCE}")
 
  message(CHECK_START "Trying to compile try-${C_NAME}.c")
  try_compile(COMPILE_RESULT "${CMAKE_CURRENT_BINARY_DIR}" "${C_SOURCE}" OUTPUT_VARIABLE "OUTPUT" 
    CMAKE_FLAGS "${CMAKE_FLAGS}"
    COMPILE_DEFINITIONS "${COMPILE_DEFINITIONS}"
    LINK_OPTIONS "${LINK_OPTIONS}"
    LINK_LIBRARIES "${LINK_LIBRARIES}"
  )
  file(REMOVE "${C_SOURCE}")
  if(COMPILE_RESULT)
    message(CHECK_PASS "ok")
  else()
    set(COMPILE_LOG "${CMAKE_CURRENT_BINARY_DIR}/compile-${C_NAME}.log")
    relative_paths(COMPILE_LOG "${CMAKE_CURRENT_SOURCE_DIR}" "${COMPILE_LOG}")

    message(CHECK_FAIL "fail: ${COMPILE_LOG}")
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
 
  message(CHECK_START "Trying to run try-${C_NAME}.c")
  try_run(RUN_RESULT COMPILE_RESULT "${CMAKE_CURRENT_BINARY_DIR}" "${C_SOURCE}" COMPILE_OUTPUT_VARIABLE "OUTPUT" LINK_LIBRARIES "${ARGN}")
  file(REMOVE "${C_SOURCE}")
  if(COMPILE_RESULT AND RUN_RESULT)
    message(CHECK_PASS "ok")
    # add_definitions(-D${RESULT_VAR})
  else()
    set(COMPILE_LOG "${CMAKE_CURRENT_BINARY_DIR}/run-${C_NAME}.log")
    message(CHECK_FAIL "fail: ${COMPILE_LOG}")
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
  if(${ARGC} LESS 3)
    clean_name("${INC}" INC_D)
  endif()
  if(${ARGC} GREATER_EQUAL 2)
    set(RESULT_VAR "${ARGV1}")
  else()
    string(TOUPPER "HAVE_${INC_D}" RESULT_VAR)
  endif()
  if(${ARGC} GREATER_EQUAL 3)
    set(PREPROC_DEF "${ARGV2}")
  else()
    string(TOUPPER "HAVE_${INC_D}" PREPROC_DEF)
  endif()
  check_include_file("${INC}" RESULT)
  if(RESULT_VAR)
    if(RESULT)
      set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${INC}' header file")
      if(NOT "${PREPROC_DEF}" STREQUAL "")
        var2define("${PREPROC_DEF}" 1)
      endif()
    endif()
  endif()
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
# clean_name <STRING> <OUTPUT-VAR>
#
function(clean_name STR OUTPUT_VAR)
  string(TOUPPER "${STR}" STR)
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" STR "${STR}")
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${STR}" PARENT_SCOPE)
  endif()
endfunction()

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
  if(RESULT_VAR)
    if(${${RESULT_VAR}})
      set("${RESULT_VAR}" TRUE CACHE INTERNAL "Define this if you have the '${INC}' header file")
      if(NOT "${PREPROC_DEF}" STREQUAL "")
        var2define("${PREPROC_DEF}" 1)
      endif()
    endif()
  endif()
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
# have_includes <OUTPUT-VAR> [INCLUDES...]
#
function(have_includes OUTPUT_VAR)
  set(LIST "")
  foreach(INC ${ARGN})
    clean_name("HAVE_${INC}" RESULT_VAR)
    if(${${RESULT_VAR}})
      list(APPEND LIST "${INC}")
    endif()
  endforeach()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${LIST}" PARENT_SCOPE)
  endif()
endfunction()

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
  if(RESULT_VAR)
    if(${${RESULT_VAR}})
      set("${RESULT_VAR}" TRUE CACHE BOOL "Define this if you have the '${FUNC}' function")
      if(NOT "${PREPROC_DEF}" STREQUAL "")
        add_definitions(-D${PREPROC_DEF})
      endif()
    endif()
  endif()
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
  check_include_def("${INC}" "${INC_RESULT}" "${INC_RESULT}")
  if(${${INC_RESULT}})
    check_function_def("${FUNC}" "${FUNC_RESULT}" "${FUNC_RESULT}")
  endif()
endmacro()

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
    set_add(SOURCE_DEFINITIONS -D_${TYPE}_SOURCE)
  endforeach()
  add_definitions(${SOURCE_DEFINITIONS})
endmacro()

#
# check_sys_siglist_declaration
#
function(check_sys_siglist_declaration)
  if(${ARGC} LESS 1)
    set(OUTPUT_VAR "HAVE_SYS_SIGLIST_DECLARATION")
  else()
    set(OUTPUT_VAR "${ARGV0}")
  endif()
  if(${ARGC} LESS 2)
    set(PREPROC_DEF "HAVE_SYS_SIGLIST_DECLARATION")
  else()
    set(PREPROC_DEF "${ARGV1}")
  endif()
 
  check_symbol_exists(sys_siglist "signal.h" SYM_EXISTS)
  if(SYM_EXISTS)
    add_definitions(-D${PREPROC_DEF})
  endif()
 
  check_c_compiler_flag("-Wall" WARN_ALL)
  if(WARN_ALL)
    set(WERROR_FLAG "${WERROR_FLAG} -Wall")
  endif()
 
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${SYM_EXISTS}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_no_unused_warn_flags
#
macro(check_no_unused_warn_flags)
  check_c_compiler_flag("-Wno-unused-variable" WARN_NO_UNUSED_VARIABLE)
  if(WARN_NO_UNUSED_VARIABLE)
    set(WERROR_FLAG "${WERROR_FLAG} -Wno-unused-variable")
  endif()
  check_c_compiler_flag("-Wno-unused-function" WARN_NO_UNUSED_FUNCTION)
  if(WARN_NO_UNUSED_FUNCTION)
    set(WERROR_FLAG "${WERROR_FLAG} -Wno-unused-function")
  endif()
  check_c_compiler_flag("-Wno-error=unused-but-set-variable" WARN_NO_UNUSED_FUNCTION)
  if(WARN_NO_UNUSED_FUNCTION)
    set(WERROR_FLAG "${WERROR_FLAG} -Wno-error=unused-but-set-variable")
  endif()
endmacro()

#
# check_falign_flags
#
macro(check_falign_flags)
  check_c_compiler_flag("-falign-functions=1" F_ALIGN_FUNCTIONS)
  check_c_compiler_flag("-falign-jumps=1" F_ALIGN_JUMPS)
  check_c_compiler_flag("-falign-labels=1" F_ALIGN_LABELS)
  check_c_compiler_flag("-falign-loops=1" F_ALIGN_LOOPS)
  
  if(F_ALIGN_COMMONS)
    add_cflags(-falign-commons=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_DOUBLE)
    add_cflags(-falign-double=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_FUNCTIONS)
    add_cflags(-falign-functions=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_JUMPS)
    add_cflags(-falign-jumps=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_LABELS)
    add_cflags(-falign-labels=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_LOOPS)
    add_cflags(-falign-loops=1 ${CMAKE_BUILD_TYPE})
  endif()
  if(F_ALIGN_STRINGOPS)
    add_cflags(-falign-stringops=1 ${CMAKE_BUILD_TYPE})
  endif()
endmacro()

# MinSizeRel means bytes over everything else: every flag below is probed before
# use, and each one trades speed, hardening or diagnostics for size.
#
# check_falign_flags
#
#   -f*-unwind-tables    .eh_frame is 15% of an untuned binary; nothing unwinds
#   -fno-jump-tables     switch tables become compare chains -f*-sections only pays off together with --gc-sections
#
macro(check_fno_optim_flags)
  set(FLAGS "-fno-asynchronous-unwind-tables" "-fno-unwind-tables" "-fno-stack-protector" "-fno-jump-tables" "-fno-plt" "-fno-ident" "-fmerge-all-constants" "-ffunction-sections" "-fdata-sections")

  foreach(FLAG ${FLAGS})
    string(MAKE_C_IDENTIFIER "F${FLAG}" FLAG_VAR)
    check_c_compiler_flag("${FLAG}" ${FLAG_VAR})
    if(${FLAG_VAR})
      add_cflags("${FLAG}")
    endif()
  endforeach()

  foreach(FLAG "-Wl,--gc-sections" "-Wl,--as-needed" "-Wl,--build-id=none" "-Wl,-z,norelro" "-Wl,-z,noseparate-code" "-Wl,--hash-style=gnu")
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

  if((WIN32 OR WIN64 OR MSVC OR MINGW OR WINDOWS) AND NOT CYGWIN AND NOT CMAKE_SYSTEM_NAME MATCHES "MSYS" AND NOT CMAKE_C_COMPILER MATCHES "msys")
    set(RESULT TRUE)
  else()
    set(RESULT FALSE)
  endif()

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
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
 
  check_windows_native(WIN_NATIVE)
  if(WIN_NATIVE)
    check_library_exists(ws2_32 gethostname /usr/lib HAVE_WS2_32)
    if(NOT HAVE_WS2_32)
      check_library_exists(wsock32 gethostname /usr/lib HAVE_WSOCK32)
    endif()
  endif()
  if(HAVE_WSOCK32)
    set(LIBRARY wsock32)
  endif()
  if(HAVE_WS2_32)
    set(LIBRARY ws2_32)
  endif()
 
  if(LIBRARY_VAR)
    set("${LIBRARY_VAR}" "${LIBRARY}" PARENT_SCOPE)
  endif()
 
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${HAVE_WS2_32}" PARENT_SCOPE)
  endif()
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
  
  if(RESULT_VAR)
    set("${RESULT_VAR}" "${RESULT}" PARENT_SCOPE)
  endif()
  
  if(LIB_VAR)
    set("${LIB_VAR}" "${LIB}" PARENT_SCOPE)
  endif()
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

  if(NOT SIZEOF_POINTER STREQUAL "")
    math(EXPR POINTER_BITS "${SIZEOF_POINTER} * 8")
    message(CHECK_PASS "${POINTER_BITS}bit")
    set(POINTER_SIZE "${SIZEOF_POINTER}")
    add_definitions(-DPOINTER_SIZE=${POINTER_SIZE})
  else()
    message(CHECK_FAIL "failed")
  endif()
  
  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${POINTER_SIZE}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_type_sizes
#
macro(check_type_sizes)
  set(CMAKE_REQUIRED_QUIET TRUE)
  check_type_size(ssize_t SIZEOF_SSIZE_T)
  if(NOT SIZEOF_SSIZE_T STREQUAL "")
    set(HAVE_SSIZE_T 1)
    add_definitions(-D_SSIZE_T_=1)
  endif()
  check_type_size(sigset_t SIZEOF_SIGSET_T)
  if(NOT SIZEOF_SIGSET_T STREQUAL "")
    set(HAVE_SIGSET_T 1)
  endif()
  check_type_size(pid_t SIZEOF_PID_T)
  if(NOT SIZEOF_PID_T STREQUAL "")
    set(HAVE_PID_T 1)
  endif()
  check_type_size(uid_t SIZEOF_UID_T)
  if(NOT SIZEOF_UID_T STREQUAL "")
    set(HAVE_UID_T 1)
  endif()
  set(CMAKE_REQUIRED_QUIET FALSE)
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
  if(HAVE_ALLOCA_H)
    set_add(CMAKE_REQUIRED_INCLUDES alloca.h)
    check_symbol_exists(alloca alloca.h HAVE_ALLOCA_SYMBOL)
  endif()  
  check_compile(HAVE_ALLOCA_ALLOCA_H "#include <stdlib.h>\n#include <alloca.h>\n\n\nint main() {\n  char* c=alloca(23);\n  (void)c;\n  return 0;\n}" )
  if(NOT HAVE_ALLOCA_ALLOCA_H)
    check_compile(HAVE_ALLOCA_MALLOC_H "#include <stdlib.h>\n#include <alloca.h>\n\n\nint main() {\n  char* c=alloca(23);\n  (void)c;\n  return 0;\n}" )
  endif()
  if(HAVE_ALLOCA_ALLOCA_H OR HAVE_ALLOCA_MALLOC_H)
    set(HAVE_ALLOCA TRUE)
    add_definitions(-DHAVE_ALLOCA)
    if(HAVE_ALLOCA_MALLOC_H)
      set(ALLOCA_HEADER "malloc.h" CACHE STRING "Header for alloca()")
    else()
      set(ALLOCA_HEADER "alloca.h" CACHE STRING "Header for alloca()")
    endif()
  endif()
  if(NOT HAVE_ALLOCA)
    set(ALLOCA_HEADER "")
  endif()
  set(CMAKE_REQUIRED_QUIET FALSE)

  if(OUTPUT_VAR)
    set("${OUTPUT_VAR}" "${HAVE_ALLOCA}" PARENT_SCOPE)
  endif()
endfunction()

#
# check_memory_mapping
#
macro(check_memory_mapping)
  check_include_file(sys/mman.h HAVE_SYS_MMAN_H)
  if(HAVE_SYS_MMAN_H)
    set(CMAKE_EXTRA_INCLUDE_FILES ${CMAKE_EXTRA_INCLUDE_FILES} sys/mman.h)
    set_add(CMAKE_REQUIRED_INCLUDES sys/mman.h)
  endif()

  check_function_exists(mmap HAVE_MMAP_FUNC)
  check_function_exists(munmap HAVE_MUNMAP)
  check_function_exists(mremap HAVE_MREMAP)

  # whether the platform can support memory-mapped file I/O at all -- either
  # POSIX mmap(2)/munmap(2) (sys/mman.h present and both functions found), or
  # Windows' CreateFileMapping/MapViewOfFile, which lib/mmap/ and lib/buffer/'s
  # WINDOWS_NATIVE branches implement mmap(2) in terms of. USE_MMAP (an
  # option(), see CMakeLists.txt) may only be ON when this is true; HAVE_MMAP
  # (the compiler define lib/mmap/ and its callers actually check) tracks
  # USE_MMAP's final, validated value, not raw platform capability.
  if(HAVE_SYS_MMAN_H AND HAVE_MMAP_FUNC AND HAVE_MUNMAP)
    set(HAVE_MMAP_SUPPORT TRUE)
  elseif(WIN32 OR WIN64 OR MINGW OR WINDOWS OR CMAKE_SYSTEM_NAME STREQUAL "WASI") # WASI: -lwasi-emulated-mman
    set(HAVE_MMAP_SUPPORT TRUE)
  endif()

  if(USE_MMAP AND NOT HAVE_MMAP_SUPPORT)
    message(WARNING "USE_MMAP requested, but mmap()/munmap() aren't available on this platform -- disabling")
    set(USE_MMAP OFF CACHE BOOL "Use mmap() for memory-mapped file I/O" FORCE)
  endif()

  set(HAVE_MMAP ${USE_MMAP} CACHE BOOL "Have the mmap() function")
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
    # wasi-libc lacks fork/wait/termios/pwd/sigaction; src/wasi/ stubs them
    include_directories(BEFORE ${CMAKE_CURRENT_SOURCE_DIR}/src/wasi)
    add_compile_options(-include ${CMAKE_CURRENT_SOURCE_DIR}/src/wasi/wasi_compat.h)

    # setjmp/longjmp (subshells, pipelines, break/return) need wasm exceptions
    add_compile_options(-mllvm -wasm-enable-sjlj -mexception-handling)
    add_link_options(-mllvm -wasm-enable-sjlj -lsetjmp)
    add_compile_definitions(_WASI_EMULATED_SIGNAL _WASI_EMULATED_GETPID _WASI_EMULATED_PROCESS_CLOCKS _WASI_EMULATED_MMAN)
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

macro(check_termios_winsize)
  check_includes_def(termios.h sys/ioctl.h)
  if(HAVE_TERMIOS_H AND HAVE_SYS_IOCTL_H)
    set(GET_COLUMNS "#include <unistd.h>\n#include <fcntl.h>\n#include <termios.h>\n#include <sys/ioctl.h>\n#include <stdio.h>\n\nint\nmain() {\n\tstruct winsize sz;\n\tint fd = isatty(0) ? dup(0) : open(\"/dev/tty\", O_RDWR);\n\n\tif(!isatty(fd)) {\n\t\tfputs(\"not a tty\\n\", stderr);\n\t\tfflush(stderr);\n\t\treturn 1;\n\t}\n\n\tif(ioctl(fd, TIOCGWINSZ, &sz) == -1) {\n\t\tperror(\"ioctl\");\n\t\treturn 1;\n\t}\n\n\tclose(fd);\n\n\tprintf(\"%u\\n\", sz.ws_col);\n\treturn 0;\n}\n" )
    check_compile(HAVE_WINSIZE "${GET_COLUMNS}")
    if(HAVE_WINSIZE)
      add_definitions(-DHAVE_WINSIZE=1)
    endif()
  endif()
endmacro()

#
# check_pw_functions
#
macro(check_pw_functions)
  check_include_file(pwd.h HAVE_PWD_H)
  check_include_file(grp.h HAVE_GRP_H)
  check_function_exists(getpwuid_r HAVE_GETPWUID_R)
  check_function_exists(getpwuid HAVE_GETPWUID)
  check_function_exists(getgrgid_r HAVE_GETGRGID_R)
  check_function_exists(getgrgid HAVE_GETGRGID)
endmacro()

#
# check_sig_functions
#
macro(check_sig_functions)
  check_function_exists(sigprocmask HAVE_SIGPROCMASK)

  if(NOT HAVE_SIGPROCMASK)
    check_function_exists(sigblock HAVE_SIGBLOCK)
    check_function_exists(sigsetmask HAVE_SIGSETMASK)
    check_function_exists(sigpause HAVE_SIGPAUSE)
  endif()

  check_function_exists(sigaction HAVE_SIGACTION)
  check_function_exists(setpgid HAVE_SETPGID)
endmacro()

#
# compiler_flags_optimize_size
#
macro(compiler_flags_optimize_size)
  string(REGEX REPLACE "-O[1-9]" "-Os" CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
  string(REGEX REPLACE "-O[1-9]" "-Os" CMAKE_C_FLAGS_MINSIZEREL "${CMAKE_C_FLAGS_MINSIZEREL}")
  string(REGEX REPLACE "-O[1-9]" "-Os" CMAKE_C_FLAGS_RELEASE "${CMAKE_C_FLAGS_RELEASE}")
  string(REGEX REPLACE "-O[1-9]" "-Os" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
  string(REGEX REPLACE "-O[1-9]" "-Os" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
endmacro()

#
# compiler_flags_debug [OUTPUT-VAR]
#
function(compiler_flags_debug)
  if(${ARGC} GREATER_EQUAL 1)
    set(OUTPUT_VAR "${ARGV0}")
  else()
    set(OUTPUT_VAR CMAKE_C_FLAGS_DEBUG)
  endif()

  #message_func("compiler_flags_debug" ${OUTPUT_VAR})

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

#
# option_debug_mode
#
macro(option_debug_mode)
  option(BUILD_DEBUG "Build in debug mode" OFF)

  if("${CMAKE_BUILD_TYPE}" MATCHES ".*Deb.*")
    set(BUILD_DEBUG TRUE)
  endif()

  if(BUILD_DEBUG)
    add_definitions(-D_DEBUG)
  else()
    add_definitions(-DNDEBUG=1)
  endif()

  if(BUILD_DEBUG)
    option(USE_EFENCE "Enable electric fence" OFF)
    check_library_exists(efence malloc /usr/lib HAVE_EFENCE)
  endif()

  if(HAVE_EFENCE)
    if(USE_EFENCE)
      set(ELECTRICFENCE_LIBRARY efence)
    endif()
  endif()
endmacro()

#
# option_link_static
#
macro(option_link_static)
  option(LINK_STATIC "Link executables statically" OFF)

  if(LINK_STATIC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")

    add_definitions(-DLINK_STATIC=1)

    set(ENABLE_SHARED OFF)
    set(BUILD_SHARED_LIBS FALSE)
  endif()
endmacro()

#
# option_link_time_optimization
#
macro(option_link_time_optimization)
  check_c_compiler_flag("-flto" F_LTO)

  if(F_LTO)
    option(ENABLE_LTO "Enable link-time optimization" OFF)
  endif()

  if(ENABLE_LTO)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -flto")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -flto")
  endif()
endmacro()
