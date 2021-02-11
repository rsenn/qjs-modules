project(quickjs-modules C)

if(NOT QUICKJS_PREFIX)
  set(QUICKJS_PREFIX "${CMAKE_INSTALL_PREFIX}" CACHE PATH "QuickJS install directory")
endif(NOT QUICKJS_PREFIX)

include(CheckIncludeFile)

set(CMAKE_REQUIRED_INCLUDES "${QUICKJS_PREFIX}/include/quickjs")
message("CMAKE_REQUIRED_INCLUDES = ${CMAKE_REQUIRED_INCLUDES}")
check_include_file(quickjs.h HAVE_QUICKJS_H)

if(NOT HAVE_QUICKJS_H)
  message(FATAL_ERROR "QuickJS headers not found")
endif(NOT HAVE_QUICKJS_H)

add_library(mmap MODULE mmap.c)

set_target_properties(mmap PROPERTIES PREFIX "" INCLUDE_DIRECTORIES "${QUICKJS_PREFIX}/include/quickjs")
  target_compile_definitions(mmap PRIVATE -DJS_SHARED_LIBRARY)

install(TARGETS mmap DESTINATION lib/quickjs)