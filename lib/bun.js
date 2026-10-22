/* bun.js: the Bun global: Bun.plugin(), Bun.file(), Bun.write(), Bun.stdin/stdout/stderr,
 * Bun.mmap(), the process properties, the utilities of lib/bun/util.js, Bun.serve(), Bun.fetch(),
 * on top of lib/node.js: Bun is Node-compatible, so this is a superset.
 * https://bun.com/docs/runtime/plugins, https://bun.com/docs/api/file-io
 * depends on: node, blob, fs, path, stream, textcode, url; plugin() needs the qjsm global
 * registerHooks, Bun.serve and Bun.fetch need qjs-lws. Members from other modules load on
 * first read (lazyGlobal), so startup stays the same.
 * rule: Bun.* keeps Bun's names, argument shapes and error fields; the roadmap is in TODO.md.
 *
 *   Bun.plugin({ name, setup(build) {} })
 *   build.onResolve({ filter, namespace? }, ({ path, importer, namespace, kind }) => { path, namespace? })
 *   build.onLoad({ filter, namespace? }, ({ path, namespace, loader }) => { contents?, loader?, exports? })
 *   build.module(specifier, () => ({ contents?, loader?, exports? }))
 *
 * loaders: js, json, text; the others (ts, tsx, jsx, toml, ...) need a
 * transpiler this engine does not have and throw TypeError.
 */

import 'node';
import { Blob } from 'blob';
import { closeSync, constants, fstatSync, mkdirSync, openSync, readFileSync, readSync, statSync, unlinkSync, writeSync, writevSync } from 'fs';
import { dirname, resolve as resolvePath } from 'path';
import { ReadableStream } from 'stream';
import { TextDecoder, TextEncoder } from 'textcode';
import { fileURLToPath } from 'url';
import lazyGlobal from 'lazyGlobal';

const resolvers = []; // { filter, namespace, callback }
const loaders = []; // { filter, namespace, callback }
const modules = new Map(); // specifier -> callback
const exportsKey = Symbol.for('qjsm.bun.exports');
const exportsStore = (globalThis[exportsKey] = new Map());
let hooks, nextExportsId = 0;

const NAMESPACE = /^([a-z][a-z0-9+.-]*):(.*)$/is;
const MODULE_SCHEME = 'bun-module';

/* url -> { namespace, path }; "file:///a/b.js" -> { 'file', '/a/b.js' },
   "yaml:./x.yaml" -> { 'yaml', './x.yaml' }, "./x.js" -> { 'file', './x.js' } */
function parseUrl(url) {
  if(url.startsWith('file://')) return { namespace: 'file', path: decodeURIComponent(url.slice(7)) };

  const m = NAMESPACE.exec(url);

  if(m && !/^(node|data|https?)$/i.test(m[1])) return { namespace: m[1], path: m[2] };

  return { namespace: 'file', path: url };
}

function toUrl(path, namespace) {
  if(namespace && namespace !== 'file') return `${namespace}:${path}`;

  return path.startsWith('/') ? 'file://' + encodeURI(path).replace(/[?#]/g, c => '%' + c.charCodeAt(0).toString(16).toUpperCase()) : path;
}

function loaderOf(path) {
  const m = /\.([a-z0-9]+)$/i.exec(path);
  const ext = m ? m[1].toLowerCase() : '';

  return ({ mjs: 'js', cjs: 'js', js: 'js', json: 'json', txt: 'text' })[ext] ?? ext;
}

function text(contents) {
  return typeof contents == 'string' ? contents : new TextDecoder().decode(contents);
}

/* an onLoad/module result as a load hook result */
function toLoadResult(result, path) {
  if('exports' in result) {
    const id = nextExportsId++;
    let source = `const e = globalThis[Symbol.for('qjsm.bun.exports')].get(${id});\n`;

    exportsStore.set(id, result.exports);

    for(const key of Object.keys(result.exports)) {
      if(key == 'default') source += 'export default e.default;\n';
      else if(/^[A-Za-z_$][\w$]*$/.test(key)) source += `export const ${key} = e[${JSON.stringify(key)}];\n`;
    }

    return { format: 'module', source, shortCircuit: true };
  }

  const loader = result.loader ?? loaderOf(path);

  switch (loader) {
    case 'js':
      return { format: 'module', source: result.contents, shortCircuit: true };
    case 'json':
      return { format: 'json', source: result.contents, shortCircuit: true };
    case 'text':
      return { format: 'module', source: `export default ${JSON.stringify(text(result.contents))};`, shortCircuit: true };
    default:
      throw new TypeError(`Bun.plugin: loader '${loader}' is not supported (for '${path}')`);
  }
}

function resolve(specifier, context, nextResolve) {
  const importer = context.parentURL ? parseUrl(context.parentURL) : { namespace: 'file', path: '' };

  if(modules.has(specifier)) return { url: `${MODULE_SCHEME}:${specifier}`, shortCircuit: true };

  for(const { filter, namespace, callback } of resolvers) {
    if(namespace !== importer.namespace || !filter.test(specifier)) continue;

    const result = callback({ path: specifier, importer: importer.path, namespace: importer.namespace, kind: 'import-statement' });

    if(result == null) continue;

    return { url: toUrl(result.path, result.namespace), shortCircuit: true };
  }

  return nextResolve(specifier, context);
}

function load(url, context, nextLoad) {
  if(url.startsWith(MODULE_SCHEME + ':')) {
    const result = modules.get(url.slice(MODULE_SCHEME.length + 1))();

    if(result != null) return toLoadResult(result, url);
  }

  const { namespace, path } = parseUrl(url);

  for(const entry of loaders) {
    if(entry.namespace !== namespace || !entry.filter.test(path)) continue;

    const result = entry.callback({ path, namespace, loader: loaderOf(path), defer: () => Promise.resolve() });

    if(result == null) continue;

    return toLoadResult(result, path);
  }

  return nextLoad(url, context);
}

function constraints(name, c, callback) {
  if(!(c?.filter instanceof RegExp)) throw new TypeError(`${name}: constraints must be { filter: RegExp, namespace?: string }`);
  if(typeof callback != 'function') throw new TypeError(`${name}: callback must be a function`);

  return { filter: c.filter, namespace: c.namespace ?? 'file', callback };
}

const build = {
  onResolve(c, callback) {
    resolvers.push(constraints('onResolve', c, callback));
    return this;
  },
  onLoad(c, callback) {
    loaders.push(constraints('onLoad', c, callback));
    return this;
  },
  module(specifier, callback) {
    if(typeof callback != 'function') throw new TypeError('module: callback must be a function');
    modules.set(specifier, callback);
    return this;
  },
  onStart() {
    return this;
  },
  onEnd() {
    return this;
  },
  onBeforeParse() {
    return this;
  },
};

/** Registers `plugin.setup(build)`; returns whatever setup returns (a Promise if async). */
export function plugin(p) {
  if(typeof p?.setup != 'function') throw new TypeError('Bun.plugin: expected { name?, setup(build) }');

  hooks ??= registerHooks({ resolve, load });

  return p.setup(build);
}

/** Removes every plugin registered so far. */
plugin.clearAll = function clearAll() {
  resolvers.length = loaders.length = 0;
  modules.clear();
  hooks?.deregister();
  hooks = undefined;
};

/* ---- Bun.file(), Bun.write(), Bun.stdin/stdout/stderr ---------------- */

const encoder = new TextEncoder();
const decoder = new TextDecoder();
const CHUNK = 65536;
const fds = new WeakMap(); // BunFile -> fd, for the fd-backed ones

/* code | errno | message of the codes that file I/O raises */
const ERRORS = {
  EPERM: [-1, 'operation not permitted'],
  ENOENT: [-2, 'no such file or directory'],
  EACCES: [-13, 'permission denied'],
  EEXIST: [-17, 'file already exists'],
  ENOTDIR: [-20, 'not a directory'],
  EISDIR: [-21, 'illegal operation on a directory'],
  EINVAL: [-22, 'invalid argument'],
  ENOTEMPTY: [-39, 'directory not empty'],
};

/* an fs error as Bun's: `ENOENT: no such file or directory, open 'x'` with code/errno/syscall/path */
function sysError(e, syscall, path) {
  const [errno, text] = ERRORS[e.code] ?? [e.errno ?? -1, e.message];
  const error = new Error(e.code == 'EISDIR' && syscall == 'read' ? 'Directories cannot be read like files' : `${e.code}: ${text}, ${syscall}${path === undefined ? '' : ` '${path}'`}`);

  return Object.assign(error, { errno, code: e.code, syscall, path });
}

function guard(syscall, path, fn) {
  try {
    return fn();
  } catch(e) {
    throw e?.code ? sysError(e, syscall, path) : e;
  }
}

const MIME = {
  txt: 'text/plain;charset=utf-8',
  html: 'text/html;charset=utf-8',
  htm: 'text/html;charset=utf-8',
  css: 'text/css;charset=utf-8',
  js: 'text/javascript;charset=utf-8',
  mjs: 'text/javascript;charset=utf-8',
  cjs: 'text/javascript;charset=utf-8',
  ts: 'text/javascript;charset=utf-8',
  json: 'application/json;charset=utf-8',
  md: 'text/markdown',
  csv: 'text/csv',
  xml: 'application/xml',
  svg: 'image/svg+xml',
  png: 'image/png',
  jpg: 'image/jpeg',
  jpeg: 'image/jpeg',
  gif: 'image/gif',
  webp: 'image/webp',
  ico: 'image/x-icon',
  pdf: 'application/pdf',
  wasm: 'application/wasm',
  zip: 'application/zip',
  gz: 'application/gzip',
};

function mimeOf(path) {
  const m = typeof path == 'string' ? /\.([a-z0-9]+)$/i.exec(path) : null;

  return (m && MIME[m[1].toLowerCase()]) || 'application/octet-stream';
}

/* the view's bytes as an ArrayBuffer; fs reads and writes want one */
function toArrayBuffer(u8) {
  return u8.byteOffset == 0 && u8.byteLength == u8.buffer.byteLength ? u8.buffer : u8.buffer.slice(u8.byteOffset, u8.byteOffset + u8.byteLength);
}

function writeAll(fd, u8) {
  let buf = toArrayBuffer(u8);

  while(buf.byteLength > 0) {
    const n = writeSync(fd, buf);

    if(n >= buf.byteLength) break;

    buf = buf.slice(n);
  }
}

/* writes all views with writev, going on after a short count */
function writevAll(fd, views) {
  while(views.length) {
    let n = writevSync(fd, views),
      i = 0;

    for(; i < views.length && n >= views[i].byteLength; i++) n -= views[i].byteLength;

    views = views.slice(i);

    if(n > 0) views[0] = views[0].subarray(n);
  }
}

function readFd(fd) {
  const chunks = [];
  let total = 0;

  for(;;) {
    const buf = new Uint8Array(CHUNK);
    const n = readSync(fd, buf.buffer, 0, CHUNK);

    if(n <= 0) break;

    chunks.push(buf.subarray(0, n));
    total += n;
  }

  const out = new Uint8Array(total);
  let pos = 0;

  for(const c of chunks) {
    out.set(c, pos);
    pos += c.length;
  }

  return out;
}

/* Bun.file(path | URL | fd): a lazy, file-backed Blob; nothing is read until a method asks.
 *
 * ```js
 * const f = Bun.file('data.json');
 * f.size; f.type; f.name; f.lastModified;
 * await f.text(); await f.json(); await f.arrayBuffer(); await f.bytes();
 * f.stream(); f.slice(0, 10); await f.exists(); await f.delete(); f.writer();
 * ```
 */
class BunFile extends Blob {
  #path; // string, or undefined for an fd
  #fd; // number, or undefined for a path
  #type;
  #start;
  #end;

  constructor(source, type, start = 0, end = Infinity) {
    super([]);

    if(typeof source == 'number') fds.set(this, (this.#fd = source));
    else this.#path = source;

    this.#type = type;
    this.#start = start;
    this.#end = end;
  }

  get name() {
    return this.#path;
  }

  get type() {
    return this.#type;
  }

  /* 0 when missing, Infinity for a stream-like fd (stdin) */
  get size() {
    const size = this.#fileSize();

    if(!Number.isFinite(size)) return size;

    return Math.max(0, Math.min(this.#end, size) - Math.min(this.#start, size));
  }

  get lastModified() {
    try {
      return Math.floor(this.#stat().mtimeMs);
    } catch {
      return 2 ** 52 - 1;
    }
  }

  #stat() {
    return this.#path === undefined ? fstatSync(this.#fd) : statSync(this.#path);
  }

  #fileSize() {
    try {
      const st = this.#stat();

      return this.#path === undefined && st.size === 0 ? Infinity : st.size;
    } catch {
      return 0;
    }
  }

  /* the (sliced) bytes; throws like Bun: ENOENT, EISDIR */
  #bytes() {
    let u8;

    if(this.#path === undefined) u8 = guard('read', undefined, () => readFd(this.#fd));
    else {
      if(guard('open', this.#path, () => statSync(this.#path).isDirectory())) throw sysError({ code: 'EISDIR' }, 'read', this.#path);

      u8 = new Uint8Array(guard('open', this.#path, () => readFileSync(this.#path)));
    }

    return this.#start || this.#end !== Infinity ? u8.subarray(this.#start, this.#end) : u8;
  }

  async bytes() {
    return this.#bytes().slice();
  }

  async arrayBuffer() {
    return toArrayBuffer(this.#bytes().slice());
  }

  async text() {
    return decoder.decode(this.#bytes());
  }

  async json() {
    return JSON.parse(await this.text());
  }

  stream() {
    const path = this.#path, end = this.#end, source = this.#fd;
    let fd, pos = this.#start;
    const close = () => {
      if(fd !== undefined && path !== undefined) closeSync(fd);
      fd = undefined;
    };

    return new ReadableStream({
      pull(controller) {
        try {
          fd ??= path === undefined ? source : guard('open', path, () => openSync(path, 'r'));

          const want = Math.min(CHUNK, end - pos);
          const buf = new Uint8Array(want > 0 ? want : 0);
          const n = want > 0 ? (path === undefined ? readSync(fd, buf.buffer, 0, want) : readSync(fd, buf.buffer, 0, want, pos)) : 0;

          if(n <= 0) {
            close();
            controller.close();
          } else {
            pos += n;
            controller.enqueue(buf.subarray(0, n));
          }
        } catch(e) {
          close();
          throw e;
        }
      },
      cancel: close,
    });
  }

  /* start/end count from the end when negative; the type defaults to this file's */
  slice(start = 0, end = Infinity, type) {
    const size = this.#fileSize();
    const norm = v => (v < 0 ? Math.max(0, (Number.isFinite(size) ? size : 0) + v) : v);
    const from = this.#start + norm(start);
    const to = end === Infinity ? this.#end : Math.min(this.#end, this.#start + norm(end));

    return new BunFile(this.#path ?? this.#fd, type || this.#type, from, to);
  }

  async exists() {
    if(this.#path === undefined) return true;

    try {
      return !statSync(this.#path).isDirectory();
    } catch {
      return false;
    }
  }

  async delete() {
    guard('unlink', this.#path, () => unlinkSync(this.#path));
  }

  unlink() {
    return this.delete();
  }

  /* a FileSink that truncates a path (or writes to an fd) */
  writer(options) {
    return new FileSink(this.#path ?? this.#fd, options);
  }
}

/* Bun.file(...).writer(): buffered incremental writer.
 *
 * ```js
 * const w = Bun.file('out.txt').writer();
 * w.write('abc'); // bytes accepted
 * w.flush();      // bytes written to the file by this call
 * w.end();        // flush and close
 * ```
 */
class FileSink {
  #fd;
  #owned;
  #chunks = [];
  #pending = 0;
  #highWaterMark;

  constructor(target, { highWaterMark = 16384 } = {}) {
    this.#owned = typeof target != 'number';
    this.#fd = this.#owned ? guard('open', target, () => openSync(target, 'w', 0o666)) : target;
    this.#highWaterMark = highWaterMark;
  }

  write(chunk) {
    const u8 = typeof chunk == 'string' ? encoder.encode(chunk) : ArrayBuffer.isView(chunk) ? new Uint8Array(chunk.buffer, chunk.byteOffset, chunk.byteLength) : new Uint8Array(chunk);

    this.#chunks.push(u8.slice());
    this.#pending += u8.length;

    if(this.#pending >= this.#highWaterMark) this.flush();

    return u8.length;
  }

  flush() {
    const total = this.#pending;
    const out = new Uint8Array(total);
    let pos = 0;

    for(const c of this.#chunks) {
      out.set(c, pos);
      pos += c.length;
    }

    this.#chunks = [];
    this.#pending = 0;

    if(total) writeAll(this.#fd, out);

    return total;
  }

  end() {
    const n = this.flush();

    if(this.#owned && this.#fd !== undefined) closeSync(this.#fd);
    this.#fd = undefined;

    return n;
  }

  ref() {}
  unref() {}
}

const fileUrl = v => (v !== null && typeof v == 'object' && v.protocol === 'file:' ? fileURLToPath(v.href) : v);

/** Bun.file(path | URL | fd, { type? }): a lazy file-backed Blob. */
export function file(source, options) {
  source = fileUrl(source);

  if(typeof source == 'number') {
    if(!Number.isInteger(source)) throw new RangeError(`The value of "fd" is out of range. It must be an integer. Received ${source}`);
  } else if(typeof source != 'string') throw new TypeError('Expected file path string or file descriptor');

  return new BunFile(source, options?.type ?? mimeOf(source));
}

/** Bun.stdin, Bun.stdout, Bun.stderr: BunFiles over fd 0, 1, 2. */
export const stdin = new BunFile(0, 'application/octet-stream');
export const stdout = new BunFile(1, 'application/octet-stream');
export const stderr = new BunFile(2, 'application/octet-stream');

/* the bytes of anything Bun.write() takes; numbers and objects are stringified like Bun */
async function toBytes(data) {
  if(data === undefined || data === null) throw new TypeError('Bun.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write');
  if(data instanceof Blob) return new Uint8Array(await data.arrayBuffer());
  if(ArrayBuffer.isView(data)) return new Uint8Array(data.buffer, data.byteOffset, data.byteLength);
  if(data instanceof ArrayBuffer) return new Uint8Array(data);
  if(typeof data.arrayBuffer == 'function') return new Uint8Array(await data.arrayBuffer());

  return encoder.encode(String(data));
}

const blobSegments = Symbol.for('qjs.blob.segments');

/* the views to write: a native Blob's segments without flattening, a
 * BunFile reads its file, anything else is one view */
async function toViews(data) {
  if(data instanceof Blob && !(data instanceof BunFile)) return data[blobSegments]();

  return [await toBytes(data)];
}

/** Bun.write(dest, data): writes to a path, URL, fd or BunFile; resolves to the bytes written. */
export async function write(dest, data) {
  const views = await toViews(data);
  const size = views.reduce((n, v) => n + v.byteLength, 0);

  dest = fileUrl(dest instanceof BunFile ? (dest.name ?? fds.get(dest)) : dest);

  if(typeof dest == 'number') writevAll(dest, views);
  else if(typeof dest == 'string') {
    guard('mkdir', dirname(dest), () => mkdirSync(dirname(dest), { recursive: true }));
    guard('open', dest, () => {
      const fd = openSync(dest, constants.O_WRONLY | constants.O_CREAT | constants.O_TRUNC, 0o666);

      try {
        writevAll(fd, views);
      } finally {
        closeSync(fd);
      }
    });
  } else throw new TypeError('Bun.write(pathOrFdOrBlob, blob) expects a Blob-y thing to write');

  return size;
}

/** Bun.mmap(path, { shared }): the file as a Uint8Array over a mapping, shared by default.
 * the file is opened read-write like Bun does, an empty file is EINVAL. */
export function mmap(path, options = {}) {
  const { mmap: map, PROT_READ, PROT_WRITE, MAP_SHARED, MAP_PRIVATE } = requireModule('mmap');
  const abs = resolvePath(fileUrl(path));
  const fd = guard('open', abs, () => openSync(abs, constants.O_RDWR));

  try {
    const { size } = fstatSync(fd);

    if(!size) throw sysError({ code: 'EINVAL' }, 'mmap');

    return new Uint8Array(map(null, size, PROT_READ | PROT_WRITE, options?.shared === false ? MAP_PRIVATE : MAP_SHARED, fd, 0));
  } finally {
    closeSync(fd);
  }
}

/* the globals Bun shares with Node (fetch, URL, Buffer, ...) come from
 * lib/node.js, Bun.serve and Bun.fetch from qjs-lws on first read */
const Bun = { plugin, file, write, stdin, stdout, stderr, mmap };

/* a read-only property computed on each read */
const getter = (name, get) => Object.defineProperty(Bun, name, { get, enumerable: true, configurable: true });

getter('env', () => process.env);
getter('argv', () => process.argv);
getter('main', () => process.argv[1] ?? '');
getter('version', () => process.versions.qjsm);
getter('revision', () => '');
getter('origin', () => '');
getter('isMainThread', () => true);
getter('enableANSIColors', () => !!process.stdout?.isTTY);

/* members loaded on first read:
 *
 * | member          | module           | export  |
 * | --------------- | ---------------- | ------- |
 * | password        | password         | default |
 * | JSONL           | jsonl            | JSONL   |
 * | JSON5, JSONC    | json5, jsonc     | same    |
 * | ArrayBufferSink | arraybuffer_sink | same    |
 * | UTIL names      | bun/util         | same    | */
const UTIL = [
  'Glob', 'allocUnsafe', 'concatArrayBuffers', 'deepEquals', 'deepMatch', 'escapeHTML', 'fileURLToPath', 'gc', 'indexOfLine', 'inspect', 'nanoseconds', 'pathToFileURL', 'peek',
  'readableStreamToArray', 'readableStreamToArrayBuffer', 'readableStreamToBlob', 'readableStreamToBytes', 'readableStreamToJSON', 'readableStreamToText',
  'sleep', 'sleepSync', 'stringWidth', 'stripANSI', 'which',
];

lazyGlobal(Bun, 'password', 'password', true, 'default');
lazyGlobal(Bun, 'JSONL', 'jsonl', true);
lazyGlobal(Bun, 'JSON5', 'json5', true);
lazyGlobal(Bun, 'JSONC', 'jsonc', true);
lazyGlobal(Bun, 'ArrayBufferSink', 'arraybuffer_sink', true);

for(const name of UTIL) lazyGlobal(Bun, name, 'bun/util', true);

lazyGlobal(Bun, 'serve', 'serve', true);
lazyGlobal(Bun, 'fetch', 'fetch', true);

export default Bun;
