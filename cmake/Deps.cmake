# Deps.cmake - library dependencies of a CMake project: where the toolchain keeps its
# libraries, a dependency tree, "use the installed copy or build one", and a summary.
#
# Nothing here knows about any particular project or library.
#
#   toolchain_triplet(OUT)  toolchain_prefix(OUT)  toolchain_includedir(OUT)
#   toolchain_libdir(OUT)   toolchain_pkgconfigdir(OUT)  toolchain_is_native(OUT)
#       what the active C compiler (CMAKE_C_COMPILER, CMAKE_C_FLAGS, CMAKE_SYSROOT) uses
#   toolchain_divert() / toolchain_restore()
#       point pkg-config and CMake's find_*() at that toolchain, and back
#
#   dep_declare(<name> [DEPENDS <dep>...] [PKGCONFIG <module>] [HEADERS <file>...]
#               [LIBRARIES <name>...] [FIND <cmd>] [BUILD <cmd>] [OPTION <var>]
#               [DESCRIPTION <text>])
#   dep_resolve(<name> SUFFIX <s> [PIC <bool>] [BINARY <dir>] [SOURCE <dir>])
#   dep_provide(<name> <suffix> SOURCE <system|built> [ORIGIN <text>] [PREFIX <dir>]
#               [INCLUDE_DIRS ...] [LIBRARIES ...] [LINK_FLAGS ...] [TARGETS ...])
#   dep_get(<name> <suffix> <FIELD> OUT)        one recorded field
#   dep_include_dirs / dep_libraries / dep_link_flags / dep_targets (<name> <suffix> OUT)
#                                               the field of the dep and of what it uses
#   dep_tree(<name> OUT)  dep_print_tree(<name>)  the declared tree
#   dep_summary([DEFER])                        one line per resolved dependency
#
# Vocabulary: a dependency is resolved once per SUFFIX (a variant of the build such as
# shared/static, with its own PIC setting). Resolving finds the installed copy, and only
# when there is none, or when the option BUILD_<NAME> is ON, resolves the DEPENDS first and
# runs the BUILD command. FIND and BUILD are commands called as
#   <FIND>  <name> <suffix>
#   <BUILD> <name> <suffix> <pic> <binary-dir> <source-dir>
# and report through dep_provide(). They run in a function scope: only what they hand to
# dep_provide() survives.

include_guard(GLOBAL)

# needs CMake 3.18 (cmake_language CALL); dep_summary(DEFER) 3.19

set(TOOLCHAIN_PREFIX "" CACHE PATH "Prefix of the toolchain's libraries (default: probed from the compiler)")
set(TOOLCHAIN_INCLUDEDIR "" CACHE STRING "Include directories of the toolchain (default: probed)")
set(TOOLCHAIN_LIBDIR "" CACHE PATH "Library directory of the toolchain (default: probed)")
set(TOOLCHAIN_PKGCONFIGDIR "" CACHE STRING "pkg-config directories of the toolchain (default: probed)")
set(TOOLCHAIN_DIVERT "AUTO" CACHE STRING "Point pkg-config and find_*() at the toolchain: AUTO (not for the host's own), ON, OFF")
set_property(CACHE TOOLCHAIN_DIVERT PROPERTY STRINGS AUTO ON OFF)
set(TOOLCHAIN_PKGCONFIG_PATH_MAP "" CACHE STRING "FROM=TO pairs pkgcfg rewrites in .pc paths while diverted (list)")
set(TOOLCHAIN_PKGCFG "" CACHE FILEPATH "c-utils pkgcfg used in place of pkg-config while diverted (default: found on PATH)")

set(DEPS_BUILD "REQUESTED" CACHE STRING
    "Which missing libraries may be built from source: REQUESTED (those a MODULE_<name>=ON or BUILD_<lib>=ON asks for), NONE, CMAKE (every missing one that has a CMake build) or ALL")
set_property(CACHE DEPS_BUILD PROPERTY STRINGS REQUESTED NONE CMAKE ALL)
set(DEPS_MISSING "FAIL" CACHE STRING
    "A missing library that is not built: FAIL the configure, or DISABLE the module that needs it (with a warning)")
set_property(CACHE DEPS_MISSING PROPERTY STRINGS FAIL DISABLE)

foreach(_DEPS_VAR DEPS_BUILD DEPS_MISSING)
  string(TOUPPER "${${_DEPS_VAR}}" _DEPS_VALUE)
  get_property(_DEPS_VALID CACHE ${_DEPS_VAR} PROPERTY STRINGS)
  if(NOT _DEPS_VALUE IN_LIST _DEPS_VALID)
    message(FATAL_ERROR "${_DEPS_VAR} must be one of ${_DEPS_VALID}, not '${${_DEPS_VAR}}'")
  endif()
  set(${_DEPS_VAR} "${_DEPS_VALUE}")
endforeach(_DEPS_VAR)

##############################################################################
# toolchain
##############################################################################

# _toolchain_probe(<compiler> <flags> <prefix> <includedirs> <libdir> <triplet> <musl>)
#
# Asks the compiler: -dumpmachine for the triplet, -print-search-dirs for the first library
# directory that holds a C library. musl-gcc reports glibc's paths, so it is recognised
# by name and given the <arch>-linux-musl layout its packages install to.
function(_toolchain_probe cc flags out_prefix out_includes out_libdir out_triplet out_musl)
  set(args ${flags})
  execute_process(COMMAND ${cc} ${args} -dumpmachine OUTPUT_VARIABLE triplet OUTPUT_STRIP_TRAILING_WHITESPACE
                  ERROR_QUIET)

  get_filename_component(ccname "${cc}" NAME)
  set(musl FALSE)
  if("${ccname}" MATCHES "musl" OR "${flags}" MATCHES "musl")
    set(musl TRUE)
  endif()

  set(prefix "")
  set(includes "")
  set(libdir "")

  if(musl)
    string(REGEX REPLACE "-.*" "" arch "${triplet}")
    if("${flags}" MATCHES "-m32")
      set(arch i386)
    endif()
    set(triplet "${arch}-linux-musl")
    set(prefix "/usr")
    set(libdir "/usr/lib/${triplet}")
    set(includes "/usr/include/${triplet}")
  else()
    execute_process(COMMAND ${cc} ${args} -print-search-dirs OUTPUT_VARIABLE dirs ERROR_QUIET)
    string(REGEX MATCH "libraries: =[^\n]*" dirs "${dirs}")
    string(REGEX REPLACE "^libraries: =" "" dirs "${dirs}")
    string(REPLACE ":" ";" dirs "${dirs}")

    foreach(dir ${dirs})
      get_filename_component(dir "${dir}" REALPATH)
      foreach(marker libc.a libc.so libmingw32.a libkernel32.a)
        if(NOT libdir AND EXISTS "${dir}/${marker}")
          set(libdir "${dir}")
        endif()
      endforeach(marker)
    endforeach(dir)

    # a multiarch libdir names the architecture, whatever -dumpmachine says (-m32)
    if(libdir MATCHES "^(.*)/lib/([^/]+)$")
      set(prefix "${CMAKE_MATCH_1}")
      set(triplet "${CMAKE_MATCH_2}")
    elseif(libdir MATCHES "^(.*)/lib(32|64)?$")
      set(prefix "${CMAKE_MATCH_1}")
    endif()

    if(prefix)
      set(includes "${prefix}/include")
      if(IS_DIRECTORY "${prefix}/include/${triplet}")
        list(APPEND includes "${prefix}/include/${triplet}")
      endif()
    endif()
  endif()

  set(${out_prefix} "${prefix}" PARENT_SCOPE)
  set(${out_includes} "${includes}" PARENT_SCOPE)
  set(${out_libdir} "${libdir}" PARENT_SCOPE)
  set(${out_triplet} "${triplet}" PARENT_SCOPE)
  set(${out_musl} "${musl}" PARENT_SCOPE)
endfunction(_toolchain_probe)

# Probes once, keeps the answer in global properties. The host's own libdir (plain cc, no
# flags) is probed too: a toolchain is native when it uses the same one.
function(_toolchain_init)
  get_property(ready GLOBAL PROPERTY TOOLCHAIN:READY)
  if(ready)
    return()
  endif()

  set(cc "${CMAKE_C_COMPILER}")
  if(NOT cc)
    set(cc cc)
  endif()

  set(flags "")
  if(CMAKE_C_COMPILER_ARG1)
    separate_arguments(arg1 UNIX_COMMAND "${CMAKE_C_COMPILER_ARG1}")
    list(APPEND flags ${arg1})
  endif()
  if(CMAKE_C_FLAGS)
    separate_arguments(cflags UNIX_COMMAND "${CMAKE_C_FLAGS}")
    list(APPEND flags ${cflags})
  endif()
  if(CMAKE_SYSROOT)
    list(APPEND flags "--sysroot=${CMAKE_SYSROOT}")
  endif()

  _toolchain_probe("${cc}" "${flags}" prefix includes libdir triplet musl)
  _toolchain_probe(cc "" host_prefix host_includes host_libdir host_triplet host_musl)

  if(TOOLCHAIN_PREFIX)
    set(prefix "${TOOLCHAIN_PREFIX}")
    set(includes "${prefix}/include")
    set(libdir "${prefix}/lib")
  endif()
  if(TOOLCHAIN_INCLUDEDIR)
    set(includes ${TOOLCHAIN_INCLUDEDIR})
  endif()
  if(TOOLCHAIN_LIBDIR)
    set(libdir "${TOOLCHAIN_LIBDIR}")
  endif()

  # musl shares its prefix with the host's glibc: only its own libdir may offer .pc files
  set(pkgconfig ${TOOLCHAIN_PKGCONFIGDIR})
  if(NOT pkgconfig)
    set(pcdirs "${libdir}/pkgconfig")
    if(NOT musl)
      list(APPEND pcdirs "${prefix}/lib/pkgconfig" "${prefix}/share/pkgconfig")
    endif()
    # the toolchain's own directory stays in even when it does not exist: an empty list
    # would leave PKG_CONFIG_LIBDIR empty and pkg-config falls back to the host's
    list(GET pcdirs 0 pkgconfig)
    foreach(dir ${pcdirs})
      if(IS_DIRECTORY "${dir}")
        list(APPEND pkgconfig "${dir}")
      endif()
    endforeach(dir)
    list(REMOVE_DUPLICATES pkgconfig)
  endif()

  set(native FALSE)
  if(NOT musl AND NOT TOOLCHAIN_PREFIX AND NOT TOOLCHAIN_LIBDIR AND "${libdir}" STREQUAL "${host_libdir}")
    set(native TRUE)
  endif()

  set_property(GLOBAL PROPERTY TOOLCHAIN:PREFIX "${prefix}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:INCLUDEDIR "${includes}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:LIBDIR "${libdir}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:PKGCONFIGDIR "${pkgconfig}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:TRIPLET "${triplet}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:NATIVE "${native}")
  set_property(GLOBAL PROPERTY TOOLCHAIN:READY TRUE)
endfunction(_toolchain_init)

function(_toolchain_get field out)
  _toolchain_init()
  get_property(value GLOBAL PROPERTY TOOLCHAIN:${field})
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_triplet out)
  _toolchain_get(TRIPLET value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_prefix out)
  _toolchain_get(PREFIX value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_includedir out)
  _toolchain_get(INCLUDEDIR value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_libdir out)
  _toolchain_get(LIBDIR value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_pkgconfigdir out)
  _toolchain_get(PKGCONFIGDIR value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(toolchain_is_native out)
  _toolchain_get(NATIVE value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# toolchain_divert()
#
# A macro: the variables it sets belong to the calling scope, so call it inside a function
# and the CMake side undoes itself on return. The environment is not scoped:
# toolchain_restore() puts back PKG_CONFIG_LIBDIR, PKG_CONFIG_PATH, PKG_CONFIG_SYSROOT_DIR and
# PKGCFG_PATH_MAP. Not nestable. Does nothing for the host's own toolchain (TOOLCHAIN_DIVERT=AUTO).
#
# What the toolchain file configured stays in: its roots (CMAKE_FIND_ROOT_PATH, CMAKE_PREFIX_PATH)
# and its CMAKE_SYSTEM_INCLUDE_PATH / CMAKE_SYSTEM_LIBRARY_PATH are searched first, then what the
# compiler reported. A sysroot such as /usr/x86_64-w64-mingw32/sys-root/mingw is not where the
# compiler keeps its own C library, so probing alone would never look in it.
#
# pkg-config looks in <root>/lib/pkgconfig of every root, then in the probed directory. When
# c-utils' pkgcfg is installed (<triplet>-pkg-config, else pkgcfg, on the PATH, or
# TOOLCHAIN_PKGCFG), it replaces pkg-config and rewrites the paths of foreign .pc files while it
# reads them (they are not touched on disk): TOOLCHAIN_PKGCONFIG_PATH_MAP is a list of FROM=TO
# pairs (PKGCFG_PATH_MAP). For a Windows target /mingw64, /mingw32, /ucrt64 and /clang64 map to
# the first root that has a lib/pkgconfig, as MSYS2 packages unpacked into a sysroot have those
# prefixes, and a <dir>/sysroot prefix maps to <dir>/sys-root. The map replaces
# PKG_CONFIG_SYSROOT_DIR.
macro(toolchain_divert)
  toolchain_is_native(_TC_NATIVE)

  set(_TC_DIVERTED FALSE)
  if("${TOOLCHAIN_DIVERT}" STREQUAL "ON" OR ("${TOOLCHAIN_DIVERT}" STREQUAL "AUTO" AND NOT _TC_NATIVE))
    set(_TC_DIVERTED TRUE)

    toolchain_prefix(_TC_PREFIX)
    toolchain_includedir(_TC_INCLUDES)
    toolchain_libdir(_TC_LIBDIR)
    toolchain_pkgconfigdir(_TC_PKGCONFIG)

    # the toolchain file's own roots first, then the probed prefix
    set(_TC_ROOTS "")
    foreach(_TC_DIR ${CMAKE_FIND_ROOT_PATH} ${CMAKE_PREFIX_PATH} ${_TC_PREFIX})
      if(IS_DIRECTORY "${_TC_DIR}")
        list(APPEND _TC_ROOTS "${_TC_DIR}")
      endif()
    endforeach(_TC_DIR)
    list(REMOVE_DUPLICATES _TC_ROOTS)

    set(_TC_PCDIRS "")
    foreach(_TC_DIR ${_TC_ROOTS})
      foreach(_TC_SUB lib/pkgconfig share/pkgconfig)
        if(IS_DIRECTORY "${_TC_DIR}/${_TC_SUB}")
          list(APPEND _TC_PCDIRS "${_TC_DIR}/${_TC_SUB}")
        endif()
      endforeach(_TC_SUB)
    endforeach(_TC_DIR)
    list(APPEND _TC_PCDIRS ${_TC_PKGCONFIG})
    list(REMOVE_DUPLICATES _TC_PCDIRS)
    string(REPLACE ";" ":" _TC_PKGCONFIG_PATH "${_TC_PCDIRS}")

    foreach(_TC_VAR PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR PKGCFG_PATH_MAP)
      if(DEFINED ENV{${_TC_VAR}})
        set(_TC_SAVED_${_TC_VAR} "$ENV{${_TC_VAR}}")
      else()
        unset(_TC_SAVED_${_TC_VAR})
      endif()
    endforeach(_TC_VAR)

    set(ENV{PKG_CONFIG_LIBDIR} "${_TC_PKGCONFIG_PATH}")
    unset(ENV{PKG_CONFIG_PATH})
    if(CMAKE_SYSROOT)
      set(ENV{PKG_CONFIG_SYSROOT_DIR} "${CMAKE_SYSROOT}")
    endif()

    # pkgcfg replaces pkg-config for the pkg_check_modules() calls made while diverted
    set(_TC_PKGCFG_MAP ${TOOLCHAIN_PKGCONFIG_PATH_MAP})
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows" OR MINGW OR WIN32)
      foreach(_TC_DIR ${_TC_ROOTS})
        if(IS_DIRECTORY "${_TC_DIR}/lib/pkgconfig")
          foreach(_TC_FROM /mingw64 /mingw32 /ucrt64 /clang64)
            list(APPEND _TC_PKGCFG_MAP "${_TC_FROM}=${_TC_DIR}")
          endforeach(_TC_FROM)
          # prefixes written as <dir>/sysroot instead of the real <dir>/sys-root
          if("${_TC_DIR}" MATCHES "^(.*)/sys-root(/.*)?$")
            list(APPEND _TC_PKGCFG_MAP "${CMAKE_MATCH_1}/sysroot=${CMAKE_MATCH_1}/sys-root")
          endif()
          break()
        endif()
      endforeach(_TC_DIR)
    endif()

    # <triplet>-pkg-config (c-utils' pkgcfg under the cross name, which finds its own sysroot),
    # else pkgcfg itself. TOOLCHAIN_PKGCFG overrides; an empty cache entry would stop find_program()
    set(_TC_PKGCFG "${TOOLCHAIN_PKGCFG}")
    if(NOT _TC_PKGCFG)
      toolchain_triplet(_TC_TRIPLET)
      find_program(TOOLCHAIN_PKGCFG_FOUND NAMES ${_TC_TRIPLET}-pkg-config pkgcfg)
      set(_TC_PKGCFG "${TOOLCHAIN_PKGCFG_FOUND}")
    endif()
    if(_TC_PKGCFG)
      set(PKG_CONFIG_EXECUTABLE "${_TC_PKGCFG}")
      if(_TC_PKGCFG_MAP)
        string(REPLACE ";" ":" _TC_PKGCFG_MAP_ENV "${_TC_PKGCFG_MAP}")
        set(ENV{PKGCFG_PATH_MAP} "${_TC_PKGCFG_MAP_ENV}")
        unset(ENV{PKG_CONFIG_SYSROOT_DIR}) # the map replaces it, which would prepend to absolute paths
      endif()
    endif()

    set(_TC_SYS_INCLUDES ${CMAKE_SYSTEM_INCLUDE_PATH})
    set(_TC_SYS_LIBS ${CMAKE_SYSTEM_LIBRARY_PATH})
    set(CMAKE_PREFIX_PATH ${_TC_ROOTS})
    set(CMAKE_INCLUDE_PATH ${_TC_SYS_INCLUDES} ${_TC_INCLUDES})
    set(CMAKE_LIBRARY_PATH ${_TC_SYS_LIBS} "${_TC_LIBDIR}")
    # only the toolchain's directories: not the host's /usr/lib, /usr/include, PATH
    set(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH FALSE)
    set(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH FALSE)
    set(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH FALSE)
  endif()
endmacro(toolchain_divert)

macro(toolchain_restore)
  if(_TC_DIVERTED)
    foreach(_TC_VAR PKG_CONFIG_LIBDIR PKG_CONFIG_PATH PKG_CONFIG_SYSROOT_DIR PKGCFG_PATH_MAP)
      if(DEFINED _TC_SAVED_${_TC_VAR})
        set(ENV{${_TC_VAR}} "${_TC_SAVED_${_TC_VAR}}")
      else()
        unset(ENV{${_TC_VAR}})
      endif()
    endforeach(_TC_VAR)

    unset(PKG_CONFIG_EXECUTABLE)
    unset(CMAKE_PREFIX_PATH)
    unset(CMAKE_INCLUDE_PATH)
    unset(CMAKE_LIBRARY_PATH)
    unset(CMAKE_FIND_USE_CMAKE_SYSTEM_PATH)
    unset(CMAKE_FIND_USE_SYSTEM_ENVIRONMENT_PATH)
    unset(CMAKE_FIND_USE_CMAKE_ENVIRONMENT_PATH)
    set(_TC_DIVERTED FALSE)
  endif()
endmacro(toolchain_restore)

##############################################################################
# build policy
##############################################################################
#
#   dep_mode(<value> <out>)                  ON, OFF or AUTO ("" and anything else is AUTO)
#   dep_module_mode(<module> <out>)          MODULE_<MODULE> as ON or OFF (AUTO is an error)
#   dep_builder_usable(<builder> <out> [TOOLS <tool>...])
#       can this machine run a build of that kind: cmake always; autotools needs sh and make and
#       never works on MSVC; TOOLS must all be installed. Sets <out> and <out>_WHY.
#   dep_gate(<lib> MODULE <module> BUILDER <cmake|autotools|tool> [TOOLS ...] FOUND <bool>
#            OUT <var> [OPTION <BUILD_x>])
#       what to do about a library: FOUND (use the installed one), BUILD or SKIP (the module
#       stays out, a warning says how to change that)
#
# Who decides what (BUILD_<lib> is ON, OFF or AUTO, MODULE_<module> is ON or OFF):
#
#   BUILD_<lib>=ON       build it, even when installed          (needs DEPS_BUILD to allow it)
#   BUILD_<lib>=OFF      never build it
#   MODULE_<m>=ON        the module is built; a missing library is built (an error when it cannot be)
#   MODULE_<m>=OFF       the module is not built, its library is not looked for
#   DEPS_BUILD           REQUESTED  builds only what the two above ask for (default)
#                        NONE       builds nothing, so a request for a build is an error
#                        CMAKE      also builds every missing library that has a CMake build
#                        ALL        builds every missing library
#   DEPS_MISSING         FAIL (default) stops, DISABLE switches the module off with a warning
#
# A library without a build the platform can run (autotools on MSVC) is never built.

function(dep_mode value out)
  string(TOUPPER "${value}" v)
  if(v MATCHES "^(ON|TRUE|YES|Y|1)$")
    set(${out} ON PARENT_SCOPE)
  elseif(v MATCHES "^(OFF|FALSE|NO|N|0)$" OR v MATCHES "-NOTFOUND$")
    set(${out} OFF PARENT_SCOPE)
  else()
    set(${out} AUTO PARENT_SCOPE)
  endif()
endfunction(dep_mode)

function(dep_module_mode module out)
  string(TOUPPER "${module}" upper)
  string(REGEX REPLACE "[^A-Z0-9]" "_" upper "${upper}")
  if("${MODULE_${upper}}" STREQUAL "")
    set(MODULE_${upper} ON)
  endif()
  dep_mode("${MODULE_${upper}}" mode)
  if("${mode}" STREQUAL "AUTO")
    message(FATAL_ERROR "MODULE_${upper}='${MODULE_${upper}}': a module is ON or OFF, AUTO does not exist. "
                        "A module is not chosen by which libraries are installed.")
  endif()
  set(${out} "${mode}" PARENT_SCOPE)
endfunction(dep_module_mode)

function(dep_builder_usable builder out)
  cmake_parse_arguments(B "" "" "TOOLS" ${ARGN})
  set(usable TRUE)
  set(why "")

  if("${builder}" STREQUAL "autotools")
    if(MSVC)
      set(usable FALSE)
      set(why "a GNU autotools build is not possible with MSVC")
    endif()
    list(APPEND B_TOOLS sh make)
  endif()

  set(missing "")
  foreach(tool ${B_TOOLS})
    if("${tool}" STREQUAL "make")
      find_program(TOOL_make NAMES gmake make)
    else()
      find_program(TOOL_${tool} NAMES ${tool})
    endif()
    if(NOT TOOL_${tool})
      list(APPEND missing ${tool})
    endif()
  endforeach(tool)

  if(missing)
    string(REPLACE ";" " " missing "${missing}")
    set(usable FALSE)
    set(why "needs ${missing}")
  endif()

  set(${out} ${usable} PARENT_SCOPE)
  set(${out}_WHY "${why}" PARENT_SCOPE)
endfunction(dep_builder_usable)

# _dep_policy_allows(<builder> <out> <why-out>): DEPS_BUILD lets a build of that kind run at all
function(_dep_policy_allows builder out why)
  set(allowed TRUE)
  set(reason "")
  if("${DEPS_BUILD}" STREQUAL "NONE")
    set(allowed FALSE)
    set(reason "DEPS_BUILD=NONE builds nothing")
  elseif("${DEPS_BUILD}" STREQUAL "CMAKE" AND NOT "${builder}" STREQUAL "cmake")
    set(allowed FALSE)
    set(reason "DEPS_BUILD=CMAKE builds only libraries with a CMake build, this one does not")
  endif()
  set(${out} ${allowed} PARENT_SCOPE)
  set(${why} "${reason}" PARENT_SCOPE)
endfunction(_dep_policy_allows)

function(dep_gate lib)
  cmake_parse_arguments(G "" "MODULE;BUILDER;FOUND;OUT;OPTION" "TOOLS" ${ARGN})

  string(TOUPPER "${lib}" libupper)
  string(REGEX REPLACE "[^A-Z0-9]" "_" libupper "${libupper}")
  string(TOUPPER "${G_MODULE}" modupper)
  string(REGEX REPLACE "[^A-Z0-9]" "_" modupper "${modupper}")
  if(NOT G_OPTION)
    set(G_OPTION BUILD_${libupper})
  endif()
  if(NOT G_BUILDER)
    set(G_BUILDER cmake)
  endif()

  dep_mode("${${G_OPTION}}" buildopt)
  dep_module_mode(${G_MODULE} modmode)
  _dep_policy_allows(${G_BUILDER} allowed policy_why)
  dep_builder_usable(${G_BUILDER} usable TOOLS ${G_TOOLS})

  set(can_build FALSE)
  set(why "")
  if(NOT allowed)
    set(why "${policy_why}")
  elseif(NOT usable)
    set(why "${lib} cannot be built here: ${usable_WHY}")
  else()
    set(can_build TRUE)
  endif()

  set(result SKIP)

  if("${buildopt}" STREQUAL "ON")
    if(NOT can_build)
      message(FATAL_ERROR "${G_MODULE}: ${G_OPTION}=ON, but ${why}")
    endif()
    set(result BUILD)
  elseif(G_FOUND)
    set(result FOUND)
  elseif("${buildopt}" STREQUAL "OFF")
    set(why "${G_OPTION}=OFF")
  else()
    set(by_module FALSE)
    if("${modmode}" STREQUAL "ON")
      set(by_module TRUE)
    endif()
    if(by_module OR "${DEPS_BUILD}" STREQUAL "ALL" OR "${DEPS_BUILD}" STREQUAL "CMAKE")
      if(can_build)
        set(result BUILD)
      elseif(by_module)
        message(FATAL_ERROR "${G_MODULE}: MODULE_${modupper}=ON, but ${lib} is not installed and ${why}")
      endif()
    endif()
  endif()

  if("${result}" STREQUAL "SKIP")
    set(text "${G_MODULE}: ${lib} not found")
    if(why)
      string(APPEND text " (${why})")
    endif()
    string(APPEND text ", module skipped. Either -DMODULE_${modupper}=OFF to drop it quietly,")
    string(APPEND text " or -D${G_OPTION}=ON to build ${lib} from source,")
    string(APPEND text " or install ${lib}.")
    if("${DEPS_MISSING}" STREQUAL "FAIL")
      message(FATAL_ERROR "${text} (DEPS_MISSING=FAIL)")
    endif()
    message(WARNING "${text}")
    set_property(GLOBAL APPEND PROPERTY DEP:SKIPPED "${G_MODULE} (${lib})")
  endif()

  set(${G_OUT} ${result} PARENT_SCOPE)
endfunction(dep_gate)

##############################################################################
# dependency registry
##############################################################################

set_property(GLOBAL PROPERTY DEP:NAMES "")

function(_dep_set name suffix field)
  set_property(GLOBAL PROPERTY "DEP:${name}:${suffix}:${field}" "${ARGN}")
endfunction()

function(_dep_name_check name)
  get_property(known GLOBAL PROPERTY "DEP:${name}:DECLARED")
  if(NOT known)
    message(FATAL_ERROR "dependency '${name}' is not declared, call dep_declare(${name} ...) first")
  endif()
endfunction()

# dep_declare(<name> ...): see the top of this file. Creates the option BUILD_<NAME> (AUTO, ON
# or OFF): ON builds even when an installed copy exists, OFF never builds. BUILDER (cmake by
# default, autotools, or tool with TOOLS) says what kind of build the BUILD command runs, which
# DEPS_BUILD and the platform decide on.
function(dep_declare name)
  cmake_parse_arguments(D "" "FIND;BUILD;OPTION;DESCRIPTION;PKGCONFIG;BUILDER" "DEPENDS;HEADERS;LIBRARIES;TOOLS" ${ARGN})

  string(TOUPPER "${name}" upper)
  string(REGEX REPLACE "[^A-Z0-9]" "_" upper "${upper}")
  if(NOT D_OPTION)
    set(D_OPTION BUILD_${upper})
  endif()
  if(NOT D_DESCRIPTION)
    set(D_DESCRIPTION "${name}")
  endif()

  if(NOT D_BUILDER)
    set(D_BUILDER cmake)
  endif()

  set(${D_OPTION} AUTO CACHE STRING "Build ${D_DESCRIPTION}: ON instead of the installed one, OFF never, AUTO as DEPS_BUILD says")
  set_property(CACHE ${D_OPTION} PROPERTY STRINGS AUTO ON OFF)

  set_property(GLOBAL PROPERTY "DEP:${name}:DECLARED" TRUE)
  set_property(GLOBAL PROPERTY "DEP:${name}:DEPENDS" "${D_DEPENDS}")
  set_property(GLOBAL PROPERTY "DEP:${name}:PKGCONFIG" "${D_PKGCONFIG}")
  set_property(GLOBAL PROPERTY "DEP:${name}:HEADERS" "${D_HEADERS}")
  set_property(GLOBAL PROPERTY "DEP:${name}:LIBRARIES" "${D_LIBRARIES}")
  set_property(GLOBAL PROPERTY "DEP:${name}:FIND" "${D_FIND}")
  set_property(GLOBAL PROPERTY "DEP:${name}:BUILD" "${D_BUILD}")
  set_property(GLOBAL PROPERTY "DEP:${name}:OPTION" "${D_OPTION}")
  set_property(GLOBAL PROPERTY "DEP:${name}:BUILDER" "${D_BUILDER}")
  set_property(GLOBAL PROPERTY "DEP:${name}:TOOLS" "${D_TOOLS}")

  get_property(names GLOBAL PROPERTY DEP:NAMES)
  list(APPEND names "${name}")
  list(REMOVE_DUPLICATES names)
  set_property(GLOBAL PROPERTY DEP:NAMES "${names}")
endfunction(dep_declare)

# dep_provide(<name> <suffix> SOURCE <system|built> ...): what a FIND or BUILD command calls
# to say what it found or made. LIBRARIES are in link order, TARGETS are the build targets a
# consumer must DEPENDS on, PREFIX the install prefix of a built copy.
function(dep_provide name suffix)
  cmake_parse_arguments(P "" "SOURCE;ORIGIN;PREFIX" "INCLUDE_DIRS;LIBRARIES;LINK_FLAGS;TARGETS" ${ARGN})

  if(P_INCLUDE_DIRS)
    list(FILTER P_INCLUDE_DIRS EXCLUDE REGEX "^$")
  endif()
  _dep_set(${name} ${suffix} SOURCE "${P_SOURCE}")
  _dep_set(${name} ${suffix} ORIGIN "${P_ORIGIN}")
  _dep_set(${name} ${suffix} PREFIX "${P_PREFIX}")
  _dep_set(${name} ${suffix} INCLUDE_DIRS "${P_INCLUDE_DIRS}")
  _dep_set(${name} ${suffix} LIBRARIES "${P_LIBRARIES}")
  _dep_set(${name} ${suffix} LINK_FLAGS "${P_LINK_FLAGS}")
  _dep_set(${name} ${suffix} TARGETS "${P_TARGETS}")
endfunction(dep_provide)

function(dep_get name suffix field out)
  get_property(value GLOBAL PROPERTY "DEP:${name}:${suffix}:${field}")
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# Installed copy: pkg-config first, then find_path()/find_library(); both look where the
# toolchain keeps its libraries when dep_resolve() has diverted the search.
function(_dep_find_generic name suffix)
  get_property(module GLOBAL PROPERTY "DEP:${name}:PKGCONFIG")
  get_property(headers GLOBAL PROPERTY "DEP:${name}:HEADERS")
  get_property(libs GLOBAL PROPERTY "DEP:${name}:LIBRARIES")

  if(module)
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
      pkg_check_modules(_DEP_${name} QUIET ${module})
      if(_DEP_${name}_FOUND)
        list(GET _DEP_${name}_LINK_LIBRARIES 0 first)
        dep_provide(${name} ${suffix} SOURCE system ORIGIN "${first}" INCLUDE_DIRS ${_DEP_${name}_INCLUDE_DIRS}
                    LIBRARIES ${_DEP_${name}_LINK_LIBRARIES} LINK_FLAGS ${_DEP_${name}_LDFLAGS_OTHER})
        return()
      endif()
    endif()
  endif()

  if(headers AND libs)
    find_path(_dep_${name}_include NAMES ${headers})
    find_library(_dep_${name}_library NAMES ${libs})
    if(_dep_${name}_include AND _dep_${name}_library)
      dep_provide(${name} ${suffix} SOURCE system ORIGIN "${_dep_${name}_library}" INCLUDE_DIRS "${_dep_${name}_include}"
                  LIBRARIES "${_dep_${name}_library}")
    endif()
  endif()
endfunction(_dep_find_generic)

# dep_resolve(<name> SUFFIX <s> ...): installed copy, else the dependencies and a build.
# Safe to call again: a (name, suffix) pair is resolved once, and a cycle is an error.
function(dep_resolve name)
  cmake_parse_arguments(R "" "SUFFIX;PIC;BINARY;SOURCE" "" ${ARGN})
  _dep_name_check(${name})

  if(NOT R_SUFFIX)
    message(FATAL_ERROR "dep_resolve(${name}): SUFFIX is required")
  endif()
  if(NOT DEFINED R_PIC)
    set(R_PIC OFF)
  endif()
  if(NOT R_BINARY)
    set(R_BINARY "${CMAKE_CURRENT_BINARY_DIR}")
  endif()
  if(NOT R_SOURCE)
    set(R_SOURCE "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()

  get_property(state GLOBAL PROPERTY "DEP:${name}:${R_SUFFIX}:STATE")
  if("${state}" STREQUAL "resolving")
    message(FATAL_ERROR "dependency cycle through '${name}'")
  elseif("${state}" STREQUAL "done")
    return()
  endif()
  _dep_set(${name} ${R_SUFFIX} STATE resolving)

  get_property(suffixes GLOBAL PROPERTY DEP:SUFFIXES)
  list(APPEND suffixes ${R_SUFFIX})
  list(REMOVE_DUPLICATES suffixes)
  set_property(GLOBAL PROPERTY DEP:SUFFIXES "${suffixes}")

  get_property(option GLOBAL PROPERTY "DEP:${name}:OPTION")
  get_property(find GLOBAL PROPERTY "DEP:${name}:FIND")
  get_property(build GLOBAL PROPERTY "DEP:${name}:BUILD")
  get_property(depends GLOBAL PROPERTY "DEP:${name}:DEPENDS")
  get_property(builder GLOBAL PROPERTY "DEP:${name}:BUILDER")
  get_property(tools GLOBAL PROPERTY "DEP:${name}:TOOLS")

  _dep_set(${name} ${R_SUFFIX} SOURCE "")

  dep_mode("${${option}}" mode)
  if(NOT "${mode}" STREQUAL "ON")
    toolchain_divert()
    if(find)
      cmake_language(CALL ${find} ${name} ${R_SUFFIX})
    else()
      _dep_find_generic(${name} ${R_SUFFIX})
    endif()
    toolchain_restore()
  endif()

  dep_get(${name} ${R_SUFFIX} SOURCE source)
  if(NOT source)
    # resolved for a library that is being built: its own build decides, as for any library
    set(why "")
    if(NOT build)
      set(why "nothing to build it with")
    elseif("${mode}" STREQUAL "OFF")
      set(why "${option}=OFF")
    else()
      _dep_policy_allows("${builder}" allowed policy_why)
      dep_builder_usable("${builder}" usable TOOLS ${tools})
      if(NOT allowed)
        set(why "${policy_why}")
      elseif(NOT usable)
        set(why "it cannot be built here: ${usable_WHY}")
      endif()
    endif()

    if(NOT why)
      foreach(dep ${depends})
        dep_resolve(${dep} SUFFIX ${R_SUFFIX} PIC ${R_PIC} BINARY ${R_BINARY} SOURCE ${R_SOURCE})
      endforeach(dep)
      _dep_set(${name} ${R_SUFFIX} USES ${depends})

      cmake_language(CALL ${build} ${name} ${R_SUFFIX} ${R_PIC} ${R_BINARY} ${R_SOURCE})
      dep_get(${name} ${R_SUFFIX} SOURCE source)
    elseif("${mode}" STREQUAL "ON")
      message(FATAL_ERROR "${option}=ON, but ${why}")
    endif()

    if(NOT source)
      _dep_set(${name} ${R_SUFFIX} SOURCE missing)
      set(text "${name} not found and not built (${why}). Install it, or allow a build with -D${option}=ON")
      if("${DEPS_MISSING}" STREQUAL "FAIL")
        message(FATAL_ERROR "${text} (DEPS_MISSING=FAIL)")
      endif()
      message(WARNING "${text}.")
    endif()
  endif()

  _dep_set(${name} ${R_SUFFIX} STATE done)
endfunction(dep_resolve)

##############################################################################
# the tree
##############################################################################

# dep_collect(<name> <suffix> <FIELD> <OUT>): the field of the dependency followed by that of
# everything it was built with, dependants first. A library list keeps the last copy of a
# duplicate (what a static linker needs), the others keep the first.
function(dep_collect name suffix field out)
  dep_get(${name} ${suffix} ${field} own)
  set(all ${own})

  dep_get(${name} ${suffix} USES uses)
  foreach(dep ${uses})
    dep_collect(${dep} ${suffix} ${field} sub)
    list(APPEND all ${sub})
  endforeach(dep)

  if("${field}" STREQUAL "LIBRARIES" AND all)
    list(REVERSE all)
    list(REMOVE_DUPLICATES all)
    list(REVERSE all)
  elseif(all)
    list(REMOVE_DUPLICATES all)
  endif()

  set(${out} "${all}" PARENT_SCOPE)
endfunction(dep_collect)

function(dep_include_dirs name suffix out)
  dep_collect(${name} ${suffix} INCLUDE_DIRS value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(dep_libraries name suffix out)
  dep_collect(${name} ${suffix} LIBRARIES value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(dep_link_flags name suffix out)
  dep_collect(${name} ${suffix} LINK_FLAGS value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

function(dep_targets name suffix out)
  dep_collect(${name} ${suffix} TARGETS value)
  set(${out} "${value}" PARENT_SCOPE)
endfunction()

# dep_tree(<name> OUT): every declared dependency below <name>, depth first, no duplicates.
function(dep_tree name out)
  _dep_name_check(${name})
  get_property(depends GLOBAL PROPERTY "DEP:${name}:DEPENDS")

  set(all "")
  foreach(dep ${depends})
    dep_tree(${dep} sub)
    list(APPEND all ${dep} ${sub})
  endforeach(dep)

  if(all)
    list(REMOVE_DUPLICATES all)
  endif()
  set(${out} "${all}" PARENT_SCOPE)
endfunction(dep_tree)

function(_dep_print_tree name indent)
  message(STATUS "${indent}${name}")
  get_property(depends GLOBAL PROPERTY "DEP:${name}:DEPENDS")
  foreach(dep ${depends})
    _dep_print_tree(${dep} "${indent}  ")
  endforeach(dep)
endfunction()

function(dep_print_tree name)
  _dep_name_check(${name})
  _dep_print_tree(${name} "")
endfunction()

##############################################################################
# summary
##############################################################################

function(_dep_pad text width out)
  string(LENGTH "${text}" len)
  set(pad "")
  while(len LESS width)
    string(APPEND pad " ")
    math(EXPR len "${len} + 1")
  endwhile()
  set(${out} "${text}${pad}" PARENT_SCOPE)
endfunction()

# dep_summary([DEFER]): one row per dependency and suffix: where it comes from (system: the
# installed library, built: the prefix it was built into) and what it was built with. DEFER
# prints when the top-level configure ends (CMake 3.19), after everything else.
function(dep_summary)
  # a summary shows through the quiet level of a module configure (QUICKJS_MODULE_QUIET)
  if(QUICKJS_MODULE_QUIET)
    set(CMAKE_MESSAGE_LOG_LEVEL STATUS)
  endif()
  if("${ARGN}" STREQUAL "DEFER")
    if(NOT CMAKE_VERSION VERSION_LESS 3.19)
      cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL dep_summary)
    endif()
    return()
  endif()

  get_property(names GLOBAL PROPERTY DEP:NAMES)
  toolchain_triplet(triplet)
  toolchain_prefix(prefix)
  toolchain_is_native(native)
  if(native)
    set(kind "native")
  else()
    set(kind "diverted")
  endif()

  get_property(all_suffixes GLOBAL PROPERTY DEP:SUFFIXES)

  set(rows "")
  foreach(name ${names})
    foreach(suffix ${all_suffixes})
      get_property(state GLOBAL PROPERTY "DEP:${name}:${suffix}:STATE")
      if("${state}" STREQUAL "done")
        dep_get(${name} ${suffix} SOURCE source)
        dep_get(${name} ${suffix} ORIGIN origin)
        dep_get(${name} ${suffix} PREFIX where)
        dep_get(${name} ${suffix} USES uses)
        if(NOT origin)
          set(origin "${where}")
        endif()
        _dep_pad("${name}" 12 c1)
        _dep_pad("${suffix}" 8 c2)
        _dep_pad("${source}" 8 c3)
        set(row "${c1}${c2}${c3}${origin}")
        if(uses)
          string(REPLACE ";" ", " usedby "${uses}")
          string(APPEND row "  (built with: ${usedby})")
        endif()
        list(APPEND rows "${row}")
      endif()
    endforeach(suffix)
  endforeach(name)

  get_property(skipped GLOBAL PROPERTY DEP:SKIPPED)

  if(rows OR skipped)
    message("")
    message(STATUS "Library dependencies (toolchain ${triplet}, prefix ${prefix}, ${kind})")
    foreach(row ${rows})
      message(STATUS "  ${row}")
    endforeach(row)
  endif()

  if(skipped)
    string(REPLACE ";" ", " skipped "${skipped}")
    message(STATUS "  skipped for lack of a library: ${skipped} (DEPS_BUILD=${DEPS_BUILD}, DEPS_MISSING=${DEPS_MISSING})")
  endif()
endfunction(dep_summary)
