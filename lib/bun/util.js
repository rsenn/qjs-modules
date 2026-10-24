/* bun/util.js: the Bun.* utility functions, loaded on the first read of one of them.
 * depends on: fs, path, misc, blob, inspect, terminal, glob, perf_hooks, textcode, url, std, os.
 * rule: Bun's names, argument shapes and results, checked against bun 1.4.2.
 *
 * | Bun.*                       | here                                         |
 * | --------------------------- | -------------------------------------------- |
 * | deepEquals(a, b, strict)    | own walk, SameValue for primitives            |
 * | deepMatch(subset, value)    | own walk, arrays must have the same length    |
 * | inspect(v, opts)            | inspect, inspect.custom is Node's symbol      |
 * | nanoseconds()               | performance.now() in ns                       |
 * | sleep(ms or Date), sleepSync | setTimeout, os.sleep                         |
 * | peek(p), peek.status(p)     | misc.promiseState, promiseResult              |
 * | which(cmd, { PATH, cwd })   | a PATH walk, executable regular files only    |
 * | readableStreamTo*           | one reader loop                               |
 * | stripANSI, stringWidth      | terminal                                      |
 */
import { accessSync, constants, statSync } from 'fs';
import { delimiter, isAbsolute, resolve } from 'path';
import { promiseResult, promiseState } from 'misc';
import { Blob } from 'blob';
import { inspect as inspectValue } from 'inspect';
import { charWidth, stringWidth as cellWidth, stripAnsi } from 'terminal';
import { performance } from 'perf_hooks';
import { TextDecoder, TextEncoder } from 'textcode';
import { gc as stdGc } from 'std';
import { sleep as osSleep } from 'os';

export { fileURLToPath, pathToFileURL } from 'url';
export { Glob } from 'glob';

const encoder = new TextEncoder();
const isObject = v => v !== null && (typeof v == 'object' || typeof v == 'function');

/* own enumerable string and symbol keys, the ones holding undefined left out unless strict */
const keysOf = (o, strict) => Reflect.ownKeys(o).filter(k => Object.prototype.propertyIsEnumerable.call(o, k) && (strict || o[k] !== undefined));

function equal(a, b, strict, seen) {
  if(Object.is(a, b)) return true;
  if(!isObject(a) || !isObject(b) || typeof a == 'function' || typeof b == 'function') return false;

  const tag = Object.prototype.toString.call(a);

  if(tag != Object.prototype.toString.call(b) || Array.isArray(a) != Array.isArray(b)) return false;

  if(strict) {
    const pa = Object.getPrototypeOf(a),
      pb = Object.getPrototypeOf(b);

    if(pa !== pb && pa !== null && pb !== null) return false;
  }

  if(seen.get(a) === b) return true;
  seen.set(a, b);

  if(a instanceof Date) return a.getTime() === b.getTime();
  if(a instanceof RegExp) return a.source === b.source && a.flags === b.flags;
  if(a instanceof Error && (a.name !== b.name || a.message !== b.message)) return false;
  if(a instanceof Number || a instanceof String || a instanceof Boolean) return Object.is(a.valueOf(), b.valueOf());

  if(a instanceof ArrayBuffer || ArrayBuffer.isView(a)) {
    if(a.constructor !== b.constructor || a.byteLength !== b.byteLength) return false;

    const x = new Uint8Array(a.buffer ?? a, a.byteOffset ?? 0, a.byteLength),
      y = new Uint8Array(b.buffer ?? b, b.byteOffset ?? 0, b.byteLength);

    return x.every((v, i) => v === y[i]);
  }

  if(a instanceof Map) {
    if(a.size !== b.size) return false;

    for(const [k, v] of a) if(!b.has(k) || !equal(v, b.get(k), strict, seen)) return false;

    return true;
  }

  if(a instanceof Set) {
    if(a.size !== b.size) return false;

    for(const v of a) if(!b.has(v) && !(isObject(v) && [...b].some(w => equal(v, w, strict, seen)))) return false;

    return true;
  }

  if(Array.isArray(a)) {
    if(a.length !== b.length) return false;

    for(let i = 0; i < a.length; i++) {
      if(strict && i in a !== i in b) return false;
      if(!equal(a[i], b[i], strict, seen)) return false;
    }
  }

  const ka = keysOf(a, strict).filter(k => !Array.isArray(a) || !/^\d+$/.test(k)),
    kb = keysOf(b, strict).filter(k => !Array.isArray(b) || !/^\d+$/.test(k));

  return ka.length === kb.length && ka.every(k => Object.prototype.hasOwnProperty.call(b, k) && equal(a[k], b[k], strict, seen));
}

/* deepEquals(a, b, strict): structural equality, strict also tells undefined properties, holes and prototypes apart */
export function deepEquals(a, b, strict = false) {
  return equal(a, b, !!strict, new Map());
}

/* deepMatch(subset, value): every property of subset is in value with a matching value */
export function deepMatch(subset, value) {
  if(Object.is(subset, value)) return true;
  if(!isObject(subset) || !isObject(value)) return false;
  if(Array.isArray(subset)) return Array.isArray(value) && subset.length === value.length && subset.every((v, i) => deepMatch(v, value[i]));
  if(subset instanceof Date || subset instanceof RegExp || subset instanceof Map || subset instanceof Set || ArrayBuffer.isView(subset)) return deepEquals(subset, value);

  return keysOf(subset, true).every(k => k in value && deepMatch(subset[k], value[k]));
}

/* inspect(v, opts): the string console.log would print, without colors unless asked */
export function inspect(value, options) {
  return inspectValue(value, { colors: false, ...options });
}

inspect.custom = Symbol.for('nodejs.util.inspect.custom');

/* nanoseconds since the process started, a number */
export const nanoseconds = () => Math.round(performance.now() * 1e6);

export function gc(force) {
  stdGc();
}

/* sleep(ms | Date): a promise resolved after ms, or at the Date */
export function sleep(when) {
  const ms = when instanceof Date ? when.getTime() - Date.now() : Number(when);

  return new Promise(resolve => setTimeout(resolve, Math.max(0, ms || 0)));
}

export function sleepSync(ms) {
  osSleep(Math.max(0, Number(ms) || 0));
}

const PENDING = 0,
  FULFILLED = 1;

/* peek(p): the value or error of a settled promise, p itself while pending, non-promises as they are */
export function peek(p) {
  return p instanceof Promise && promiseState(p) != PENDING ? promiseResult(p) : p;
}

peek.status = p => (p instanceof Promise ? ['pending', 'fulfilled', 'rejected'][promiseState(p)] : 'fulfilled');

const bytesOf = v => (v instanceof ArrayBuffer ? new Uint8Array(v) : ArrayBuffer.isView(v) ? new Uint8Array(v.buffer, v.byteOffset, v.byteLength) : encoder.encode(String(v)));

/* concatArrayBuffers(list, maxLength, asUint8Array): one ArrayBuffer, at most maxLength bytes */
export function concatArrayBuffers(list, maxLength = Infinity, asUint8Array = false) {
  const views = [...list].map(bytesOf);
  const size = Math.min(
    views.reduce((n, v) => n + v.byteLength, 0),
    maxLength ?? Infinity,
  );
  const out = new Uint8Array(size);
  let pos = 0;

  for(const v of views) {
    if(pos >= size) break;

    out.set(v.subarray(0, size - pos), pos);
    pos += Math.min(v.byteLength, size - pos);
  }

  return asUint8Array ? out : out.buffer;
}

export const allocUnsafe = n => new Uint8Array(n);

const HTML_ESCAPES = { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#x27;' };

export const escapeHTML = v => String(v).replace(/[&<>"']/g, c => HTML_ESCAPES[c]);

export const stripANSI = s => stripAnsi(String(s));

/* stringWidth(s, { countAnsiEscapeCodes }): terminal cells, wide characters count 2 */
export function stringWidth(s, options) {
  s = String(s);

  return options?.countAnsiEscapeCodes ? [...s].reduce((w, ch) => w + charWidth(ch), 0) : cellWidth(s);
}

/* indexOfLine(buf, offset): index of the first newline at or after offset, -1 if none */
export function indexOfLine(buf, offset = 0) {
  return bytesOf(buf).indexOf(10, offset);
}

function executable(path) {
  try {
    return statSync(path).isFile() && (accessSync(path, constants.X_OK), true);
  } catch(e) {
    return false;
  }
}

/* which(cmd, { PATH, cwd }): the path of the executable, null when there is none */
export function which(cmd, options = {}) {
  cmd = String(cmd);

  const cwd = options.cwd ?? '.';

  if(cmd.includes('/')) {
    const path = isAbsolute(cmd) ? cmd : resolve(cwd, cmd);

    return executable(path) ? (isAbsolute(cmd) ? cmd : path) : null;
  }

  const PATH = options.PATH ?? globalThis.process?.env?.PATH ?? '';

  for(const dir of PATH.split(delimiter)) {
    if(!dir) continue;

    const path = resolve(cwd, dir, cmd);

    if(executable(path)) return path;
  }

  return null;
}

/* the chunks of a ReadableStream, strings and bytes as they came */
async function chunksOf(stream) {
  const reader = stream.getReader(),
    chunks = [];

  for(;;) {
    const { value, done } = await reader.read();

    if(done) break;

    chunks.push(value);
  }

  reader.releaseLock?.();
  return chunks;
}

export async function readableStreamToArray(stream) {
  return chunksOf(stream);
}

export async function readableStreamToBytes(stream) {
  return concatArrayBuffers(await chunksOf(stream), Infinity, true);
}

export async function readableStreamToArrayBuffer(stream) {
  return concatArrayBuffers(await chunksOf(stream));
}

export async function readableStreamToText(stream) {
  return new TextDecoder().decode(await readableStreamToBytes(stream));
}

export async function readableStreamToJSON(stream) {
  return JSON.parse(await readableStreamToText(stream));
}

export async function readableStreamToBlob(stream) {
  return new Blob([await readableStreamToBytes(stream)]);
}
