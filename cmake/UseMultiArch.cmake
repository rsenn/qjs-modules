if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)
endif()

if(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR lib CACHE STRING "Specify the output directory for libraries (default is lib)")
endif()

#
# get_system_name <HOST_SYSTEM_VAR> <TARGET_SYSTEM_VAR>
#
# Store the host and the target system triplet (cc -dumpmachine, or the MSVC
# architectures) in the two variables.
#
function(get_system_name HOST_SYSTEM_VAR TARGET_SYSTEM_VAR)
  if(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})
    set(HOST_SYSTEM $ENV{VSCMD_ARG_HOST_ARCH})
    set(TARGET_SYSTEM $ENV{VSCMD_ARG_TGT_ARCH})
  else(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})
    execute_process(COMMAND cc -dumpmachine OUTPUT_VARIABLE HOST_SYSTEM OUTPUT_STRIP_TRAILING_WHITESPACE)
    # a clang that gets its target from CMAKE_C_COMPILER_TARGET reports the host
    if(CMAKE_C_COMPILER_TARGET)
      set(TARGET_SYSTEM "${CMAKE_C_COMPILER_TARGET}")
    else()
      execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE TARGET_SYSTEM
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif()
  endif(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})

  set("${HOST_SYSTEM_VAR}" "${HOST_SYSTEM}" PARENT_SCOPE)
  set("${TARGET_SYSTEM_VAR}" "${TARGET_SYSTEM}" PARENT_SCOPE)
endfunction()

get_system_name(HOST_SYSTEM_NAME SYSTEM_NAME)

# Android (Termux) has no multiarch directories: plain lib/, whatever an earlier
# configure cached from the host
if(ANDROID OR CMAKE_SYSTEM_NAME STREQUAL "Android")
  set(CMAKE_INSTALL_LIBDIR lib)
  set(CMAKE_ARCH_LIBDIR lib CACHE STRING "Architecture specific libraries" FORCE)
endif()

if(NOT CMAKE_ARCH_LIBDIR)
  if(NOT SYSTEM_NAME)
    if(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})
      set(HOST_SYSTEM_NAME $ENV{VSCMD_ARG_HOST_ARCH})
      set(SYSTEM_NAME $ENV{VSCMD_ARG_TGT_ARCH})
    else(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})
      execute_process(COMMAND cc -dumpmachine OUTPUT_VARIABLE HOST_SYSTEM_NAME OUTPUT_STRIP_TRAILING_WHITESPACE)
      execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine OUTPUT_VARIABLE SYSTEM_NAME
                      OUTPUT_STRIP_TRAILING_WHITESPACE)
    endif(MSVC OR DEFINED ENV{VSCMD_ARG_TGT_ARCH})
  endif(NOT SYSTEM_NAME)

  if(CMAKE_CROSSCOMPILING)
    if(NOT "${HOST_SYSTEM_NAME}" STREQUAL "${SYSTEM_NAME}")
      string(REGEX REPLACE i686 i386 CMAKE_CROSS_ARCH "${SYSTEM_NAME}")
    endif()

    set(CMAKE_CROSS_ARCH "${CMAKE_CROSS_ARCH}" CACHE STRING "Cross compiling target")
  endif()

  set(THIS_SYSTEM "${SYSTEM_NAME}")
  string(REGEX REPLACE "-unknown-" "-" THIS_SYSTEM "${THIS_SYSTEM}")
  string(REGEX REPLACE android[0-9]* android THIS_SYSTEM "${THIS_SYSTEM}")

  if(THIS_SYSTEM AND NOT "${THIS_SYSTEM}" STREQUAL "")
    set(CMAKE_INSTALL_LIBDIR lib/${THIS_SYSTEM})
    set(CMAKE_ARCH_LIBDIR lib/${THIS_SYSTEM} CACHE STRING "Architecture specific libraries")
  endif(THIS_SYSTEM AND NOT "${THIS_SYSTEM}" STREQUAL "")
endif(NOT CMAKE_ARCH_LIBDIR)

message(STATUS "Architecture-specific library directory: ${CMAKE_ARCH_LIBDIR}")
