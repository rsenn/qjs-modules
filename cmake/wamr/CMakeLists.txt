# WAMR (third_party/wamr) as a static library `wamr_vmlib`: classic interpreter, no WASI.
# A subdirectory so runtime_lib.cmake's global add_definitions() stay out of the parent.
cmake_minimum_required(VERSION 3.14)

set(WAMR_BUILD_PLATFORM linux)
set(WAMR_BUILD_TARGET X86_64)
set(WAMR_BUILD_INTERP 1)
set(WAMR_BUILD_FAST_INTERP 0)
set(WAMR_BUILD_AOT 0)
set(WAMR_BUILD_JIT 0)
set(WAMR_BUILD_LIBC_BUILTIN 1)
set(WAMR_BUILD_LIBC_WASI 0)
set(WAMR_BUILD_MULTI_MODULE 1)
set(WAMR_BUILD_BULK_MEMORY 1)
set(WAMR_BUILD_REF_TYPES 1)
set(WAMR_ROOT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../third_party/wamr)

# runtime_lib.cmake prints its whole feature table; keep the one line that matters
if(NOT CMAKE_VERSION VERSION_LESS 3.17)
  set(CMAKE_MESSAGE_LOG_LEVEL WARNING)
endif()
include(${WAMR_ROOT_DIR}/build-scripts/runtime_lib.cmake)
unset(CMAKE_MESSAGE_LOG_LEVEL)
message(STATUS "WAMR: interpreter, no WASI")

add_library(wamr_vmlib STATIC ${WAMR_RUNTIME_LIB_SOURCE})
set_target_properties(wamr_vmlib PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(wamr_vmlib PUBLIC ${WAMR_ROOT_DIR}/core/iwasm/include)
target_link_libraries(wamr_vmlib PUBLIC m pthread dl)
