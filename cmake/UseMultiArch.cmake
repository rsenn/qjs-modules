if(UNIX AND NOT APPLE)
  include(GNUInstallDirs)

elseif(NOT DEFINED CMAKE_INSTALL_LIBDIR)
  set(CMAKE_INSTALL_LIBDIR ""
      CACHE PATH "Specify the output directory for libraries (default is lib)")
endif()

if(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "lib")
  execute_process(COMMAND cc -dumpmachine OUTPUT_VARIABLE HOST_SYSTEM_NAME
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  execute_process(COMMAND ${CMAKE_C_COMPILER} -dumpmachine
                  OUTPUT_VARIABLE SYSTEM_NAME OUTPUT_STRIP_TRAILING_WHITESPACE)

  if(NOT "${HOST_SYSTEM_NAME}" STREQUAL "${SYSTEM_NAME}")
    string(REGEX REPLACE i686 i386 CMAKE_CROSS_ARCH "${SYSTEM_NAME}")
  endif()

  set(CMAKE_CROSS_ARCH "${CMAKE_CROSS_ARCH}" CACHE STRING
                                                   "Cross compiling target")

  if(SYSTEM_NAME
     AND EXISTS
         "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${SYSTEM_NAME}")
    set(CMAKE_INSTALL_LIBDIR lib/${SYSTEM_NAME})
  endif(SYSTEM_NAME
        AND EXISTS
            "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}/${SYSTEM_NAME}")
endif(NOT CMAKE_INSTALL_LIBDIR OR "${CMAKE_INSTALL_LIBDIR}" STREQUAL "lib")
