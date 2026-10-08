# TestBigEndian for Emscripten: the check has the same problem as CheckTypeSize (it greps emcc's .js output
# for a string that is in the wasm), and WebAssembly is little-endian by definition. liblzma's configure
# finds this directory through -DCMAKE_MODULE_PATH.
macro(TEST_BIG_ENDIAN VARIABLE)
  set(${VARIABLE} 0)
endmacro()
