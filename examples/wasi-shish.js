#!/usr/bin/env qjsm
import { readFileSync } from 'fs';
import process from 'process';
import { WASI } from '../lib/wasi.js';

/**
 * Runs the WASI build of the shish shell (https://github.com/rsenn/shish).
 *
 *   qjsm examples/wasi-shish.js [-c 'echo hi'] [script args...]
 *   SHISH_WASM=path/to/shish SHISH_ROOT=/some/dir qjsm examples/wasi-shish.js
 *
 * The guest sees SHISH_ROOT (default: the current directory) as `/`.
 * Runs on qjsm, node, bun and deno, given a build the engine can execute:
 * a shish built with `-fwasm-exceptions` (legacy `try`) needs node, bun or
 * deno; wasm3 and WAMR in qjsm lack exception handling.
 */
if(!globalThis.WebAssembly) await import('../lib/webassembly.js');

const file = process.env.SHISH_WASM ?? import.meta.url.slice(7).replace(/[^/]*$/, '') + '../../../shish/build/wasi/shish';
const root = process.env.SHISH_ROOT ?? '.';

const wasi = new WASI({
  version: 'preview1',
  args: ['shish', ...process.argv.slice(2)],
  env: { PATH: '/bin', HOME: '/', PWD: '/' },
  preopens: { '/': root },
});

const { instance } = await WebAssembly.instantiate(readFileSync(file), wasi.getImportObject());

try {
  process.exit(wasi.start(instance));
} catch(e) {
  if(e instanceof WebAssembly.RuntimeError) console.error(`${e.message}: does the build use wasm exceptions (-fwasm-exceptions)?`);
  throw e;
}
