# CheckTypeSize for Emscripten, used instead of CMake's own module (the sub-builds of the bundled libraries
# get this directory through -DCMAKE_MODULE_PATH, so their include(CheckTypeSize) finds this file first).
#
# CMake's check_type_size() compiles a program whose data holds the text "INFO:size[00004]" and greps the
# result for it. emcc's output is a .js file (the wasm is a separate or encoded file), so the grep finds
# something else: every type comes out as size 7 and lzo2 and libarchive refuse to configure. Compiling to a
# static library instead leaves the object file, with the text in it, as the file that gets searched; so
# this module just runs the original check with CMAKE_TRY_COMPILE_TARGET_TYPE=STATIC_LIBRARY.
include("${CMAKE_ROOT}/Modules/CheckTypeSize.cmake")

# redefining a macro keeps the old definition as _check_type_size
macro(CHECK_TYPE_SIZE TYPE VARIABLE)
  set(_SHISH_TRY_TARGET_TYPE "${CMAKE_TRY_COMPILE_TARGET_TYPE}")
  set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
  _check_type_size("${TYPE}" "${VARIABLE}" ${ARGN})
  set(CMAKE_TRY_COMPILE_TARGET_TYPE "${_SHISH_TRY_TARGET_TYPE}")
endmacro()
