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
  if(BUILD_DEBUG)
    option(DEBUG_OUTPUT "Print debug output" ${DEBUG_OUTPUT_DEFAULT})

    if(DEBUG_OUTPUT)
      add_definitions(-DDEBUG_OUTPUT=1)
    endif()
  endif()
endmacro()