#!/usr/bin/env qjsm
import * as os from 'os';
import * as std from 'std';
import * as fs from 'fs';
import inspect from 'inspect';
import * as path from 'path';
import { Lexer, Token } from 'lexer';
import { Console } from 'console';
import JSLexer from 'jslexer.js';
import { randInt, getTypeName, getTypeStr, isObject, shorten, toString, toArrayBuffer, define, curry, unique, split, extendArray, camelize, types, getOpt, quote, escape } from 'util';

('use strict');
('use math');

let buffers = {},
  modules = {},
  removeExports = false,
  removeComments = false,
  relativeTo,
  outputFile,
  recursive = true,
  debug = 0,
  header = [],
  processed = new Set(),
  bufferRef = new WeakMap(),
  fileBuffers = new Map(),
  fileMaps = new Map();

const FileBannerComment = (filename, i) => {
  let s = '';
  s += ` ${i ? 'end' : 'start'} of '${path.basename(filename)}' `;
  let n = Math.floor((80 - 6 - s.length) / 2);
  s = '/* ' + '-'.repeat(n) + s;
  s += '-'.repeat(80 - 3 - s.length) + ' */';
  if(i == 0) s = '\n' + s + '\n';
  else s = s + '\n';
  return s;
};

extendArray(Array.prototype);
const IsBuiltin = moduleName => /^[^\/.]+$/.test(moduleName);
const compact = (n, more = {}) => console.config({ compact: n, maxArrayLength: 100, ...more });
const AddUnique = (arr, item) => (arr.indexOf(item) == -1 ? arr.push(item) : null);
const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

const What = { IMPORT: Symbol.for('import'), EXPORT: Symbol.for('export') };
const ImportTypes = { IMPORT: 0, IMPORT_DEFAULT: 1, IMPORT_NAMESPACE: 2 };
const IsOneOf = curry((n, value) => (Array.isArray(n) ? n.some(num => num === value) : n === value));
const TokIs = curry((type, lexeme, tok) => {
  if(tok != undefined) {
    if(lexeme != undefined) if (typeof lexeme == 'string' && !IsOneOf(lexeme, tok.lexeme)) return false;
    if(type != undefined) {
      if(typeof type == 'string' && !IsOneOf(type, tok.type)) return false;
      if(typeof type == 'number' && !IsOneOf(type, tok.id)) return false;
    }
    return true;
  }
});
const CompareRange = (a, b) => (a === null || b === null ? 0 : typeof a[0] == 'number' && typeof b[0] == 'number' && a[0] != b[0] ? a[0] - b[0] : a[1] - b[1]);

const IsKeyword = TokIs('keyword');
const IsPunctuator = TokIs('punctuator');
const IsIdentifier = TokIs('identifier');
const IsStringLiteral = TokIs('stringLiteral');
const PutsFunction = outFn => str => {
  let b = toArrayBuffer(str);
  return outFn(b, b.byteLength);
};

const debugLog = (str, ...args) => {
  const pred = arg => isObject(arg) && 'compact' in arg;
  let opts = args.filter(pred);

  opts = opts.reduce((acc, opt) => define(acc, opt), opts.shift() ?? {});

  if(opts.compact === undefined) define(opts, compact(1));
  if(opts.maxArrayLength === undefined) define(opts, { maxArrayLength: 10 });
  if(opts.depth === undefined) define(opts, { depth: Infinity });

  args = args.filter(arg => !pred(arg));

  console.log(str, opts, ...args);
};

const FileWriter = file => {
  let fd = os.open(file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  return define(FdWriter(fd, file), {
    close: () => os.close(fd)
  });
};

function FdWriter(fd, name) {
  debugLog('FdWriter', { fd, name });
  let fn;
  fn = (buf, len) => {
    // if(!buf || !len) if (typeof fn.close == 'function') return fn.close();
    len ??= buf.byteLength;
    let result = os.write(fd, buf, 0, len);
    //debugLog('FdWriter.write', { fd, buf: shorten(toString(buf), 80), len, result });
    return result;
  };

  define(fn, {
    fd,
    name,
    file: name ?? fd,
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    seek: (whence, offset) => os.seek(fd, whence, offset),

    [Symbol.toStringTag]: `FileWriter< ${fd} >`,
    inspect() {
      return inspect({ fd }) ?? this[Symbol.toStringTag];
    }
  });
  return fn;
}

function ImportIds(seq) {
  return seq.filter(tok => IsIdentifier(null, tok));
}

function ImportType(seq) {
  if(IsKeyword(['import', 'export'], seq[0])) seq.shift();
  if(IsPunctuator('*', seq[0])) {
    if(IsKeyword('as', seq[1])) return ImportTypes.IMPORT_NAMESPACE;
  } else if(IsIdentifier(undefined, seq[0])) {
    if(IsKeyword('from', seq[1])) return ImportTypes.IMPORT_DEFAULT;
  }
  return ImportTypes.IMPORT;
}

function ImportFile(seq) {
  seq = NonWS(seq);
  //console.log('ImportFile', { seq });
  let idx = seq.findIndex(tok => IsKeyword('from', tok));
  while(seq[idx] && seq[idx].type != 'stringLiteral') ++idx;

  //  if(seq[idx-1].lexeme == 'from')
  if(seq[idx]) if (seq[idx].type == 'stringLiteral') return seq[idx].lexeme.replace(/^[\'\"\`](.*)[\'\"\`]$/g, '$1');
}

function ExportName(seq) {
  let idx = seq.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok));
  return seq[idx]?.lexeme;
}

function ByteSequence(tokens) {
  if(tokens.length) {
    let { loc } = tokens[0];
    let start = loc.byteOffset;
    let total = tokens.reduce((n, t) => n + t.byteLength, 0);
    let end = start + total;
    //let line = toString(BufferFile(loc.file).slice(start, end));
    return [start, end];
  }
}

function AddExport(tokens, relativePath = s => s) {
  if(tokens[0].seq == tokens[1].seq) tokens.shift();

  const { loc, seq } = tokens[0];

  if(!/^(im|ex)port$/i.test(tokens[0].lexeme)) throw new Error(`AddExport tokens: ` + inspect(tokens, { compact: false }));
  let def = tokens.some(tok => IsKeyword('default', tok));
  let file = ImportFile(tokens); // fromIndex != -1 ? Unquote(tokens[fromIndex + 1].lexeme) : null;
  if(file == ' ') throw new Error('XXX ' + inspect(tokens, { compact: false }));
  const idx = def || file ? tokens.findIndex(tok => tok.lexeme == ';') : tokens.slice(1).findIndex(tok => tok.type != 'whitespace');
  const remove = tokens.slice(0, idx + 1); //idx + 1);
  if(remove[0]) if (remove[0].lexeme != 'export') throw new Error(`AddExport tokens: ` + inspect(tokens, { compact: false }));
  const range = ByteSequence(remove) ?? ByteSequence(tokens);
  let source = loc.file;
  let type = ImportType(tokens);
  let code = toString(BufferFile(source).slice(...range));
  //console.log('AddExport', {remove,range,code});
  let len = tokens.length;
  //  console.log('AddExport', { range, code });
  if(tokens[1].lexeme != '{') len = tokens.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok)) + 1;
  tokens = tokens.slice(0, len);
  let exp = define(
    {
      type: What.EXPORT,
      file: file && /\./.test(file) ? relativePath(file) : file,
      tokens,
      exported: ExportName(tokens),
      range
    },
    {
      code,
      loc,
      ids() {
        return ImportIds(this.tokens);
      }
    }
  );
  return exp;
}

function AddImport(tokens, relativePath = s => s) {
  if(!/^(im|ex)port$/i.test(tokens[0].lexeme)) throw new Error(`AddImport tokens: ` + inspect(tokens, { compact: false }));
  const tok = tokens[0];
  const { loc, seq } = tok;
  let source = loc.file;
  let type = ImportType(tokens.slice()),
    file = ImportFile(tokens.slice());
  const range = ByteSequence(tokens.slice());
  range[0] = loc.byteOffset;
  let code = toString(BufferFile(source).slice(...range));

  //debugLog('AddImport', { type, file,code,range });

  //if(!/\./.test(file)) return null;
  let imp = define(
    { type, file: file && /\./.test(file) ? relativePath(file) : file, range },
    {
      tokens: tokens.slice(),
      code,
      loc,
      ids() {
        return ImportIds(tokens.slice());
      }
    }
  );
  let fn = {
    [ImportTypes.IMPORT_NAMESPACE]() {
      const { tokens } = this;
      let idx = tokens.findIndex(tok => IsKeyword('as', tok));
      return tokens[idx + 1].lexeme;
    },
    [ImportTypes.IMPORT_DEFAULT]() {
      const { tokens } = this;
      let idx = tokens.findIndex(tok => IsKeyword('import', tok));
      return tokens[idx + 1].lexeme;
    },
    [ImportTypes.IMPORT]() {
      const { tokens } = this;
      let idx = 0,
        specifier = [],
        specifiers = [];
      if(IsKeyword(['import', 'export'], tokens[idx])) ++idx;
      if(IsPunctuator('{', tokens[idx])) ++idx;
      for(; tokens[idx] && !IsKeyword('from', tokens[idx]); ++idx) {
        if(IsPunctuator([',', '}'], tokens[idx])) {
          if(specifier.length) specifiers.push(specifier);
          specifier = [];
        } else if(IsIdentifier(tokens[idx])) {
          specifier.push(tokens[idx]);
        }
      }
      specifiers = specifiers.flat().filter(tok => tok.type == 'identifier');
      return specifiers.map(tok => tok.lexeme);
    }
  }[type];

  if(typeof fn == 'function') {
    let local = fn.call(imp);

    // console.log('AddImport', { local });
    define(imp, { local });
  }

  return imp;
}

function ProcessFile(source, log = () => {}, recursive) {
  let start = Date.now();
  const dir = path.dirname(source);
  //console.log('ProcessFile', {source,dir});

  let bytebuf = source ? BufferFile(source) : code[1];

  let len = bytebuf.byteLength,
    type = path.extname(source).substring(1),
    base = camelize(path.basename(source, '.' + type).replace(/[^0-9A-Za-z_]/g, '_'));

  let lex = {
    js: new JSLexer(bytebuf, source)
  };
  lex.mjs = lex.js;
  lex.cjs = lex.js;

  const lexer = lex[type];

  // T = lexer.tokens.reduce((acc, name, id) => ({ ...acc, [name]: id }), {});

  let e = new SyntaxError();

  if(!lexer) throw new Error(`Error lexing: ${source}`);

  lexer.handler = lex => {
    const { loc, mode, pos, start, byteLength, state } = lex;
    log(' '.repeat(loc.column - 1) + '^');
  };
  let tokenList = [],
    declarations = [];
  const colSizes = [12, 8, 4, 20, 32, 10, 0];

  let tok,
    i = 0,
    mask = IntToBinary(lexer.mask),
    state = lexer.topState();
  lexer.beginCode = () => (code == 'js' ? 0b1000 : 0b0100);
  const balancer = () => {
    let self;
    let stack = [];
    const table = { '}': '{', ']': '[', ')': '(' };
    self = function ParentheseBalancer(tok) {
      switch (tok?.lexeme) {
        case '{':
        case '[':
        case '(': {
          stack.push(tok.lexeme);
          break;
        }
        case '}':
        case ']':
        case ')': {
          if(stack.last != table[tok.lexeme]) throw new Error(`top '${stack.last}' != '${tok.lexeme}' [ ${stack.map(s => `'${s}'`).join(', ')} ]`);

          stack.pop();
          break;
        }
      }
    };
    Object.assign(self, {
      stack,
      reset() {
        stack.clear();
      },
      /* prettier-ignore */ get depth() {
        return stack.length;
      }
    });

    return self;
  };
  let balancers = [balancer()],
    imports = [],
    exports = [],
    comments = [],
    impexp,
    cond,
    imp = [],
    showToken = tok => {
      if((lexer.constructor != JSLexer && tok.type != 'whitespace') || /^((im|ex)port|from|as)$/.test(tok.lexeme)) {
        let a = [/*(file + ':' + tok.loc).padEnd(file.length+10),*/ tok.type.padEnd(20, ' '), escape(tok.lexeme)];
        std.puts(a.join('') + '\n');
      }
    };

  const PathAdjust = s => {
    let j = path.join(dir, s);
    j = path.collapse(j);
    if(path.isRelative(j)) j = './' + j;
    return j;
  };
  let prevToken;
  for(;;) {
    let { stateDepth } = lexer;
    let value = lexer.next();
    let done = value === undefined;
    if(done) break;
    let newState = lexer.topState();
    if(newState != state) {
      if(state == 'TEMPLATE' && lexer.stateDepth > stateDepth) balancers.push(balancer());
      if(newState == 'TEMPLATE' && lexer.stateDepth < stateDepth) balancers.pop();
    }
    let n = balancers.last.depth;
    const { token } = lexer;
    const { loc, length, seq } = token;
    const { pos } = loc;
    //  let s = toString(bytebuf).slice(pos, pos + length);
    //  console.log('',token.lexeme, {pos, s, length})

    if(n == 0 && token.lexeme == '}' && lexer.stateDepth > 0) {
      lexer.popState();
    } else {
      balancer(token);
      if(n > 0 && balancers.last.depth == 0) log('balancer');
      if(/comment/i.test(token.type)) {
        comments.push(token);
      }
      if(['import', 'export'].indexOf(token.lexeme) >= 0) {
        impexp = What[token.lexeme.toUpperCase()];
        cond = true;
        imp = token.lexeme == 'export' ? [token] : [];
      }
      if(cond == true) {
        imp.push(token);
        if([';'].indexOf(token.lexeme) != -1) {
          cond = false;
          if(impexp == What.IMPORT || imp.some(i => i.lexeme == 'from')) {
            let obj = AddImport(imp, PathAdjust);
            if(obj) imports.push(obj);
          } else {
            exports.push(AddExport(imp, PathAdjust));
          }
        }
      }
      prevToken = token;
    }
    state = newState;
  }
  let end = Date.now();

  console.log(`Lexing '${source.replace(/^\.\//, '')}' took ${end - start}ms`);
  start = Date.now();

  let exportsFrom = exports.filter(exp => exp.tokens).filter(exp => exp.tokens.some(tok => tok.lexeme == 'from'));

  if(path.isRelative(source) && !/^(\.|\.\.)\//.test(source)) source = './' + source;

  // console.log('exportsFrom', exports);

  modules[source] = { imports, exports };

  let allExportsImports = exports.concat(imports).sort((a, b) => a.range[0] - b.range[0]);
  let fileImports = allExportsImports.filter(imp => typeof imp.file == 'string'); ///\.js$/i.test(imp.file));
  let splitPoints = unique(fileImports.reduce((acc, imp) => [...acc, ...imp.range], []));
  buffers[source] = [...split(BufferFile(source), ...splitPoints)].map(b => b ?? toString(b, 0, b.byteLength));

  /*console.log('fileImports', fileImports);*/

  let map = FileMap.for(source);

  for(let impexp of allExportsImports) {
    const { type, file, range, code, loc } = impexp;
    const [start, end] = range;
    // let bytebuf = BufferFile(source);
    let bufstr = toString(bytebuf.slice(...range));
    let arrbuf = toArrayBuffer(bufstr);

    // console.log('impexp', { type,file });
    let replacement = type == What.EXPORT ? null : /*FileMap.for*/ file;
    let { byteOffset } = loc;

    if(bufstr == ' ') throw new Error(`bufstr = ' ' loc: ${loc} ${loc.byteOffset} range: ${range} code: ` + toString(bytebuf.slice(loc.byteOffset, range[1] + 10)));

    //if(replacement == null) debugLog('replaceRange', inspect({ file: map.file, bufstr, range, replacement, loc: loc + '' }, { compact: 3, depth: 3 }) );
    if(typeof replacement == 'string' && !path.exists(replacement)) {
      header.push(impexp);
      replacement = null;
      //throw new Error(`Non-existing file '${replacement}' ${code}`);
    }

    //debugLog('impexp', { bufstr, loc: loc + '', range: new NumericRange(...range), removeExports, type });

    if(bufstr.trim() == 'export' && removeExports) replacement = null;

    if(type != What.EXPORT || removeExports) {
      map.replaceRange(range, replacement);
    }
  }

  //  debugLog('comments', comments.map(({byteRange, lexeme})=>[byteRange,lexeme,toString(bytebuf.slice(...byteRange))]));
  //
  if(removeComments) {
    i = -1;
    debugLog(`Removing ${comments.length} comments from '${source}'`);
    for(let { byteRange, lexeme } of comments) {
      let sl = bytebuf.slice(...byteRange);
      //debugLog(`comment[${++i}]`, compact(2), { byteRange, str: toString(sl) });
      map.replaceRange(byteRange, null);
    }
  }

  // debugLog('map', map.dump());

  if(debug) console.log('dump map', map.dump());

  end = Date.now();

  // console.log(`Substituting '${source.replace(/^\.\//, '')}' took ${end - start}ms`);
  processed.add(source);

  if(recursive > 0) {
    for(let imp of fileImports) {
      const { file, range, tokens } = imp;
      if(!/\./.test(file)) {
        // console.log(`Builtin module '${file}'`);
        continue;
      }
      if(!path.exists(file)) {
        console.log(`Path must exist '${file}'`);
        continue;
      }
      if(processed.has(file) || file == source) {
        //console.log(`Already processed '${file}'`);
        continue;
      }
      //  console.log(`ProcessFile.recursive`, { file });
      let args = [file, log, typeof recursive == 'number' ? recursive - 1 : recursive];
      //    console.log(`ProcessFile(${args.join(', ')})`);
      processed.add(file);

      ProcessFile(...args);
    }
  }
  /*
  let end = Date.now();
  console.log(`'${source.replace(/^\.\//, '')}' took ${end - start}ms`);
*/

  std.gc();

  return map;
}

function NonWS(tokens) {
  return tokens.filter(tok => tok.type != 'whitespace');
}

function TokenSequence(tokens) {
  return define(
    tokens.map(tok => tok.lexeme),
    {
      toString() {
        return this.join('');
      }
    }
  );
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
    //console.log('ranges', console.config({ compact: 1 }), ranges);
    let i = -1;
    for(let range of ranges) {
      if(IsRange(range)) {
        range = [...range];
        console.log('range#' + ++i, inspect(range));

        if(IsRange(prev) && IsRange(range)) {
          //     if(range[0] < prev[1]) range[0] = prev[1];
          let [start, end] = range;

          if(start >= prev[1]) yield new NumericRange(prev[1], start);
          //  else throw new Error(`Invalid range ` + inspect([start, end]) + ' ' + inspect({ prev: [...prev] }));
        }

        if(!only) {
          if(!(isObect(range) && range instanceof NumericRange)) range = new NumericRange(...range);
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

define(NumericRange.prototype, {
  [Symbol.toStringTag]: 'NumericRange',
  [Symbol.inspect](depth, opts) {
    const [start, end] = this;
    let s = '';
    //s += `\x1b[1;31mNumericRange\x1b[0m(`;
    const pad = s => (s + '').padEnd(5);

    s += `\x1b[1;36m${pad(start)}\x1b[0m`;
    s += ` - `;
    //  s += `\x1b[1;36m${pad(end)}\x1b[0m`;
    s += `\x1b[1;36m${pad('+' + (end - start))}\x1b[0m`;
    //s+=`)`;
    s = `[ ${s} ]`;
    return s;
  }
});

class FileMap extends Array {
  constructor(file, buf) {
    super();

    if(typeof file != 'number') {
      //console.log('FileMap.constructor',{file,buf});
      this.file = file;
      buf ??= BufferFile(file);
      if(!buf) throw new Error(`FileMap buf == ${buf}`);
      this.push([new NumericRange(0, buf.byteLength), buf]);
      fileMaps.set(file, this);
    }
  }

  static empty(file) {
    if(typeof file == 'string') file = FileMap.for(file);

    if(isObject(file) && file instanceof FileMap) return file.isEmpty();
  }

  isEmpty() {
    return false;
  }

  static for(file, buf) {
    // console.log('FileMap.for', { file, buf });
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
        [Symbol.toStringTag]: 'FileMap(empty)'
      };
      return Object.setPrototypeOf(obj, FileMap.prototype);
    }
    if(fileMaps.has(file)) return fileMaps.get(file);
    return new FileMap(file, buf);
  }

  splitAt(pos) {
    let i = this.findIndex(([range, buf]) => range && InRange(range, pos));

    if(i != -1) {
      let [range, buf] = this[i];
      let [start, end] = range;
      this.splice(i + 1, 0, [[(range[1] = pos), end], buf]);
      return i + 1;
    }
  }

  sliceAt(n) {
    return Array.prototype.findIndex.call(this, ([range, buf]) => (range ? InRange(range, n) : false));
  }

  replaceRange(range, file) {
    if(Array.isArray(range) && !(range instanceof NumericRange)) range = new NumericRange(...range);
    const sliceIndex = n => {
      let r;
      if(this[0]) {
        const range = new NumericRange(...this[0][0]);
        if(n < range.start) return 0;
      }
      r = this.reduce((acc, item, i) => {
        if(acc === undefined) return [i, item[0][0]];
        if(item[0]) {
          if(n >= item[0][1]) return [i + 1, item[0][1]];
          if(n => item[0][0]) return [i, item[0][0]];
        }
        return acc;
      }, undefined);
      return r[0];
    };
    let start = sliceIndex(range.start);
    let end = sliceIndex(range.end);

    //debugLog(`FileMap.replaceRange`, compact(2, { maxArrayLength: 10 }), { start, end }, 'this[end] =', this[end]);

    if(range.start > this[start][0].start) {
      if(start == end) {
        let [range, buf] = this[start];
        let insert = [new NumericRange(...range), buf];
        this.splice(++end, 0, insert);
      }
      this[start][0].end = range.start;
      if(this[end] && this[end][0]) this[end][0].start = range.end;
    } else {
      this[start][0].start = range.end;
    }

    if(file != null) this.splice(start + 1, 0, [null, file]);
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
          /*if(!isObject(str)) s += quote(shorten(str), "'");
          else*/
          if(isObject(buf) && 'byteLength' in buf) s += `<this>`;
          else s += inspect(buf, { maxArrayLength: 30 });
          s += ` ],`;
          return s;
        }, '') +
      `\n\t]\n}`
    );
  }

  at(i) {
    const [range, buf] = this[i];
    if(range && buf) {
      const [start, end] = range;
      return buf.slice(start, end);
    }
    if(range == null) {
      let file = buf;
      let str = buf;
      if(typeof str == 'string') {
        if(!path.exists(str)) throw Error(`Inexistent file '${str}'`);
        str = FileMap.for(str);
      }
      return str;
    }
    throw new Error(`at(${i}) ` + inspect({ range, buf }));
  }

  toArray() {
    return this.map((s, i) => this.at(i));
  }

  holes() {
    let ranges = [...this.map(([range]) => range)].sort(CompareRange);

    console.log('ranges', console.config({ depth: Infinity }), ranges);

    let iter = NumericRange.holes(ranges, true);
    console.log('iter', iter);
    let holes = [...iter];
    let len = holes.length;
    for(let i = 0; i < len; i++) {
      const hole = holes[i];
      const [range] = this[i];

      console.log('#' + (i + 1), compact(2), inspect({ hole, range }, { compact: 2, depth: 4 }));
    }
    //console.log('holes', holes);

    return holes;
  }

  write(out, depth = 0, serial) {
    if(debug) debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, { out, depth, serial });
    let r,
      written = 0;
    let { length } = this;
    serial ??= randInt(0, 1000);
    if(this.serial === serial) return 0;
    this.serial = serial;
    if(typeof out == 'string') out = FileWriter(out);
    for(let i = 0; i < length; i++) {
      let str = this.at(i);
      let len = str.byteLength ?? str.length;
      if(isObject(str)) {
        if(str instanceof FileMap) {
          if(str.serial === serial) {
            r = 0;
            continue;
          }
          out.puts(FileBannerComment(str.file, 0));
          r = str.write(out, depth + 1, serial);
          out.puts(FileBannerComment(str.file, 1));
        } else {
          // console.log('out', out);
          r = out(str, len);
          if(r != len) r = -1;
        }
      } else {
        throw new Error(getTypeName(str));
      }
      //debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, `[${i + 1}/${length}]`, `result=${r}`, compact(1, { customInspect: true }), { depth }, out.inspect());
      if(r < 0) break;
      written += r;
    }
    return written;
  }

  toString(fn = FileBannerComment) {
    const n = this.length;
    let s = '',
      i;
    for(i = 0; i < n; i++) {
      let str;
      const [range, buf] = this[i];
      if(range === null && buf === null) continue;
      if((str = this.at(i)) === null) continue;
      if(range === null) if (typeof buf == 'string') /*if(typeof str == 'string')*/ str = fn(buf, 0) + str + fn(buf, 1);
      s += str;
    }
    return s;
  }
}

FileMap.prototype[Symbol.toStringTag] = 'FileMap';
FileMap.prototype[Symbol.inspect] = function(depth, opts) {
  let arr = [...this].map(([range, buf], i) => {
    if(range) {
      buf = buf.slice(...range);
    }
    // console.log(`i=${i}`, { range, buf });
    return [range, buf.constructor.name];
  });
  return (
    `FileMap\x1b[1;35m<${this.file}>\x1b[0m ` +
    inspect(arr, {
      ...opts,
      compact: 1,
      breakLength: Infinity,
      maxArrayLength: 10,
      maxStringLength: 10,
      customInspect: true,
      depth: depth + 2
    })
  );
};

function BufferFile(file, buf) {
  file = path.normalize(file);
  buf ??= buffers[file] ??= fs.readFileSync(file, { flag: 'r' });
  if(typeof buf == 'object' && buf !== null) bufferRef.set(buf, file);
  if(typeof buf == 'object' && buf !== null) fileBuffers.set(file, buf);
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

function WriteFile(file, tok) {
  let f = std.open(file, 'w+');
  f.puts(tok);
  console.log('Wrote "' + file + '": ' + tok.length + ' bytes');
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;
  return 'Lexer ' + inspect({ start, pos, size });
}

function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;
  return `★ Token ${inspect({ chars, offset, length, loc }, { depth: 1 })}`;
}

function PrintES6Import(imp) {
  return {
    [ImportTypes.IMPORT_NAMESPACE]: ({ local, file }) => `import * as ${local} from '${file}';`,
    [ImportTypes.IMPORT_DEFAULT]: ({ local, file }) => `import ${local} from '${file}';`,
    [ImportTypes.IMPORT]: ({ local, file }) => `import { ${local.join(', ')} } from '${file}';`
  }[imp.type](imp);
}

function PrintCJSImport({ type, local, file }) {
  return {
    [ImportTypes.IMPORT_NAMESPACE]: () => `const ${local} = require('${file}');`,
    [ImportTypes.IMPORT_DEFAULT]: () => `const ${local} = require('${file}');`,
    [ImportTypes.IMPORT]: () => `const { ${local.join(', ')} } = require('${file}');`
  }[type]();
}

function main(...args) {
  globalThis.console = new Console(process.stderr, {
    inspectOptions: {
      colors: true,
      depth: 8,
      stringBreakNewline: true,
      maxStringLength: 1000,
      maxArrayLength: Infinity,
      compact: false,
      //reparseable: true,
      hideKeys: [Symbol.toStringTag /*, 'code'*/]
    }
  });

  let optind = 0,
    code = 'c',
    exp = true;

  let out = FdWriter(1, 'stdout');

  let params = getOpt(
    {
      debug: [false, () => ++debug, 'x'],
      sort: [false, null, 's'],
      'case-sensitive': [false, null, 'c'],
      quiet: [false, null, 'q'],
      export: [false, () => (exp = true), 'e'],
      'relative-to': [true, arg => (relativeTo = path.absolute(arg)), 'r'],
      output: [true, file => (outputFile = file), 'o'],
      'no-recursive': [false, () => (recursive = false), 'R'],
      'remove-exports': [false, () => (removeExports = true), 'E'],
      'remove-comments': [false, () => (removeComments = true), 'C'],
      '@': 'files'
    },
    args
  );
  let files = params['@'];
  const { sort, 'case-sensitive': caseSensitive, quiet } = params;

  if(outputFile) out = FileWriter(outputFile);

  if(debug) debugLog('main', { outputFile, out });

  const RelativePath = file => path.join(path.dirname(process.argv[1]), '..', file);

  if(!files.length) files.push(RelativePath('lib/util.js'));

  let log = quiet ? () => {} : (...args) => console.log(`${file}:`, ...args);
  let results = [];
  for(let file of files) {
    results.push(ProcessFile(file, log, recursive));
  }
  let [result] = results;

  let lines = header
    .filter(impexp => !IsBuiltin(impexp.file))
    .map(hdr => hdr.code)
    .filter(line => !line.startsWith('export'));

  /* let headerIds = header.map(impexp => [impexp.file, impexp.ids().map(tok => tok.lexeme)]);

  debugLog(`headerIds:`, headerIds);*/

  if(lines.length) lines = [FileBannerComment('header', 0), ...lines, FileBannerComment('header', 1)];

  let output = lines.reduce((acc, line) => acc + line + '\n', '');

  out.puts(output);

  //  console.log('holes', compact(1), result.holes());

  console.log(`${result.write(out)} bytes written to '${out.file}'`);

  out.close();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
