#!/usr/bin/env qjsm
import { readFileSync } from 'fs';
import process from 'process';
import { WASI } from '../lib/wasi.js';

/**
 * Runs the WASI build of the shish shell (https://github.com/rsenn/shish).
 *
 *   qjsm examples/wasm-shish.js [-c 'echo hi'] [script args...]
 *   SHISH_WASM=path/to/shish SHISH_ROOT=/some/dir qjsm examples/wasm-shish.js
 *
 * The guest sees SHISH_ROOT (default: the current directory) as `/`.
 * Runs on qjsm, node, bun and deno.
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

process.exit(wasi.start(instance));
