# Convenience toolchain file for building qjs-modules with Emscripten.
#
# It locates an installed Emscripten SDK (emsdk) and delegates to the
# CMake toolchain file that ships with it, so the SDK's own compiler/
# linker setup stays authoritative instead of being duplicated here.
#
# Mirrors ../../cmake/emscripten.toolchain.cmake in the parent quickjs
# project, which qjs-modules is normally built alongside.
#
# Usage (with the emsdk environment sourced, e.g. `source
# <emsdk>/emsdk_env.sh`, so that the EMSDK environment variable is set):
#
#   cmake -B build/wasm32-unknown-emscripten -DCMAKE_TOOLCHAIN_FILE=cmake/emscripten.toolchain.cmake
#   cmake --build build/wasm32-unknown-emscripten
#
# or, without sourcing emsdk_env.sh first:
#
#   emcmake cmake -B build/wasm32-unknown-emscripten
#   emmake cmake --build build/wasm32-unknown-emscripten

if(NOT DEFINED ENV{EMSDK})
  message(
    FATAL_ERROR
    "EMSDK environment variable is not set. Run 'source <emsdk>/emsdk_env.sh' first, or configure with emcmake instead of this toolchain file."
  )
endif()

set(EMSCRIPTEN_SDK_TOOLCHAIN
    "$ENV{EMSDK}/upstream/emscripten/cmake/Modules/Platform/Emscripten.cmake")

if(NOT EXISTS "${EMSCRIPTEN_SDK_TOOLCHAIN}")
  message(
    FATAL_ERROR
    "Could not find Emscripten's own CMake toolchain file at ${EMSCRIPTEN_SDK_TOOLCHAIN}"
  )
endif()

include("${EMSCRIPTEN_SDK_TOOLCHAIN}")
