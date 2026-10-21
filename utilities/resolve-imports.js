#!/usr/bin/env qjsm
import { chmodSync, closeSync, openSync, readFileSync, renameSync, sizeSync, statSync, unlinkSync, writeSync } from 'fs';
import * as os from 'os';
import * as path from 'path';
import { curry, define, difference, error, getOpt, getset, getTypeName, intersection, isObject, mapWrapper, memoize, nonenumerable, quote, randInt, toArrayBuffer, toString, types, unique, } from 'util';
import { Console } from 'console';
import extendArray from 'extendArray';
import extendArrayBuffer from 'extendArrayBuffer';
import inspect from 'inspect';
import { Lexer, Token } from 'lexer';
import ECMAScriptLexer from 'lexer/ecmascript.js';
import { merge, parse, print } from 'module-lexer';
import * as std from 'std';

/* äöü */
const inspectSymbol = Symbol.for('quickjs.inspect.custom');

class ResolveError extends Error {
  constructor(imp, importer) {
    const { loc, file, code } = imp;
    const where = loc ? `${loc.file}:${loc.line}:${loc.column}` : importer;

    super(`${where}: cannot resolve module '${file}'` + (code ? `\n    ${String(code).trim()}` : '') + (importer && loc && loc.file != importer ? `\n    imported from '${importer}'` : ''));
    this.stack = null;
  }
}

class ArgumentError extends Error {
  constructor(...args) {
    super(...args);
    this.stack = null;
  }
}

let globalSheBang,
  buffers = {},
  modules = {},
  showTiming,
  removeExports = false,
  onlyImports = false,
  globalExports = 0,
  showDeps = 0,
  removeImports = false,
  removeComments = false,
  readPackage = false,
  userScript = false,
  relativeTo,
  outputFile,
  recursive,
  debug = 0,
  processed = new Set(),
  bufferRef = new WeakMap(),
  fileBuffers = new Map(),
  fileMaps = new Map(),
  dependencyMap = new Map(),
  printFiles,
  printImports,
  quiet = false,
  importsFor = {},
  logFile = () => {},
  log = () => {};

Object.assign(globalThis, {
  buffers,
  fileBuffers,
  fileMaps,
  modules,
  ImportedBy,
  importsFor,
  allImports() {
    const r = [];

    for(let source in importsFor) {
      let imports = importsFor[source];
      r.push(...imports);
    }

    return [...new Set(r)];
  },
});

const header = (globalThis.header = []),
  footer = (globalThis.footer = []);

const dependencyTree = memoize(arg => [], dependencyMap);

const bufferMap = getset(bufferRef);
let identifiersUsed;

const STDIN_NAME = '<stdin>.js';

function ReadJSON(filename) {
  let data = readFileSync(filename, 'utf-8');
  return data ? JSON.parse(data) : null;
}

const ReadPackageJSON = memoize(() => {
  let ret = ReadJSON('package.json');

  if(typeof ret != 'object' || ret == null) ret = { _moduleAliases: {} };
  ret._moduleAliases ??= {};
  return ret;
});

/* the files that import `importFile`, by specifier or by the path it resolves to */
function ImportedBy(importFile) {
  const r = [],
    target = path.resolve(importFile);

  for(let source in importsFor) {
    const dir = path.dirname(source);

    if(importsFor[source].some(imp => typeof imp.file == 'string' && (imp.file == importFile || path.resolve(dir, imp.file) == target))) r.push(source);
  }

  return r;
}

function ResolveAlias(filename) {
  if(typeof filename == 'string') {
    let key = path.resolve(filename);
    let { _moduleAliases } = ReadPackageJSON();

    if(key in _moduleAliases) return _moduleAliases[key];
  }

  return filename;
}

function IsWhitespace(str) {
  if(types.isArrayBuffer(str)) str = toString(str);
  return str.trim() == '';
}

function IsFileImport(filename) {
  if(readPackage) filename = ResolveAlias(filename);
  if(/(\.|\/)/.test(filename)) return filename;
}

function NormalizePath(p) {
  p = path.absolute(p);
  p = path.normalize(p);
  p = path.relative(path.getcwd(), p);
  p = path.resolve(p);

  if(!path.isAbsolute(p)) if (!p.startsWith('./') && !p.startsWith('../') && p != '..') p = './' + p;

  return p;
}

/* a function from a UTF-16 index of text to the byte offset in its UTF-8
 * encoding of byteLength bytes, the identity for ASCII text */
function ByteOffsets(text, byteLength) {
  if(text.length == byteLength) return i => i;

  const map = new Uint32Array(text.length + 1);
  let b = 0;

  for(let i = 0; i < text.length; i++) {
    const c = text.charCodeAt(i);

    map[i] = b;
    b += c < 0x80 ? 1 : c < 0x800 ? 2 : (c & 0xfc00) == 0xd800 ? 4 : (c & 0xfc00) == 0xdc00 ? 0 : 3;
  }

  map[text.length] = b;
  return i => map[i];
}

/* a record of module-lexer's parse() as the tool uses it: range in bytes
 * and over a following ;, file the specifier after -M, code and loc for
 * ResolveError, names [local, exported] for -G */
function Statement(record, text, toByte, source, depth) {
  const [start, e] = record.range,
    end = e + (/^[ \t]*;/.exec(text.slice(e, e + 80))?.[0].length ?? 0);
  const column = start - Math.max(text.lastIndexOf('\n', start - 1), text.lastIndexOf('\r', start - 1));

  return {
    ...record,
    file: RemapModule(record.path),
    range: [toByte(start), toByte(end)],
    code: text.slice(start, end),
    loc: { file: source, line: record.line, column },
    source,
    depth,
    names: [],
  };
}

const FileBannerComment = (filename, i) => {
  let s = '';
  s += ` ${i ? 'end' : 'start'} of '${/*path.basename*/ filename}' `;
  let n = Math.max(0, Math.floor((80 - 6 - s.length) / 2));
  s = '/* ' + '-'.repeat(n) + s;
  s += '-'.repeat(Math.max(0, 80 - 3 - s.length)) + ' */';
  if(i == 0) s = '\n' + s + '\n';
  else s = s + '\n';
  return s;
};

extendArray(Array.prototype);
extendArrayBuffer();

const IsBuiltin = moduleName => /^[^\/.]+$/.test(moduleName);

const compact = (n, more = {}) =>
  console.config({
    compact: n,
    maxArrayLength: 100,
    ...more,
  });
const AddUnique = (arr, item) => (arr.indexOf(item) == -1 ? arr.push(item) : null);

const IsOneOf = curry((n, value) => (Array.isArray(n) ? n.some(num => num === value) : n === value));
const TokIs = curry((type, lexeme, tok) => {
  if(tok != undefined) {
    if(lexeme != undefined) if (typeof lexeme == 'string' && !IsOneOf(lexeme, tok.lexeme)) return false;

    if(type !== undefined) {
      if(typeof type == 'string' && !IsOneOf(type, tok.type)) return false;
      if(typeof type == 'number' && !IsOneOf(type, tok.id)) return false;
    }

    return true;
  }
});
const CompareRange = (a, b) => (a === null || b === null ? 0 : typeof a[0] == 'number' && typeof b[0] == 'number' && a[0] != b[0] ? a[0] - b[0] : a[1] - b[1]);

const PutsFunction = outFn => str => {
  const b = types.isArrayBuffer(str) ? str : toArrayBuffer(str);
  return outFn(b, b.byteLength);
};

const IsWhiteSpace = str => /^\s*$/.test(str) || str.trim() == '';

const debugLog = (str, ...args) => {
  const pred = arg => isObject(arg) && 'compact' in arg;
  let opts = args.filter(pred);

  opts = opts.reduce((acc, opt) => define(acc, opt), opts.shift() ?? {});

  if(opts.compact === undefined) define(opts, compact(1));
  if(opts.maxArrayLength === undefined) define(opts, { maxArrayLength: 10 });
  if(opts.depth === undefined) define(opts, { depth: Infinity });

  args = args.filter(arg => !pred(arg));

  opts.compact = false;

  console.log(str, opts, ...args);
};

function FileWriter(file, mode = 0o755) {
  const fd = openSync(file, 'w', mode);

  if(debug > 2) console.debug('FileWriter', `opened '${file}'`);

  return define(FdWriter(fd, file), {
    close: () => {
      closeSync(fd);
      console.debug('FileWriter', `closed '${file}' [${sizeSync(file)} bytes]`);
    },
  });
}

/* writes `file`.new, then renames it over `file` (atomic; the original keeps its mode) */
function FileReplacer(file) {
  let mode = 0o644;

  try {
    mode = statSync(file).mode & 0o7777;
  } catch(e) {}

  const temp = file + '.new';
  const fd = openSync(temp, 'w', mode);

  return define(FdWriter(fd, file), {
    close: () => {
      closeSync(fd);

      const size = sizeSync(temp);

      try {
        chmodSync(temp, mode);
        renameSync(temp, file);
      } catch(error) {
        try {
          unlinkSync(temp);
        } catch(e) {}

        throw new Error(`FileReplacer renameSync() error: ${error.message}`);
      }

      console.log(`${file} written (${size} bytes)`);
    },
  });
}

function FdWriter(fd, file) {
  const fn = (buf, len) => {
    if(!types.isArrayBuffer(buf)) buf = toArrayBuffer(buf);
    len ??= buf.byteLength;
    return writeSync(fd, buf, 0, len);
  };

  define(fn, {
    fd,
    file,
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    [Symbol.toStringTag]: `FileWriter< ${fd} >`,
    inspect() {
      return inspect({ fd }) ?? this[Symbol.toStringTag];
    },
  });

  return fn;
}

function StringWriter() {
  let s = '';

  const fn = (buf, len) => {
    len ??= buf.byteLength;
    if(types.isArrayBuffer(buf)) buf = toString(buf.slice(0, len));
    s += buf;
    return len;
  };

  define(fn, {
    get str() {
      return s;
    },
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    [Symbol.toStringTag]: `StringWriter`,
  });

  return fn;
}

function ArrayWriter(
  arr,
  t = (buf, len) => {
    if(!types.isArrayBuffer(buf)) buf = toArrayBuffer(buf);
    return buf.slice(0, len ?? buf.byteLength);
  },
) {
  const fn = (buf, len) => (arr.push(t(buf, len)), len);

  define(fn, {
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    [Symbol.toStringTag]: `ArrayWriter`,
    inspect() {
      return inspect({ arr }) ?? this[Symbol.toStringTag];
    },
  });

  return fn;
}

function DummyWriter(name) {
  const fn = (buf, len) => len;

  define(fn, {
    name,
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    [Symbol.toStringTag]: `DummyWriter< ${name} >`,
    inspect() {
      return inspect({ fd }) ?? this[Symbol.toStringTag];
    },
  });

  return fn;
}

/* module name -> absolute file or directory, from -M NAME=PATH */
const moduleMap = new Map(),
  moduleMapSpecs = [];

function AddModuleMap(spec) {
  const eq = spec.indexOf('=');

  if(eq <= 0 || eq == spec.length - 1) throw new ArgumentError(`--module-map expects NAME=PATH, got '${spec}'`);

  const name = spec.slice(0, eq),
    file = path.resolve(spec.slice(eq + 1));

  if(!path.exists(file)) throw new ArgumentError(`--module-map ${name}: '${spec.slice(eq + 1)}' does not exist`);

  moduleMap.set(name, file);
}

/* an import specifier equal to a mapped NAME (or NAME/sub/path for a mapped directory)
   becomes the mapped path, so it is resolved and bundled like any other file import */
function RemapModule(name) {
  if(typeof name != 'string' || moduleMap.size == 0) return name;

  if(moduleMap.has(name)) return moduleMap.get(name);

  for(const [from, to] of moduleMap) if(name.startsWith(from + '/') && path.isDirectory(to)) return path.join(to, name.slice(from.length + 1));

  return name;
}

/* the entry file named by a package.json `exports` value: a path, a list of
   alternatives, or an object with "." and/or condition keys (import, default, ...) */
function PackageEntry(exp) {
  if(typeof exp == 'string') return exp;

  if(Array.isArray(exp)) {
    for(const e of exp) {
      const r = PackageEntry(e);
      if(r) return r;
    }
  } else if(exp && typeof exp == 'object') {
    for(const key of ['.', 'import', 'module', 'default', 'require']) {
      const r = key in exp ? PackageEntry(exp[key]) : undefined;
      if(r) return r;
    }
  }
}

class ModuleLoader {
  static paths = [];

  static {
    if(path.isFile('package.json')) this.paths.add('node_modules');
  }

  static resolve(module) {
    let file;

    if(!path.isFile(module) && !path.isAbsolute(module)) {
      for(let p of this.paths) {
        if(module.startsWith(p)) continue;

        if((file = ModuleLoader.resolve(path.join(p, module)))) return file;
      }

      if(!/\.js$/.test(module) && path.isFile(module + '.js')) file = module + '.js';
    }

    if(!file && path.isDirectory(module)) {
      if(path.isFile(module + '/package.json')) {
        const { exports, main } = JSON.parse(readFileSync(module + '/package.json', 'utf-8'));

        /* exports, then main, then index.js */
        for(const entry of [PackageEntry(exports), main]) {
          if(typeof entry != 'string') continue;

          const candidate = path.join(module, entry);

          if(path.isFile(candidate)) return candidate;
          if(path.isFile(candidate + '.js')) return candidate + '.js';
        }
      }

      if(path.isFile(module + '/index.js')) return path.join(module, 'index.js');
    }

    if(!file && path.exists(module)) file = module;

    if(debug > 3) console.log('ModuleLoader.resolve', compact(true), { module, file });

    if(file && path.isFile(file)) return file;
  }
}

function ModuleExports(file) {
  let m;
  try {
    if((m = moduleList.find(m => new RegExp(file).test(getModuleName(m)))) || (m = loadModule(file))) {
      let list = getModuleExports(m);
      return Object.keys(list);
    }
  } catch(error) {
    console.error('ERROR', error.message + '\n' + error.stack);
  }
}

function AddDep(source, file) {
  source = NormalizePath(source);

  if(debug > 2) console.log(`Add dependency '${file}' to '${source}'`);
  dependencyTree(source).push(file);
}

function LeadingWS(str) {
  let m = str.match(/[^\s]/);
  return m?.index ?? 0;
}

function Unquote(lexeme) {
  return lexeme.replace(/(^[\'\"\`]|[\'\"\`]$)/g, '');
}

function LiteralSequence(tokens) {
  return tokens.reduce((acc, tok) => acc + tok.lexeme, '');
}

function UntilEOL(idx, tokens) {
  return Until(idx, tokens, TokIs(null, ';'));
}

function Until(idx, tokens, pred = TokIs(null, [';', '\n'])) {
  let i;
  for(i = idx; tokens[i]; i++) {
    if(pred(tokens[i])) break;
  }

  return tokens.slice(idx, i);
}

function WholeLine(idx, tokens) {
  let i, j;
  for(i = idx; tokens[i]; i++) {
    if([';', '\n'].indexOf(tokens[i].lexeme) != -1) break;
  }

  for(j = idx; j > 0; j--) {
    const tok = tokens[j - 1];
    if(!tok || [';', '\n'].indexOf(tokens[j - 1].lexeme) != -1) break;
  }

  return tokens.slice(j, i);
}

function Range(file, start, end) {
  let buf;
  if((buf = BufferFile(file))) {
    this.file = file;
    this.start = start ?? 0;
    this.end = end ?? buf.byteLength;
    return this;
  }

  return null;
}

Range.prototype.toString = function() {
  let buf = BufferFile(this.file);
  return buf.slice(this.start, this.end);
};

function InRange([start, end], i) {
  if(i >= start && i < end) return true;
  return false;
}

function IsRange(obj) {
  return isObject(obj) && ('length' in obj || 'start' in obj || obj instanceof NumericRange);
}

Object.assign(Range, {
  merge(...args) {
    return new Range(args[0].file, Math.min(...args.map(r => (Array.isArray(r) ? r[0] : r.start))), Math.max(...args.map(r => (Array.isArray(r) ? r[1] : r.end))));
  },
});

class NumericRange extends Array {
  constructor(start, end) {
    super(2);
    if(isNaN(+start)) start = 0;
    this[0] = +start;
    if(isNaN(+end)) end = this[0];
    this[1] = end;
  }

  get start() {
    return this[0];
  }

  set start(value) {
    this[0] = +value;
  }

  get end() {
    return this[1];
  }

  set end(value) {
    this[1] = +value;
  }

  get size() {
    return this[1] - this[0];
  }

  clone() {
    return new NumericRange(...this);
  }

  inside(n) {
    let [s, e] = this;
    return n >= s && n < e;
  }

  static from(range) {
    if(isObject(range) && range instanceof NumericRange) return range;

    try {
      let r = new NumericRange(...range);
      range = r;
    } catch(e) {}

    console.log('NumericRange.from', range);
    return range;
  }

  static *holes(ranges, only = false) {
    let prev = [0, 0];
    let i = -1;
    for(let range of ranges) {
      if(IsRange(range)) {
        range = [...range];
        console.log('range#' + ++i, inspect(range));
        if(IsRange(prev) && IsRange(range)) {
          let [start, end] = range;
          if(start >= prev[1]) yield new NumericRange(prev[1], start);
        }

        if(!only) {
          if(!(isObject(range) && range instanceof NumericRange)) range = new NumericRange(...range);
          yield range;
        }
      }

      prev = range;
    }
  }

  static between([s1, e1], [s2, e2]) {
    if(s2 > e1) return [e1, s2];
    if(s1 > e2) return [e2, s1];
  }
}

define(
  NumericRange.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'NumericRange',
    [inspectSymbol](depth, opts) {
      const [start, end] = this;
      let s = '';
      const pad = s => (s + '').padEnd(5);
      s += `\x1b[1;36m${pad(start)}\x1b[0m`;
      s += ` - `;
      s += `\x1b[1;36m${pad('+' + (end - start))}\x1b[0m`;
      s = `[ ${s} ]`;
      return s;
    },
  }),
);

class FileMap extends Array {
  static buffers = mapWrapper(fileBuffers);
  static filenames = mapWrapper(bufferRef);
  static serials = mapWrapper(new WeakMap());

  constructor(file, buf) {
    super();
    if(typeof file != 'number') {
      this.file = file;
      buf ??= BufferFile(file);
      if(!buf) throw new Error(`FileMap buf == ${buf}`);
      this.push([[0, buf.byteLength], buf]);
      fileMaps.set(file, this);
    }
  }

  reset() {
    const { length, buffer } = this;
    this.splice(0, length, [[0, buffer.byteLength], buffer]);
  }

  get buffer() {
    return FileMap.buffers(path.resolve(this.file));
  }

  static empty(file) {
    if(typeof file == 'string') file = FileMap.for(file);
    if(isObject(file) && file instanceof FileMap) return file.isEmpty();
  }

  isEmpty() {
    return false;
  }

  static for(file, buf) {
    let m;
    if(file && (m = fileMaps.get(file))) return m;
    if(isObject(file) && file instanceof FileMap) return file;
    else if(file === null && !buf) {
      let obj = {
        isEmpty() {
          return true;
        },
        toString() {
          return '';
        },
        [Symbol.toStringTag]: 'FileMap(empty)',
      };
      return Object.setPrototypeOf(obj, FileMap.prototype);
    }

    if(fileMaps.has(file)) return fileMaps.get(file);
    buf ??= FileMap.buffers(path.resolve(file));
    return new FileMap(file, buf);
  }

  findSlice(pos) {
    const n = this.length;
    for(let i = 0; i < n; i++) {
      let range;
      if((range = this.ranges[i])) {
        range = [...range];
        if(InRange([...range], pos)) return i;
      }
    }

    //  return -1;
    return this.findIndex(([range, buf]) => range && InRange([...range], pos));
  }

  splitAt(pos) {
    let i;
    if((i = this.findPos(pos)) != -1) {
      const [range, buf] = this[i];
      const [start, end] = range;

      if(start === pos) return i;

      this.splice(i + 1, 0, [new NumericRange((range[1] = pos), end), buf]);
      return i + 1;
    }

    return -1;
  }

  findPos(pos) {
    const n = this.length;

    for(let i = 0; i < n; i++) {
      const [range, buf] = this[i];
      const next = this[i + 1];

      if(range) {
        let [start, end] = range;
        if(pos < end) return i;
      }
    }

    return -1;
  }

  insertAt(pos, ...args) {
    let i;
    const entries = args.map(file => (types.isArrayBuffer(file) ? [null, file] : [null, null, file]));

    /*    if(pos <= this.rangeAt(0)[0]) {
      this.unshift(...entries);
      return 0;
    }*/

    if((i = this.splitAt(pos)) != -1) {
      this.splice(i, 0, ...entries);
      return i;
    }
  }

  insert(index, file) {
    let entry = types.isArrayBuffer(file) ? [null, file] : [null, null, file];
    this.splice(index, 0, entry);
    return index;
  }

  replaceRange(range, file) {
    const { buffer } = this;

    if(Array.isArray(range) && !(range instanceof NumericRange)) range = new NumericRange(...range);
    const sliceIndex = n => {
      let i, r, item;

      if(this[0] && this[0][0] != null) {
        const range = new NumericRange(...this[0][0]);
        if(n < range.start) return 0;
      }

      const { length } = this;

      for(i = 0; i < length; i++) {
        if(!types.isArrayBuffer(this[i][1])) continue;

        if(this[i] && (item = this[i][0])) {
          const [start, end] = item;
          if(n < end) break;
        }
      }

      return i;
    };
    //if(debug > 1) console.log('FileMap.replaceRange', compact(2, { customInspect: true }), { file, range: [range[0], range[1]] });

    let i = sliceIndex(range[0]);
    let j = sliceIndex(range[1]);

    /* nothing of the range is left to replace: it lies inside text that was replaced before */
    if(i >= this.length && this.length && this[this.length - 1][0] && range[0] >= this[this.length - 1][0][1]) return;
    if(this[i] && this[i][0] && range[0] < this[i][0][0] && range[1] <= this[i][0][0]) return;

    if(!this[i] || !this[i][0])
      throw new Error(`range=${range}\nlength=${this.length}\nstart=${i}\nend=${j}\nthis[${i}]=${inspect(this[i])}\nthis[${i - 1}]=${inspect(this[i - 1])}\nthis[${i + 1}]=${inspect(this[i + 1])}`);

    /* a range starting in already replaced text begins at the next buffer chunk */
    if(range[0] < this[i][0][0]) range[0] = this[i][0][0];

    const [, buf] = this[i];
    const empty = typeof file != 'string';
    const entry = [range, null].concat(empty ? [] : [file]);
    let at;

    if(range[0] > this[i][0][0]) {
      /* starts inside chunk i: keep its head, and its tail too when the range ends in it */
      if(i == j) {
        const [r] = this[i];

        this.splice(++j, 0, [new NumericRange(...r), buf]);
      }

      this[i][0][1] = range[0];
      at = i + 1;
    } else {
      /* starts at the chunk's start: chunk i goes entirely */
      at = i;
    }

    /* everything between the kept head and the chunk the range ends in is replaced */
    this.splice(at, j - at);

    const tail = this[at];

    if(tail && tail[0] && types.isArrayBuffer(tail[1])) {
      tail[0][0] = Math.max(tail[0][0], range[1]);

      if(tail[0][0] >= tail[0][1]) this.splice(at, 1);
    }

    if(!empty) this.splice(at, 0, entry);
  }

  stringAt(n) {
    const arr = [];
    const wr = ArrayWriter(arr, (buf, len) => toString(buf, len));
    const item = this.at(n) ?? '';

    if(this[n][1] === null && this[n][2] === '') wr.puts('');
    else if(isObject(item) && 'write' in item) item.write(wr);
    else if(typeof item == 'string') wr.puts(item);
    else wr.write(item);

    return arr.join('');
  }

  rangeAt(n) {
    if(n in this) {
      let r;

      if(this[n][0]) r = this[n][0];
      else if(types.isArrayBuffer(this[n][1])) {
        let start = n > 0 ? this.rangeAt(n - 1)[1] : 0;
        let end = start + this[n][1].byteLength;
        r = [start, end];
      } else {
        r = [n > 0 ? this.rangeAt(n - 1)[1] : 0, n + 1 < this.length ? this.rangeAt(n + 1)[0] : Infinity];
      }

      return r ? new NumericRange(...r) : null;
    }
  }

  get ranges() {
    const r = [];
    const { length } = this;

    for(let i = 0; i < length; i++) r.push(this[i][0] === null ? null : this.rangeAt(i));

    return r;
  }

  *[Symbol.iterator]() {
    const { length } = this;

    for(let i = 0; i < length; ++i) yield this.at(i);
  }

  dump() {
    const source = this.file;
    return (
      `FileMap {\n\tfile: \x1b[38;5;215m${source},\n\t\x1b[0m[ ` +
      [...this]
        .map((item, i) => item.concat([this.at(i)]))
        .reduce((acc, [range, buf, str], i) => {
          let s = acc + `\n\t\t[ `;
          s += ('' + (range ? '[' + NumericRange.from(range) + ']' : range)).padEnd(10);
          s += ', ';

          if(isObject(buf) && 'byteLength' in buf) {
            let filename = bufferRef.get(buf);
            buf = filename ?? `<this>`;
          }

          if(typeof buf == 'string') buf = path.resolve(buf);

          s += inspect(buf, { maxArrayLength: 30 });
          s += ` ],`;

          return s;
        }, '') +
      `\n\t]\n}`
    );
  }

  at(i) {
    if(!this[i]) return null;
    let [range, buf, file] = this[i];
    //console.log(`at(${i})`, { range, buf, file });

    if(types.isArrayBuffer(buf)) return buf.slice(...(range ?? []));

    if(file === '') file = this.file;

    if(buf == null && typeof file == 'string') {
      if(!path.isFile(file)) throw Error(`FileMap\x1b[1;35m<${this.file}>\x1b[0m Inexistent file '${file}'`);
      return FileMap.for(file);
    }
  }

  toArray() {
    return this.map((s, i) => {
      s = this.at(i);

      if(isObject(s) && 'toArray' in s) s = s.toArray();

      return s;
    });
  }

  holes() {
    const ranges = [...this.map(([range]) => range)].sort(CompareRange);
    const holes = [...NumericRange.holes(ranges, true)];
    const { length } = holes;

    for(let i = 0; i < length; i++) {
      const hole = holes[i];
      const [range] = this[i];
      console.log('#' + (i + 1), compact(2), inspect({ hole, range }, { compact: 2, depth: Infinity }));
    }

    return holes;
  }

  firstChunk() {
    return this.findIndex(([range, buf], i) => range && buf && !IsWhiteSpace(toString(buf.slice(...range))));
  }

  lastChunk() {
    return this.findLastIndex(([range, buf], i) => range && buf && !IsWhiteSpace(toString(buf.slice(...range))));
  }

  write(out, depth = 0, serial) {
    if(debug > 3)
      debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, compact(1), {
        out,
        depth,
        serial,
      });
    let r,
      written = 0;

    const { length } = this;
    serial ??= randInt(0, 1000);

    if(FileMap.serials(this) === serial) return 0;
    FileMap.serials(this, serial);

    if(typeof out == 'string') out = FileWriter(out);
    const first = this.firstChunk();
    const last = this.lastChunk();

    if(debug > 3)
      debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, compact(2), {
        chunks: [this.at(first), this.at(last)],
      });

    for(let i = 0; i < length; i++) {
      let str = this.at(i);

      if(str === null || str === undefined) continue;

      let len = str.byteLength ?? str.length;

      if(isObject(str)) {
        if(str instanceof FileMap) {
          if(str.serial === serial) {
            r = 0;
            continue;
          }

          r = str.write(out, depth + 1, serial);
        } else {
          if(depth > 0 && i == first) out.puts(FileBannerComment(this.file, 0));

          r = out.write(str, len);

          if(depth > 0 && i == last) out.puts(FileBannerComment(this.file, 1));
        }
      } else {
        let type = getTypeName(str);
        console.log('invalid type:', type);
        throw new Error(type);
      }

      if(debug > 3) debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, `[${i + 1}/${length}]`, `result=${r}`);

      if(r < 0) {
        let err = error();
        throw new Error(`error writing (fd=${out.fd}) ${len} bytes: ` + inspect(err));
        break;
      }

      written += r;
    }

    return written;
  }

  trim() {
    const { length } = this;

    for(let i = 0; i < length; i++) {
      let s = this.stringAt(i);

      if(IsWhitespace(s)) {
        this[i][1] = null;
        this[i][2] = '';
      }
    }
  }

  toString() {
    const { length } = this;
    let s = '';

    for(let i = 0; i < length; i++) s += this.stringAt(i);

    return s;
  }

  [Symbol.inspect](depth, opts) {
    const nl = '\n' + '  '.repeat(depth);
    return `\x1b[1;31mFileMap\x1b[1;35m<${this.file}>\x1b[0m [` + nl + this.map(el => inspect(el, { ...opts, compact: true, maxArrayLength: 16 })).join(',' + nl) + nl.slice(0, -2) + '}';
  }
}

FileMap.prototype[Symbol.toStringTag] = 'FileMap';

function BufferFile(file, buf) {
  file = path.resolve(file);

  if(!buf) buf = fileBuffers.get(file) ?? buffers[file];

  if(!buf) buf = buffers[file] ?? readFileSync(file, { flag: 'r' });

  if(typeof buf == 'object' && buf !== null) {
    bufferRef.set(buf, file);
    fileBuffers.set(file, buf);
    buf.file = file;
  }

  return buf;
}

function BufferLengths(file) {
  return buffers[file].map(b => b.byteLength);
}

function BufferOffsets(file) {
  return buffers[file].reduce(([pos, list], b) => [pos + b.byteLength, list.concat([pos])], [0, []])[1];
}

function BufferRanges(file) {
  return buffers[file].reduce(([pos, list], b) => [pos + b.byteLength, list.concat([[pos, b.byteLength]])], [0, []])[1];
}

function WriteFile(name, data, opts = {}) {
  const { mode = 0o644, verbose = true } = opts;

  if(typeof data == 'string') data = toArrayBuffer(data);

  if(types.isArrayBuffer(data)) data = [data];

  let fd = openSync(name, 'w', mode);
  let r = 0;

  for(let item of data) r += writeSync(fd, types.isArrayBuffer(item) ? item : toArrayBuffer(item + ''));

  closeSync(fd);
  let stat = statSync(name);

  if(verbose) console.log(`Wrote ${name}: ${stat?.size} bytes`);

  return stat?.size;
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;
  return 'Lexer ' + inspect({ start, pos, size });
}

function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;
  return `★ Token ${inspect({ chars, offset, length, loc }, { depth: 1 })}`;
}

function* DependencyTree(root, indent = ' ', spacing = false, depth = 0, pre = '', fn = (name, depth) => `${name} (${depth})`) {
  if(depth == 0) yield pre + stripLeadingDotSlash(fn(root, depth)) + `\n`;

  /* AddDep() keys by NormalizePath(source); a file nothing was recorded for has no dependencies */
  const a = dependencyMap.get(NormalizePath(root)) ?? [];
  const { length } = a;

  for(let i = 0; i < length; i++) {
    if(spacing) yield (pre + indent + `│  ` + '\n').repeat(Number(spacing));

    yield pre + indent + (i < length - 1 ? `├─ ` : `└─ `) + stripLeadingDotSlash(fn(a[i], depth + 1)) + '\n';

    yield* DependencyTree(a[i], indent, spacing, depth + 1, pre + indent + (i < length - 1 ? `│  ` : `   `));
  }

  function stripLeadingDotSlash(n) {
    if(n.startsWith('./')) n = n.slice(2);
    return n;
  }
}

function SpreadAndJoin(iterator, separator = '') {
  return [...iterator].join(separator);
}

function* PrintUserscriptBanner(fields) {
  const defaults = {
    name: 'https://github.com/rsenn',
    namespace: 'https://github.com/rsenn',
    version: '1.0',
    description: '',
    author: 'Roman L. Senn',
    match: 'http*://*/*',
    icon: 'data:image/gif;base64,R0lGODlhAQABAAAAACH5BAEKAAEALAAAAAABAAEAAAICTAEAOw==',
    grant: 'none',
  };

  const keys = Object.keys(defaults).concat(Object.keys(fields)).unique();
  yield `// ==UserScript==`;

  for(let name of keys) {
    const value = fields[name] ?? defaults[name];

    yield `// @${name.padEnd(12)} ${value}`;
  }

  yield `// ==/UserScript==`;
}

function main(...args) {
  Object.assign(globalThis, {
    ProcessFile,
    IsBuiltin,
    toString,
    LeadingWS,
    FileMap,
    NumericRange,
    ArrayWriter,
    DummyWriter,
    FdWriter,
    StringWriter,
    FileWriter,
    ResolveAlias,
    Range,
    ModuleLoader,
  });

  globalThis.console = new Console(std.err, {
    inspectOptions: {
      colors: true,
      depth: 2,
      stringBreakNewline: true,
      maxStringLength: 1000,
      maxArrayLength: 100,
      compact: 1,
      hideKeys: [Symbol.toStringTag /*, 'code'*/],
    },
  });
  let optind = 0,
    code = 'c',
    exp = true;

  let out,
    stream = define(
      { indent: 0 },
      {
        lines: [],
        write(buf, len) {
          let s = toString(buf, 0, len);
          return this.puts(s);
        },
        puts(s) {
          let pad = '  '.repeat(this.indent);
          this.lines.push(...s.split(/\r?\n/g).map(line => pad + line));
          return s.length;
        },
        close() {
          let prev = '';
          const isWS = s => s.trim() == '';
          for(let line of this.lines) {
            if(!(isWS(line) && isWS(prev))) {
              out.puts(line.trimEnd() + '\n');
            }

            prev = line;
          }

          out.close();
        },
      },
    );

  let opts;
  const params = (globalThis.params = getOpt(
    (opts = {
      help: [false, null, 'h'],
      debug: [false, () => ++debug, 'x'],
      interactive: [false, null, 'y'],
      log: [true, file => (logFile = FileWriter(file)), 'O'],
      verbose: [false, () => ++verbose, 'v'],
      sort: [false, null, 's'],
      'case-sensitive': [false, null, 'c'],
      quiet: [false, () => (quiet = (quiet | 0) + 1), 'q'],
      export: [false, () => (exp = true), 'e'],
      imports: [false, () => (onlyImports = true), 'i'],
      //'relative-to': [true, arg => (relativeTo = path.absolute(arg)), 'R'],
      output: [true, file => (outputFile = file), 'o'],
      flatten: [false, null, 'f'],
      merge: [false, null, 'm'],
      recursive: [false, () => (recursive = true), 'r'],
      'no-recursive': [false, () => (recursive = false), 'R'],
      'remove-exports': [false, () => (removeExports = true), 'E'],
      'remove-imports': [false, () => (removeImports = true), 'I'],
      'check-imports': [false, () => (scriptArgs[0] = 'check-imports'), null],
      'list-imports': [false, () => (scriptArgs[0] = 'list-imports'), null],
      'remove-comments': [false, () => (removeComments = true), 'C'],
      'global-exports': [false, () => ++globalExports, 'G'],
      'show-dependencies': [false, () => ++showDeps, 'd'],
      'print-imports': [false, () => (printImports = true), null],
      'print-files': [false, () => (printFiles = true), 'l'],
      'no-print-files': [false, () => (printFiles = false), 'L'],
      'module-map': ['NAME=PATH', spec => moduleMapSpecs.push(spec), 'M'],
      'read-package': [false, () => (readPackage = true), 'p'],
      'no-read-package': [false, () => (readPackage = false), 'P'],
      userscript: [false, () => (userScript = true), 'U'],
      'remove-unused': [false, null, 'u'],
      time: [false, () => (showTiming = true), 't'],
      '@': 'files',
    }),
    args,
  ));

  //if(debug > 1) console.log('main', { debug, logFile, quiet, exp, onlyImports, relativeTo, outputFile, recursive, removeExports, removeImports, removeComments, globalExports, showDeps, printImports, printFiles, readPackage, userScript, showTiming, });

  function ShowHelp() {
    let entries = Object.entries(opts);
    let maxlen = entries.reduce((acc, [name]) => (acc > name.length ? acc : name.length), 0);

    let s = Object.entries(opts).reduce(
      (acc, [name, [hasArg, fn, shortOpt]]) =>
        acc + (`    ${(shortOpt ? '-' + shortOpt + ',' : '').padStart(4, ' ')} --${name.padEnd(maxlen, ' ')} ` + (hasArg ? (typeof hasArg == 'boolean' ? 'ARG' : hasArg) : '')).padEnd(40, ' ') + '\n',
      `Usage: ${path.basename(scriptArgs[0])} [OPTIONS] <FILES...>\n\n`,
    );
    std.puts(s + '\n');
    std.exit(0);
  }

  if(!debug) if (process.env.DEBUG) debug = +process.env.DEBUG;

  if(params.help) ShowHelp();

  /* validated here: an exception thrown from inside the option handler would be swallowed */
  for(const spec of moduleMapSpecs) AddModuleMap(spec);

  let doOutput = true;

  let files = params['@'];

  /* standard input: the file `-`, or no file at all when it is not a terminal; it is read as a script */
  if(!files.length && !os.isatty(0)) files = ['-'];

  files = files.map(file => {
    if(file != '-') return file;

    buffers[path.resolve(STDIN_NAME)] = toArrayBuffer(std.in.readAsString());
    return STDIN_NAME;
  });

  if(/check-import/.test(scriptArgs[0])) {
    if(printFiles === undefined) printFiles = false;
    onlyImports = false;
    outputFile = null;
    out = DummyWriter('/dev/null');
  } else if(/list-import/.test(scriptArgs[0])) {
    if(printFiles === undefined && printImports === undefined) printImports = true;
    quiet = true;
    onlyImports = true;
    outputFile = null;
    out = DummyWriter('/dev/null');
  } else if(!params.merge) {
    if(typeof recursive == 'undefined') recursive = true;
  }

  if(params.merge) {
    onlyImports = !params['remove-unused'];
    if(params['remove-unused']) identifiersUsed = globalThis.identifiersUsed ??= memoize(arg => new Set());
  }

  if(typeof recursive == 'undefined') recursive = false;

  /* -q: no diagnostics at all; results (--print-files, --print-imports, the output) and errors still show */
  if(quiet) for(const method of ['log', 'debug', 'info']) console[method] = () => {};

  const { sort, 'case-sensitive': caseSensitive } = params;

  if(outputFile) out = FileWriter(outputFile);

  let argList = [...scriptArgs];

  //log =  (...args) => console.log(`${file}:`, ...args);

  logFile(`Start of: ${argList.join(' ')}\n`);

  if(!files.length) throw new ArgumentError('Expecting argument <files...>');

  const results = (globalThis.results = []);

  for(let file of files) {
    const result = ProcessFile(file, recursive, 0);

    if(debug >= 1) console.log('result', compact(2, { depth: 2 }), result);

    //Object.freeze(result.map);

    results.push(result);

    if(params.merge) {
      const map = result.map ?? FileMap.for(file);

      map.reset();

      const statements = result.imports.filter(st => !st.reexport);

      if(statements[0]) {
        const [start] = statements[0].range;

        for(let st of statements) map.replaceRange(st.range, '');

        const used = identifiersUsed && identifiersUsed(file);
        const outstr = merge(statements)
          .map(st => {
            const bindings = st.bindings.filter(({ local }) => {
              if(used && !used.has(local)) {
                console.log(`Removing unused import identifier '${local}' imported from '${st.file}'`);
                return false;
              }

              return true;
            });

            if(st.bindings.length && !bindings.length) return '';

            return st.attributes ? st.code : print({ ...st, path: st.file, bindings });
          })
          .filter(istr => istr !== '')
          .join('\n');

        map.insertAt(start, toArrayBuffer(outstr));
        map.trim();

        out ??= file == STDIN_NAME ? FdWriter(1, 'stdout') : FileReplacer(file);

        map.write(out);
        out.close();
      }

      doOutput = false;
    }
  }

  if(debug > 3) console.log('results', console.config({ compact: 3, depth: Infinity }), results);

  /* the builtin imports of every file, hoisted into one header */
  const hoisted = allImports()
    .filter(imp => !IsFileImport(imp.file))
    .map(imp => ({ ...imp, path: ResolveAlias(imp.file) }));

  if(doOutput) {
    const str = merge(hoisted)
      .map(imp => (imp.attributes ? imp.code : print(imp)) + '\n')
      .join('');

    if(debug >= 1) console.log('header =', str);

    out ??= FdWriter(1, 'stdout');

    if(globalSheBang) out.puts(globalSheBang.lexeme + '\n');

    if(!removeImports) out.puts(str + '\n');

    if(userScript) {
      for(let line of PrintUserscriptBanner({
        name: out.file,
        'run-at': 'document-start',
        version: '1.0',
        description: files.join(', '),
        downloadURL: `https://localhost:9000/${out.file}`,
        updateURL: `https://localhost:9000/${out.file}`,
      }))
        stream.puts(line);

      stream.puts("\n(function() {\n  'use strict';\n");
      ++stream.indent;
    }

    if(debug > 1) console.log('out', out, out.file);

    if(out.file) {
      let nbytes;

      try {
        if(debug >= 1) console.log(`results[0]`, compact(2, { depth: Infinity }), results[0]);
        nbytes = results[0].map.write(stream);
      } catch(error) {
        console.error(`write error ('${out.file}'):`, error);
        throw error;
      }

      if(debug >= 1) console.log(`${nbytes} bytes written to '${out.file}'`);
    }

    if(globalExports) {
      const maxDepth = globalExports - 1;
      let exportedNames = footer.filter(({ depth }) => depth <= maxDepth);

      if(debug > 3) console.log(`exportedNames`, exportedNames);

      const isIdent = n => typeof n == 'string' && n != 'default' && /^[A-Za-z_$][\w$]*$/.test(n);

      exportedNames = exportedNames
        .flatMap(({ names }) => names ?? [])
        .filter(([local, exp]) => isIdent(local) && isIdent(exp))
        .map(([local, exp]) => (local == exp ? local : `${exp}: ${local}`))
        .unique();

      stream.puts(`\nObject.assign(globalThis, { ${exportedNames.join(', ')} });\n`);
    }

    if(userScript) {
      --stream.indent;
      stream.puts('})();\n');
    }

    stream.close();
  }

  if(params.interactive) os.kill(process.pid, os.SIGUSR1);

  logFile(`Processed files: ${SpreadAndJoin(dependencyMap.keys(), ' ')}\n`);
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  if(error instanceof ArgumentError || error instanceof ResolveError) {
    console.error(error instanceof ResolveError ? `error: ${error.message}` : `${error.constructor.name}: ${error.message}`);
    std.exit(1);
  }

  console.error(`${error.constructor.name}: ${error.message}${error.stack ? '\n' + error.stack : ''}`);
  os.kill(process.pid, os.SIGUSR1);
} finally {
}

function ProcessFile(source, recursive, depth = 0) {
  if(modules[source]) return modules[source];

  const log = (...args) => console.log(`[${depth}]\x1b[38;5;208m${path.relative(source)}\x1b[0m:`, ...args);

  if(debug >= 1) log('Processing...', compact(true), { recursive, depth });
  if(printFiles) process.stdout.write(`${source}\n`);

  let start = Date.now();
  const dir = path.dirname(source);

  const bytebuf = BufferFile(source),
    type = path.extname(source).substring(1);

  if(!['js', 'mjs', 'cjs', 'json'].includes(type)) throw new Error(`Error lexing: ${source}`);

  const text = toString(bytebuf),
    toByte = ByteOffsets(text, bytebuf.byteLength);
  const { imports, exports, error: lexError } = parse(text);

  if(lexError) console.error(`${source}:${lexError.line}:${lexError.column}: ${lexError.code}`);

  /* import and export-from statements, the ones bundling replaces */
  const statements = [],
    byIndex = new Map();

  imports.forEach((r, i) => {
    if(r.kind != 'import-statement') return;

    byIndex.set(i, Statement(r, text, toByte, source, depth));
    statements.push(byIndex.get(i));
  });

  /* what -G assigns for export { a as b } from: b = a */
  for(const e of exports) if(e.kind == 'reexport' && e.imported) byIndex.get(e.importIndex).names.push([e.imported, e.name]);

  /* export { a, b as c }: one statement per list, for -G and -E */
  const lists = new Map();

  for(const e of exports) {
    if(e.kind != 'list') continue;

    if(!lists.has(e.range[0])) lists.set(e.range[0], Statement({ ...e, path: null }, text, toByte, source, depth));

    lists.get(e.range[0]).names.push([e.local, e.name]);
  }

  const map = FileMap.for(source);
  const result = (modules[source] = { imports: statements, exports, map });

  const PathAdjust = s => {
    let j = s;

    j = path.isRelative(j) ? path.join(dir, j) : s;
    j = path.normalize(j);

    if(!path.exists(j) && !path.isDirectory(j) && path.extname(j) === '') {
      if(path.isFile(j + '.js')) j = j + '.js';
    }

    const resolved = ModuleLoader.resolve(j);

    /* one spelling per file (relative to the cwd): FileMaps are keyed by the path string, so
       an absolute path (e.g. from --module-map) would get a second map for the same file */
    return resolved && path.isAbsolute(resolved) ? path.relative(resolved) : resolved;
  };

  const used = identifiersUsed ? identifiersUsed(source) : null;
  const comments = [];

  /* -u: the identifiers outside import statements, -C: the comments */
  if(used || removeComments) {
    const lexer = new ECMAScriptLexer(bytebuf, source);
    const ranges = statements.map(st => st.range);

    for(let token; (token = lexer.nextToken()); ) {
      if(/comment/i.test(token.type)) comments.push(token);
      else if(used && token.type == 'identifier' && !ranges.some(r => InRange(r, token.byteRange[0]))) used.add(token.lexeme);
    }
  }

  let end = Date.now();

  if(showTiming) log('Lexing ' + source.replace(/^\.\//, '') + ' took ${end - start}ms');
  start = Date.now();

  importsFor[source] = statements.filter(st => !st.reexport);

  if(debug >= 1) log(`importsFor`, compact(true, { maxStringLength: +(process.env.COLUMNS ?? 45) - 8 }), importsFor[source]);

  const sheBang = /^#![^\n]*/.exec(text);

  if(sheBang) {
    map.replaceRange([0, toByte(sheBang[0].length)], null);
    globalSheBang = { lexeme: sheBang[0] };
  }

  /* -m -u writes the merged imports in main(), with the unused names left out */
  if(used) {
    if(debug >= 1) {
      const imported = new Set(statements.flatMap(st => st.bindings.map(b => b.local)));

      log(`imported [ ${source} ]`, console.config({ compact: 1 }), [...imported]);
      log(`used     [ ${source} ]`, console.config({ compact: 1 }), [...intersection(used, imported)]);
      log(`unused   [ ${source} ]`, console.config({ compact: 1 }), [...difference(imported, used)]);
    }
  } else {
    for(const st of [...statements, ...lists.values()].sort((a, b) => a.range[0] - b.range[0])) {
      const { file, range, code, loc } = st;

      /* a local `export { a, b as c }` has no file to resolve: it only feeds -G, and -E drops it */
      if(st.kind == 'list') {
        footer.push(st);
        if(removeExports) map.replaceRange(range, null);
        continue;
      }

      /* builtin modules ('os', 'std', ...) stay imports: they are collected into the header
         main() prints, so the statement itself is dropped from the body unless the header is */
      if(IsBuiltin(file)) {
        if(!st.reexport && !removeImports) map.replaceRange(range, null);
        continue;
      }

      const p = PathAdjust(file);

      if(!p) throw new ResolveError(st, source);

      if(!IsFileImport(p) || !path.isFile(p)) {
        log(`\x1b[1;31mInexistent\x1b[0m file '${p}'`);
        throw new Error(`\x1b[1;31mInexistent\x1b[0m file '${p}'`);
      }

      (st.reexport ? footer : header).push(st);

      if(debug > 2) log('impexp', compact(1), { code, file, range: new NumericRange(...range), replacement: p, loc });
      else if(debug > 1) log('impexp', compact(true), { file, range: new NumericRange(...range), loc });

      map.replaceRange(range, p);
    }
  }

  if(removeComments && comments.length) {
    let i = -1;
    debugLog(`Removing ${comments.length} comments from '${source}'`);
    for(let { byteRange } of comments) {
      let sl = bytebuf.slice(...byteRange);
      if(debug > 1)
        debugLog(`comment[${++i}]`, compact(2), {
          byteRange,
          str: toString(sl),
        });

      map.replaceRange(byteRange, null);
    }
  }

  end = Date.now();

  processed.add(source);

  /* every import of the file is listed, builtin or not, whether or not it is followed:
     static, dynamic and require, as long as the specifier is a literal */
  if(printImports) {
    for(const imp of imports) {
      if(typeof imp.path != 'string') continue;

      const file = RemapModule(imp.path);
      let name = file;

      if(!IsBuiltin(file)) {
        const adjusted = PathAdjust(file);
        if(adjusted) name = path.relative(adjusted);
      }

      std.puts(source + ':' + name + ' ' + imp.bindings.map(b => b.local).join(' ') + `\n`);
    }
  }

  if(recursive > 0) {
    for(let imp of statements) {
      let { file } = imp;

      if(typeof file == 'string' && IsBuiltin(file)) {
        log(`Builtin module '${file}'`);
        continue;
      }

      const adjusted = PathAdjust(file);

      if(!adjusted) throw new ResolveError(imp, source);

      file = path.relative(adjusted);

      if(readPackage) {
        let alias = ResolveAlias(file);
        if(alias != file) {
          log(`\x1b[1;31mAliasing '${file}' to '${alias}'\x1b[0m`);
          file = alias;
        }
      }

      if(!/\.|\//.test(file)) {
        log(`Builtin module '${file}'`);
        continue;
      }

      if(processed.has(file) || file == source)
        continue;

      if(debug >= 1) log(`Import(recursive)`, compact(true), { recursive, file });

      file = ModuleLoader.resolve(file);

      if(!path.isFile(file)) {
        throw new Error(`Imported file not found: '${file}'`);
        continue;
      }

      if(debug >= 1) log(`Processed`, compact(true), { file });

      processed.add(file);

      AddDep(source, file);

      if(debug >= 1) log(`Recursing`, compact(true), { source, file });

      if(/\.(so|dll)$/i.test(file)) {
        if(printFiles) std.puts(`${path.resolve(file)}\n`);
      } else {
        try {
          ProcessFile(file, typeof recursive == 'number' ? recursive - 1 : recursive, depth + 1);
        } catch(e) {
          if(e instanceof ResolveError) throw e;
          console.error(`ERROR processing file '${file}': ${e.message}`);
        }
      }

    }
  }

  std.gc();

  if(showDeps) {
    let deps = [...DependencyTree(source, ' ', false, 0, '    ')];
    log(`Dependencies of '${source}':\n${SpreadAndJoin(deps)}`);
  }

  return result;
}
