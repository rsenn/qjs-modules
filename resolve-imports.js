#!/usr/bin/env qjsm
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { camelize, curry, define, difference, error, escape, getOpt, getset, getTypeName, intersection, isObject, mapWrapper, memoize, padStartAnsi, quote, randInt, split, toArrayBuffer, toString, types, unique, nonenumerable } from 'util';
import extendArray from 'extendArray';
import extendArrayBuffer from 'extendArrayBuffer';
import { Console } from 'console';
import inspect from 'inspect';
import { Lexer, Token } from 'lexer';
import ECMAScriptLexer from 'lexer/ecmascript.js';
import * as std from 'std';

('use strict');
('use math');

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

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
  log = () => {},
  globalImports = {};

Object.assign(globalThis, {
  buffers,
  fileBuffers,
  fileMaps,
  modules,
  ImportedBy,
  importsFor,
  globalImports,
  allImports() {
    let r = [];
    for(let source in importsFor) {
      let imports = importsFor[source];
      r.push(...imports);
    }
    return [...new Set(r)];
  }
});

let header = (globalThis.header = []),
  footer = (globalThis.footer = []);

let dependencyTree = memoize(arg => [], dependencyMap);
let bufferMap = getset(bufferRef);
let identifiersUsed;

function ReadJSON(filename) {
  let data = fs.readFileSync(filename, 'utf-8');
  return data ? JSON.parse(data) : null;
}

const ReadPackageJSON = memoize(() => {
  let ret = ReadJSON('package.json');

  if(typeof ret != 'object' || ret == null) ret = { _moduleAliases: {} };
  ret._moduleAliases ??= {};
  return ret;
});

function ImportedBy(importFile) {
  let r = [];
  for(let source in importsFor) {
    let imports = importsFor[source];

    if(imports.contains(importFile)) r.push(source);
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
  if(/\.|\//.test(filename)) return filename;
}

function NormalizePath(p) {
  p = path.absolute(p);
  p = path.normalize(p);
  p = path.relative(path.getcwd(), p);
  p = path.resolve(p);
  if(!path.isAbsolute(p)) if (!p.startsWith('./') && !p.startsWith('../') && p != '..') p = './' + p;
  return p;
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
    ...more
  });
const AddUnique = (arr, item) => (arr.indexOf(item) == -1 ? arr.push(item) : null);
const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

const What = {
  IMPORT: Symbol.for('import'),
  EXPORT: Symbol.for('export')
};

const ImportTypes = {
  IMPORT: 0,
  IMPORT_DEFAULT: 1,
  IMPORT_NAMESPACE: 2
};

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
const CompareRange = (a, b) =>
  a === null || b === null
    ? 0
    : typeof a[0] == 'number' && typeof b[0] == 'number' && a[0] != b[0]
    ? a[0] - b[0]
    : a[1] - b[1];

const IsKeyword = TokIs('keyword');
const IsPunctuator = TokIs('punctuator');
const IsIdentifier = TokIs('identifier');
const IsStringLiteral = TokIs('stringLiteral');
const PutsFunction = outFn => str => {
  let b = types.isArrayBuffer(str) ? str : toArrayBuffer(str);
  return outFn(b, b.byteLength);
};

const IsWhiteSpace = str => /^\s*$/.test(str) || str.trim() == '';

const IsImportExportFrom = seq => {
  seq = seq.slice(-3);
  return IsKeyword('from', seq[0]) && IsStringLiteral(seq[1]) && IsPunctuator(';', seq[2]);
};

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

function OutputImports(imports = globalImports) {
  let s = '';
  for(let [name, idmap] of Object.entries(imports).map(([name, obj]) => [name, Object.entries(obj)])) {
    let index;

    if(-1 != (index = idmap.findIndex(([local, name]) => name === '*'))) {
      let [[star]] = idmap.splice(index, 1);
      s += `import * as ${star} from '${name}';\n`;
    }

    if(idmap.length == 0) continue;

    s += `import `;

    if(idmap.length == 1 && ['*', 'default'].contains(idmap[0][1]))
      s += idmap[0][1] == '*' ? `* as ${idmap[0][0]}` : idmap[0][0];
    else
      s +=
        '{ ' +
        idmap.reduce(
          (acc, [local, name]) => (acc ? acc + ', ' : '') + (local === name ? local : name + ' as ' + local),
          undefined
        ) +
        ' }';

    s += ` from '${name}';\n`;
  }
  return s;
}

function MergeImports(imports) {
  let filenames = unique(imports.map(i => i.file));
  let result = [];

  //console.log('MergeImports', { filenames, imports });

  for(let file of filenames) {
    //let byFile=imports.filter(i => i.file == file);
    let merged = Import.merge(
      imports.filter(i => i.file == file),
      file
    );

    result.push(merged);
  }

  return result;
}

function FileWriter(file, mode = 0o755) {
  let fd = os.open(file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, mode);
  return define(FdWriter(fd, file), {
    close: () => os.close(fd)
  });
}

function FileReplacer(file) {
  let fd = os.open(file + '.new', os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o644);
  return define(FdWriter(fd, file), {
    close: () => {
      //console.log('FileReplacer.close', fd);
      let size = fs.sizeSync(fd);
      os.close(fd);

      let err;

      err = os.remove(file);
      //err = os.rename(file, file + '.old');
      err = os.rename(file + '.new', file);

      if(err) throw new Error(`FileReplacer rename() error: ${std.strerror(-err)}`);
      console.log(`${file} written (${size} bytes)`);
    }
  });
}

function FdWriter(fd, file) {
  let fn;
  fn = (buf, len) => {
    if(!types.isArrayBuffer(buf)) buf = toArrayBuffer(buf);
    len ??= buf.byteLength;
    let result = os.write(fd, buf, 0, len);
    return result;
  };

  define(fn, {
    fd,
    file,
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

function ArrayWriter(
  arr,
  t = (buf, len) => {
    if(!types.isArrayBuffer(buf)) buf = toArrayBuffer(buf);
    return buf.slice(0, len ?? buf.byteLength);
  }
) {
  let fn = (buf, len) => (arr.push(t(buf, len)), len);

  define(fn, {
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    [Symbol.toStringTag]: `ArrayWriter`,
    inspect() {
      return inspect({ arr }) ?? this[Symbol.toStringTag];
    }
  });
  return fn;
}

function DummyWriter(name) {
  let fn = (buf, len) => len;
  define(fn, {
    name,
    puts: PutsFunction(fn),
    write: fn,
    close: () => {},
    seek: () => {},
    [Symbol.toStringTag]: `DummyWriter< ${name} >`,
    inspect() {
      return inspect({ fd }) ?? this[Symbol.toStringTag];
    }
  });
  return fn;
}

function ImportIds(seq) {
  seq = NonWS(seq);
  const { length } = seq;
  const ret = [];
  for(let i = 0; i < length; i++) {
    if(IsIdentifier(null, seq[i])) {
      if(IsKeyword('as', seq[i + 1]) && IsIdentifier(null, seq[i + 2])) i += 2;

      ret.push(seq[i]);
    }
  }
  return ret;
}

function ImportIdMap(seq) {
  //console.log('ImportIdMap', console.config({ depth: Infinity, compact: 1, maxArrayLength: 3 }), seq);
  let i = 0,
    name,
    local,
    entries = [];
  seq = NonWS(seq);

  const add = (local, name) =>
    entries.findIndex(([l, n]) => local == l && name == n) == -1 && entries.push([local, name]);

  if(!IsKeyword('import', seq[0])) return null;

  while(seq[i]) {
    if(seq[i].type != 'keyword') break;
    ++i;
  }
  for(; i < seq.length; i++) {
    if(IsKeyword('from', seq[i])) {
      ++i;
      continue;
    }

    if(IsPunctuator(';', seq[i])) continue;

    if(IsKeyword('import', seq[i])) continue;

    if(seq[i].type == 'stringLiteral') continue;

    let tok = seq[i];
    let { loc } = tok;

    if(IsPunctuator('*', seq[i]) && seq[i + 1].lexeme == 'as') {
      name = seq[i].lexeme;
      local = seq[i + 2].lexeme;
      add(local, name);
      i += 3;
    } else if(seq[i].type == 'identifier') {
      local = seq[i].lexeme;
      name = 'default';
      add(local, name);

      if(IsPunctuator(',', seq[i + 1])) ++i;
    } else if(IsKeyword('from', seq[i + 1]) && seq[i].type == 'identifier') {
      local = seq[i].lexeme;
      name = 'default';
      add(local, name);
    } else if(IsPunctuator('{', seq[i])) {
      ++i;
      while(seq[i]) {
        if(IsPunctuator('}', seq[i])) {
          i++;
          break;
        }
        if(IsKeyword('as', seq[i + 1])) {
          name = seq[i].lexeme;
          local = seq[i + 2].lexeme;
          i += 2;
        } else {
          local = name = seq[i].lexeme;
        }
        add(local, name);
        ++i;
        while(IsPunctuator(',', seq[i])) ++i;
      }
    } else {
      const { id, type, lexeme } = tok;

      throw new Error(
        `No such token[${seq.indexOf(tok)}] (at ${tok.loc}) (${inspect({ id, type, lexeme })}) ${tok.lexeme} at ${
          tok.loc
        }`
      );
    }
  }
  return entries;
}

function ImpExpType(seq) {
  if(seq.some(tok => IsKeyword('import', tok))) return What.IMPORT;
  if(seq.some(tok => IsKeyword('export', tok))) return What.EXPORT;
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
  let idx = seq.findIndex(tok => IsKeyword('from', tok));
  while(seq[idx] && seq[idx].type != 'stringLiteral') ++idx;
  if(seq[idx])
    if(seq[idx].type == 'stringLiteral') {
      let f = seq[idx].lexeme.replace(/^[\'\"\`](.*)[\'\"\`]$/g, '$1');
      return f;
    }
}

function ExportName(seq) {
  let idx = seq.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok));

  while(seq[idx] && seq[idx].type != 'identifier') idx++;

  return seq[idx]?.lexeme;
}

function ByteSequence(tokens) {
  const first = tokens[0],
    last = tokens[tokens.length - 1];

  return [first.loc.byteOffset, last.loc.byteOffset + last.byteLength];

  if(tokens.length) {
    let { loc } = tokens[0];
    let start = loc.byteOffset;
    let total = tokens.reduce((n, t) => n + t.byteLength, 0);
    let end = start + total;
    //let line = toString(BufferFile(loc.file).slice(start, end));
    return [start, end];
  }
}

function ModuleLoader(module) {
  let file;

  if(path.isDirectory(module)) {
    file = ModuleLoader(path.join(module, 'index.js'));
  } else if(path.isFile(module)) {
    file = module;
  } else if(!/\.js$/.test(module)) {
    file = ModuleLoader(module + '.js');
  }

  return file;
}

function ModuleExports(file) {
  let m;
  try {
    if((m = moduleList.find(m => new RegExp(file).test(getModuleName(m)))) || (m = loadModule(file))) {
      let list = getModuleExports(m);
      return Object.keys(list);
    }
  } catch(error) {
    console.log('ERROR', error.message + '\n' + error.stack);
  }
}

function Export(tokens, relativePath = s => s) {
  if(tokens[0].seq == tokens[1].seq) tokens.shift();
  const { loc, seq } = tokens[0];
  if(!/^(im|ex)port$/i.test(tokens[0].lexeme))
    throw new Error(`AddExport tokens: ` + inspect(tokens, { compact: false }));
  let def = tokens.findIndex(tok => IsKeyword('default', tok));
  let k = 1;
  while(tokens[k].type == 'whitespace' || IsKeyword(['let', 'class', 'function', 'const'], tokens[k])) k++;
  while(tokens[k] && tokens[k].type != 'identifier') k++;
  let name = ExportName(tokens);
  let exported = def != -1 ? 'default' : name;
  let file = ImportFile(tokens);
  if(file == ' ') throw new Error('XXX ' + inspect(tokens, { compact: false }));
  const idx =
    def != -1
      ? def
      : file
      ? tokens.findIndex(tok => tok.lexeme == ';')
      : tokens.slice(1).findIndex(tok => tok.type != 'whitespace');
  const o = NonWS(tokens)[1].lexeme == '{';
  const remove = o || def != -1 ? tokens.slice() : tokens.slice(0, def == idx ? idx + 2 : idx + 1);
  if(remove[0])
    if(remove[0].lexeme != 'export') throw new Error(`AddExport tokens: ` + inspect(tokens, { compact: false }));
  const range = ByteSequence(remove) ?? ByteSequence(tokens);
  let source = loc.file;
  let type = ImpExpType(tokens);
  let code = TokenSequence(tokens).toString(); // toString(BufferFile(source).slice(...range));
  if(def != -1) if (debug > 2) console.log('new Export', { source, file, code, range, loc });
  let len = tokens.length;
  if(o) {
    exported = tokens.filter(tok => tok.type == 'identifier').map(tok => tok.lexeme);
  }

  if(NonWS(tokens)[1].lexeme != '{')
    len = tokens.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok)) + 1;
  tokens = tokens.slice(0, len);
  let exp = define(
    {
      type: What.EXPORT,
      file: file && /\./.test(file) ? relativePath(file) : file,
      tokens,
      exported,
      name,
      range
    },
    {
      code,
      loc,
      path() {
        const { file } = this;
        if(typeof file == 'string') return relativePath(file);
      }
    }
  );
  return Object.setPrototypeOf(exp, Export.prototype);
}

define(Export.prototype, {
  ids() {
    return ImportIds(this.tokens).map(({ lexeme }) => lexeme);
  },
  idmap() {
    return ImportIdMap(this.tokens);
  }
});

function Import(tokens, relativePath = s => s, depth) {
  //console.log('Import', {tokens: tokens.map(t => t.lexeme).slice(-3),hasFrom: tokens.some(i => IsKeyword('from', i))  });
  tokens = tokens[0].seq === tokens[1].seq ? tokens.slice(1) : tokens.slice();

  if(!/^(im|ex)port$/i.test(tokens[0].lexeme))
    throw new Error(`AddImport tokens: ` + inspect(tokens, { compact: false }));

  const tok = tokens[0];
  const { loc, seq } = tok;
  let source = loc.file;
  let type = ImpExpType(tokens.slice());

  const range = ByteSequence(tokens.slice());
  range[0] = loc.byteOffset;
  let code = toString(BufferFile(source).slice(...range));

  /* prettier-ignore */ if(debug > 3) console.log('\x1b[1;31mnew\x1b[1;33m Import\x1b[0m', compact(1), {source, file:  ImportFile(tokens), code, loc, range });
  let imp = Object.setPrototypeOf(
    nonenumerable(
      {
        get type() {
          return ImpExpType(this.tokens);
        },
        get file() {
          return ImportFile(this.tokens);
        },
        tokens,
        code,
        loc,
        depth,
        path() {
          const { file } = this;
          if(typeof file == 'string') return relativePath(file);
        }
      },
      {
        //file: ResolveAlias(file && /\./.test(file) ? relativePath(file) : file),
        range
      }
    ),
    Import.prototype
  );
  let fn = {
    [ImportTypes.IMPORT_NAMESPACE]() {
      return this.tokens[this.tokens.findIndex(tok => IsKeyword('as', tok)) + 1].lexeme;
    },
    [ImportTypes.IMPORT_DEFAULT]() {
      const { tokens } = this;
      return tokens[tokens.findIndex(tok => IsKeyword('import', tok)) + 1].lexeme;
    },
    [ImportTypes.IMPORT]() {
      const { tokens } = this;
      let i = 0,
        s = [],
        a = [];
      if(IsKeyword(['import', 'export'], tokens[i])) ++i;
      if(IsPunctuator('{', tokens[i])) ++i;
      for(; tokens[i] && !IsKeyword('from', tokens[i]); ++i) {
        if(IsPunctuator([',', '}'], tokens[i])) {
          if(s.length) a.push(s);
          s = [];
        } else if(IsIdentifier(tokens[i])) {
          s.push(tokens[i]);
        }
      }
      a = a.flat().filter(tok => tok.type == 'identifier');
      return a.map(tok => tok.lexeme);
    }
  }[type];
  if(typeof fn == 'function') {
    let local = fn.call(imp);
    define(imp, { local });
  }

  return imp;
}

define(Import.prototype, {
  [Symbol.toStringTag]: 'Import',
  ids(pred = local => true) {
    return ImportIds(this.tokens.slice())
      .map(({ lexeme }) => lexeme)
      .filter(pred);
  },
  idmap(pred = (local, name) => true) {
    return ImportIdMap(this.tokens.slice()).filter(([local, name]) => pred(local, name));
  },
  toCode(filterIds = id => true) {
    if(this.ids().filter(filterIds).length == 0) return '';
    let tokens = this.tokens.filter(tok => (tok.type == 'identifier' ? filterIds(tok.lexeme) : true));
    let prev, pnws;
    tokens = tokens.reduce((acc, tok, i) => {
      if(prev && prev.type == 'whitespace' && tok.type == 'whitespace') return acc;
      if(pnws !== undefined && [',', '{'].indexOf(acc[pnws].lexeme) != -1 && [',', '}'].indexOf(tok.lexeme) != -1) {
        if(tok.lexeme == '}') acc.splice(pnws, 1);
        if(tok.lexeme == '}') {
          pnws = acc.length;
          acc.push(tok);
        }
        return acc;
      }
      if(tok.type != 'whitespace') pnws = acc.length;
      prev = tok;
      acc.push(tok);
      return acc;
    }, []);
    tokens = AddWhitespace(tokens);
    if(debug > 3) console.log('tokens', tokens);
    return TokenSequence(tokens).toString();
  },
  toString(idPred = (local, name) => true) {
    const map = this.idmap(idPred);
    if(map.length == 0) return '';

    let index,
      output = 'import ';
    if((index = map.findIndex(([local, foreign]) => foreign == '*')) != -1) {
      output += '* as ' + map[index][0];
    } else if(map.length == 1 && map[0][1] == 'default') {
      output += map[0][0];
    } else {
      output +=
        '{ ' + map.map(([local, foreign]) => (local == foreign ? local : foreign + ' as ' + local)).join(', ') + ' }';
    }

    output += ` from '${this.file}';`;
    return output;
  }
});

define(Import, {
  merge(imports, file) {
    let tokens = [],
      i = 0;
    if(!file) {
      let imp = imports.shift();
      file = imp.file;
      tokens.push(...imp.tokens);
    }

    for(let other of imports) {
      if(other.file == file) tokens.push(...other.tokens);
      i++;
    }

    return Object.setPrototypeOf(define({ type: What.IMPORT }, { tokens, file }), Import.prototype);
  }
});

function ProcessFile(source, log = () => {}, recursive, depth = 0) {
  if(debug > 1) console.log(`Processing ${source}`);

  //source = path.resolve(source);

  if(printFiles) std.puts(`${path.resolve(source)}\n`);

  let start = Date.now();
  const dir = path.dirname(source);

  if(debug >= 2) console.log('ProcessFile', { source });

  let bytebuf = BufferFile(source);

  let len = bytebuf.byteLength,
    type = path.extname(source).substring(1),
    base = camelize(path.basename(source, '.' + type).replace(/[^0-9A-Za-z_]/g, '_'));

  let lex = {
    js: new ECMAScriptLexer(bytebuf, source)
  };

  lex.mjs = lex.js;
  lex.cjs = lex.js;
  lex.json = lex.js;

  const lexer = (globalThis.lexer = lex[type]);
  if(debug >= 3) console.log('lexer', lexer);

  // T = lexer.tokens.reduce((acc, name, id) => ({ ...acc, [name]: id }), {});

  let e = new SyntaxError();

  if(debug > 1) console.log('bytebuf', bytebuf);

  if(!lexer) {
    throw new Error(`Error lexing: ${source}`);
  }

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
          if(stack.last != table[tok.lexeme])
            throw new Error(`top '${stack.last}' != '${tok.lexeme}' [ ${stack.map(s => `'${s}'`).join(', ')} ]`);

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
    line = [],
    showToken = tok => {
      if(
        (lexer.constructor != ECMAScriptLexer && tok.type != 'whitespace') ||
        /^((im|ex)port|from|as)$/.test(tok.lexeme)
      ) {
        let a = [/*(file + ':' + tok.loc).padEnd(file.length+10),*/ tok.type.padEnd(20, ' '), escape(tok.lexeme)];
        // std.puts(a.join('') + '\n');
      }
    };

  const PathAdjust = s => {
    let j = path.join(dir, s);
    j = path.normalize(j);
    j = path.resolve(j);
    if(path.isRelative(j)) j = './' + j;
    return ModuleLoader(j);
  };

  //console.log('lexer.peek()', lexer.tokens[lexer.peek()]);

  const addIdentifier = token => {
    if(used && !used.has(token.lexeme)) {
      let { size } = used;
      used.add(token.lexeme);
      if(debug >= 2) log(`added[${size}] \x1b[1;33m${token.lexeme}\x1b[0m`);
    }
  };

  let prevToken,
    sheBang,
    doneImports,
    used = identifiersUsed ? identifiersUsed(source) : null;
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

    if(!token) break;
    const { length, seq } = token;

    if(token.type == 'shebang') {
      sheBang = token;
      continue;
    }

    if(debug > 2) log(`token '${token.lexeme}'`, { doneImports });
    if(n == 0 && token.lexeme == '}' && lexer.stateDepth > 0) {
      lexer.popState();
    } else {
      balancer(token);
      if(n > 0 && balancers.last.depth == 0) log('balancer');
      if(/comment/i.test(token.type)) {
        comments.push(token);
        continue;
      }

      if(!doneImports) {
        if(!cond && ['import', 'export'].indexOf(token.lexeme) >= 0) {
          if(imp.length == 0) {
            //console.log(``, { token:token.lexeme, loc: token.loc+'', imp: imp.length});
            impexp = What[token.lexeme.toUpperCase()];
            cond = true;
            imp = token.lexeme == 'export' ? [token] : [];
          }
        }

        if(cond && imp[0] && imp[0].lexeme == 'export' && imp[1]) {
          if(imp[1].type != 'identifier' && imp[1].type != 'punctuation') {
            cond = false;
            line.push(...imp);
          }
        }
      }

      if(debug > 3)
        log(`token[${imp.length}]`, token.loc + '', console.config({ breakLength: 80, compact: 0 }), token);

      if(token.lexeme == ';' && cond !== true) {
        doneImports = true;

        for(let tok of line) if(tok.type == 'identifier') addIdentifier(tok);

        //console.log('line', line.map(t => t.lexeme).join(', '));
      } else {
        line.push(token);
      }

      if(token.type == 'punctuator' && token.lexeme == ';') line.slice(0, line.length);

      if(doneImports) {
        if(onlyImports) break;

        if(token.type == 'identifier') addIdentifier(token);
      }

      if(cond == true) {
        if((token.type != 'keyword' || imp.indexOf(token) == -1) && token.type != 'comment') imp.push(token);

        //console.log( imp[0].loc+'',console.config({breakLength:80, compact: 0}), NonWS(imp));
        if(imp.last.lexeme == ';') {
          let obj;
          cond = false;

          if(impexp == What.IMPORT || IsImportExportFrom(imp)) {
            if(imp[1].lexeme != '(') {
              obj = new Import(imp, PathAdjust, depth);
              if(obj) {
                obj.source = source;
                imports.push(obj);
              }
            }
          } else {
            obj = new Export(imp, PathAdjust, depth);
            if(obj) {
              obj.source = source;

              exports.push(obj);
            }
          }
          //console.log('obj',console.config({breakLength:80, compact: 1}), obj, obj.loc+'');
          imp.splice(0, imp.length);
          line.splice(0, line.length);
        }
      }

      prevToken = token;
    }

    state = newState;
  }

  let end = Date.now();

  if(showTiming) console.log('Lexing ' + source.replace(/^\.\//, '') + ' took ${end - start}ms');
  start = Date.now();

  let exportsFrom = exports.filter(exp => exp.tokens).filter(exp => exp.tokens.some(tok => tok.lexeme == 'from'));

  if(path.isRelative(source) && !/^(\.|\.\.)\//.test(source)) source = './' + source;

  let allExportsImports = exports.concat(imports).sort((a, b) => a.range[0] - b.range[0]);
  let fileImports = allExportsImports.filter(imp => typeof imp.file == 'string');
  let splitPoints = unique(fileImports.reduce((acc, imp) => [...acc, ...imp.range], []));
  buffers[source] = [...split(BufferFile(source), ...splitPoints)].map(b => b ?? toString(b, 0, b.byteLength));

  importsFor[source] = allExportsImports.filter(imp => imp.type == What.IMPORT); //fileImports.map(i => i.file);

  if(debug >= 1)
    console.log(
      `importsFor[${source}]`,
      console.config({ compact: false, maxStringLength: 30 }),
      importsFor[source].map(imp => imp.code)
    );

  let map = FileMap.for(source);
  let imported = used && new Set();

  if(sheBang) {
    const { byteRange, loc } = sheBang;
    const { byteOffset, byteLength } = sheBang;
    console.log('sheBang =', { sheBang, byteRange, byteOffset, byteLength });
    map.replaceRange(byteRange, null);
    globalSheBang = sheBang;
  }

  /* for(let impexp of fileImports) {
    let file = ResolveAlias(impexp.file);
    let s = (globalImports[file] ??= new Set());

    for(let id of impexp.ids()) s.add(id);
  }*/

  if(used) {
    for(let impexp of allExportsImports) {
      if(impexp.type == What.IMPORT) {
        let ids = impexp.ids();
        for(let id of ids) imported.add(id);
      }
    }

    let numReplace = 0;
    for(let impexp of allExportsImports) {
      if(impexp.type == What.IMPORT) {
        let ids = impexp.ids();

        if(ids.some(id => !used.has(id))) {
          let { code, range, file } = impexp;
          let newCode = impexp.toCode(id => used.has(id));
          if(code != newCode) {
            map.replaceRange(range, toArrayBuffer(newCode));
            ++numReplace;
          }
        }
      }
    }

    if(debug >= 1) console.log(`imported [ ${source} ]`, console.config({ compact: 1 }), [...imported]);
    used = intersection(used, imported);
    if(debug >= 1) console.log(`used     [ ${source} ]`, console.config({ compact: 1 }), [...used]);

    let unused = difference(imported, used);

    if(debug >= 1) console.log(`unused   [ ${source} ]`, console.config({ compact: 1 }), [...unused]);

    if(numReplace) {
      let out = FileReplacer(source);
      let result = map.write(out);
      out.close();
    }
  } else {
    for(let impexp of allExportsImports) {
      const { type, file, range, code, loc } = impexp;
      const [start, end] = range;
      // let bytebuf = BufferFile(source);
      let bufstr = toString(bytebuf.slice(...range));
      let arrbuf = toArrayBuffer(bufstr);

      let replacement = type == What.EXPORT ? null : /*FileMap.for*/ file;
      let { byteOffset } = loc;
      let p;

      /* prettier-ignore */ if(debug > 3) console.log('impexp', compact(2), { code, range: new NumericRange(...range), replacement, loc: loc + ''});

      if(typeof file == 'string' && !IsFileImport(file)) {
        if(debug > 1) console.log(`\x1b[1;31mInexistent\x1b[0m file '${file}'`);

        // if(printFiles) std.puts(`${path.resolve(source)}: ${file}\n`);

        replacement = null;
      } else if(file && path.isFile(file)) {
        replacement = file;
      } else if(
        (typeof replacement == 'string' && !path.isFile(replacement)) ||
        type == What.IMPORT ||
        typeof file == 'string'
      ) {
        replacement = null;
      } else if(code.startsWith('export')) {
        if(!removeExports) continue;
        replacement = file;
      }

      let list = type == What.EXPORT ? footer : header;
      list.push(impexp);

      /* prettier-ignore */ if(debug >= 2) console.log('impexp', compact(2), { code, range: new NumericRange(...range), replacement, loc: loc + ''});
      /* prettier-ignore */ if(debug > 1) console.log('impexp', compact(1), { range: new NumericRange(...range), loc: loc + ''});

      let str;
      if(range) str = toString(map.buffer.slice(...range));

      map.replaceRange(range, replacement);
    }
  }

  if(removeComments && comments.length) {
    i = -1;
    debugLog(`Removing ${comments.length} comments from '${source}'`);
    for(let { byteRange, lexeme } of comments) {
      let sl = bytebuf.slice(...byteRange);
      if(debug > 1)
        debugLog(`comment[${++i}]`, compact(2), {
          byteRange,
          str: toString(sl)
        });

      map.replaceRange(byteRange, null);
    }
  }

  end = Date.now();

  // console.log(`Substituting '${source.replace(/^\.\//, '')}' took ${end - start}ms`);
  processed.add(source);

  if(recursive > 0) {
    for(let imp of fileImports) {
      let { file, range, tokens } = imp;

      if(readPackage) {
        let alias = ResolveAlias(file);
        if(alias != file) {
          console.log(`\x1b[1;31mAliasing '${file}' to '${alias}'\x1b[0m`);
          file = alias;
        }
      }

      if(!/\.|\//.test(file)) {
        //console.log(`Builtin module '${file}'`);
        continue;
      }

      if(!path.isFile(file)) {
        console.log(`Path must exist '${file}'`);
        continue;
      }

      if(processed.has(file) || file == source) {
        //console.log(`Already processed '${file}'`);
        continue;
      }

      file = NormalizePath(file);
      file = ModuleLoader(file);
      // file = path.resolve(file);

      if(file) {
        if(debug > 2) console.log(`Processed`, { file });

        if(!processed.has(file)) {
          processed.add(file);

          AddDep(source, file);

          if(debug >= 1) console.log(`Recursing`, { source, file });

          if(/\.(so|dll)$/i.test(file)) {
            if(printFiles) std.puts(`${path.resolve(file)}\n`);
          } else {
            let ret = ProcessFile(file, log, typeof recursive == 'number' ? recursive - 1 : recursive, depth + 1);

            //    console.log('ret',ret);
          }

          if(printImports) {
            const ids = imp.ids();

            std.puts(
              `${path.resolve(source) + ':' /*.padEnd(30, ' ')*/} ${path.resolve(file) /*.padEnd(30)*/} ${ids.join(
                ' '
              )}\n`
            );
          }
        }
        //  os.kill(process.pid, os.SIGUSR1)
      }
    }
  }

  std.gc();

  if(showDeps) {
    let deps = [...DependencyTree(source, ' ', false, 0, '    ')];
    console.log(`Dependencies of '${source}':\n${SpreadAndJoin(deps)}`);
    //console.log(`dependencyMap`,[...dependencyMap]);
  }

  return (modules[source] = { imports, exports, map });
}

function AddDep(source, file) {
  source = NormalizePath(source);

  if(debug > 2) console.log(`Add dependency '${file}' to '${source}'`);
  dependencyTree(source).push(file);
}

function IsWS(tok) {
  return tok.type == 'whitespace';
}

function NonWS(tokens) {
  return tokens.filter(tok => !IsWS(tok));
}

function LeadingWS(str) {
  let m = str.match(/[^\s]/);
  return m?.index ?? 0;
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

function AddWhitespace(tokens) {
  return tokens.reduce((acc, tok) => {
    if(acc.length) {
      if(tok.lexeme != ',')
        if(!IsWS(acc[acc.length - 1]) && !IsWS(tok)) acc.push({ type: 'whitespace', lexeme: ' ' });
    }

    acc.push(tok);
    return acc;
  }, []);
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
    return new Range(
      args[0].file,
      Math.min(...args.map(r => (Array.isArray(r) ? r[0] : r.start))),
      Math.max(...args.map(r => (Array.isArray(r) ? r[1] : r.end)))
    );
  }
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
  [inspectSymbol](depth, opts) {
    const [start, end] = this;
    let s = '';
    const pad = s => (s + '').padEnd(5);
    s += `\x1b[1;36m${pad(start)}\x1b[0m`;
    s += ` - `;
    s += `\x1b[1;36m${pad('+' + (end - start))}\x1b[0m`;
    s = `[ ${s} ]`;
    return s;
  }
});

class FileMap extends Array {
  static buffers = mapWrapper(fileBuffers);
  static filenames = mapWrapper(bufferRef);

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
        [Symbol.toStringTag]: 'FileMap(empty)'
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
      console.log('splitAt', { i });
      let [range, buf] = this[i];
      let [start, end] = range;

      if(start === pos) return i;

      this.splice(i + 1, 0, [new NumericRange((range[1] = pos), end), buf]);
      return i + 1;
    }
    return -1;
  }

  findPos(pos) {
    const n = this.length;

    for(let i = 0; i < n; i++) {
      let [range, buf] = this[i];
      let next = this[i + 1];
      if(range) {
        let [start, end] = range;
        if(pos < end) return i;
      }
    }
    return -1;
  }

  insertAt(pos, ...args) {
    let i;
    let entries = args.map(file => (types.isArrayBuffer(file) ? [null, file] : [null, null, file]));

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
    ///* prettier-ignore */ if(debug > 1) console.log('FileMap.replaceRange', compact(2, { customInspect: true }), { file, range: [range[0], range[1]] });

    let i = sliceIndex(range[0]);
    let j = sliceIndex(range[1]);

    const { length } = this;

    if(range[0] < this[i][0]) range[0] = this[i][0];

    if(!this[i][0])
      throw new Error(
        `range=${range}\nlength=${this.length}\nstart=${i}\nend=${j}\nthis[${i}]=${inspect(this[i])}\nthis[${
          i - 1
        }]=${inspect(this[i - 1])}\nthis[${i + 1}]=${inspect(this[i + 1])}`
      );

    let [, buf] = this[i];
    let empty = typeof file != 'string';
    let entry = [range, null].concat(empty ? [] : [file]);

    if(range[0] > this[i][0][0]) {
      if(i == j) {
        let [range] = this[i];
        let insert = [new NumericRange(...range), buf];
        this.splice(++j, 0, insert);
      }
      this[i][0][1] = range[0];

      if(this[j] && this[j][0]) this[j][0][0] = range[1];
    } else {
      this[i][0][0] = range[1];
      let [[start, end]] = this[i];

      let remain = end - start;

      this.splice(i, remain == 0 ? 1 : 0, ...(empty ? [] : [entry]));
      file = null;
    }

    if(file != null) this.splice(i + 1, 0, entry);

    // if(debug > 2) console.log('FileMap.replaceRange', console.config({ compact: 10 }), { i, j, length, this: this });

    //if(this.rangeAt(0)[0] != 0) throw new Error('Inconsistent FileMap');
  }

  stringAt(n) {
    let arr = [];
    let wr = ArrayWriter(arr, (buf, len) => toString(buf, len));
    let item = this.at(n) ?? '';

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
    let r = [];
    let { length } = this;
    for(let i = 0; i < length; i++) r.push(this[i][0] === null ? null : this.rangeAt(i));
    return r;
  }

  *[Symbol.iterator]() {
    let { length } = this;

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
    let ranges = [...this.map(([range]) => range)].sort(CompareRange);
    let iter = NumericRange.holes(ranges, true);
    let holes = [...iter];
    let len = holes.length;
    for(let i = 0; i < len; i++) {
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
    if(debug > 3) debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, compact(1), { out, depth, serial });
    let r,
      written = 0;
    let { length } = this;
    serial ??= randInt(0, 1000);
    if(this.serial === serial) return 0;
    this.serial = serial;
    if(typeof out == 'string') out = FileWriter(out);
    let first = this.firstChunk();
    let last = this.lastChunk();

    if(debug > 3)
      debugLog(`FileMap\x1b[1;35m<${this.file}>\x1b[0m.write`, compact(2), {
        chunks: [this.at(first), this.at(last)]
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
        throw new Error('error writing ' + len + ' bytes: ' + inspect(err));
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
    const n = this.length;
    let s = '';
    for(let i = 0; i < n; i++) s += this.stringAt(i);
    return s;
  }

  [Symbol.for('blah')](depth, opts) {
    opts = {
      ...opts,
      compact: 2,
      maxStringLength: 40,
      stringBreakNewline: false,
      customInspect: false
    };

    let arr = [...this].map((item, i) => {
      let range, buf, replacement;
      try {
        [range, buf, replacement] = [...item];
      } catch(e) {
        buf = item;
      }
      const isBuf = isObject(buf) && types.isArrayBuffer(buf);
      const filename = isBuf ? FileMap.filenames(buf) : undefined;

      return [
        types.isArrayBuffer(buf) ? range : null,
        filename ? `<Buffer[\x1b[1;36m${buf.byteLength}\x1b[1;0m] \x1b[1;33m'${filename}'\x1b[0m>` : buf,
        replacement
      ]
        .slice(0, item.length)
        .map((part, i) =>
          padStartAnsi(
            typeof part == 'string' ? part : inspect(part, opts) + (i < item.length - 1 ? ',' : ''),
            [20, 34, 0][i]
          )
        )
        .join(' ');
    });

    return (
      `FileMap\x1b[1;35m<${this.file}>\x1b[0m [\n  ` +
      arr.map((item, i) => `[ ${('#' + i).padStart(3)} ${item} ]`).join(',\n  ') +
      `\n]\n`
    );
  }
}

FileMap.prototype[Symbol.toStringTag] = 'FileMap';

function BufferFile(file, buf) {
  file = path.resolve(file);
  if(!buf) buf = fileBuffers.get(file) ?? buffers[file];
  if(!buf) buf = buffers[file] ?? fs.readFileSync(file, { flag: 'r' });
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

  let fd = fs.openSync(name, os.O_WRONLY | os.O_TRUNC | os.O_CREAT, mode);
  let r = 0;
  for(let item of data) {
    r += fs.writeSync(fd, toArrayBuffer(item + ''));
  }
  fs.closeSync(fd);
  let stat = fs.statSync(name);
  if(verbose) debug(`Wrote ${name}: ${stat?.size} bytes`);
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

function* DependencyTree(
  root,
  indent = ' ',
  spacing = false,
  depth = 0,
  pre = '',
  fn = (name, depth) => `${name} (${depth})`
) {
  if(!Array.isArray(dependencyTree(root)))
    throw new Error(`No such file '${root}' in dependency Map ([${[...dependencyMap.keys()]}])`);

  if(depth == 0) yield pre + stripLeadingDotSlash(fn(root, depth)) + `\n`;

  let a = dependencyMap.get(root);
  let i = 0,
    n = a.length;

  for(i = 0; i < n; i++) {
    if(spacing) yield (pre + indent + `│  ` + '\n').repeat(Number(spacing));

    yield pre +
      indent +
      (n == 1 ? `└─ ` : i < n - 1 ? `├─ ` : `└─ `) +
      stripLeadingDotSlash(fn(a[i], depth + 1)) +
      '\n';

    yield* DependencyTree(
      a[i],
      indent,
      spacing,
      depth + 1,
      pre + indent + (n == 1 ? `   ` : i < n - 1 ? `│  ` : `   `)
    );
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
    grant: 'none'
  };

  let keys = Object.keys(defaults).concat(Object.keys(fields)).unique();
  yield `// ==UserScript==`;
  for(let name of keys) {
    const value = fields[name] ?? defaults[name];

    yield `// @${name.padEnd(12)} ${value}`;
  }

  yield `// ==/UserScript==`;
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
  Object.assign(globalThis, {
    IsKeyword,
    IsPunctuator,
    IsBuiltin,
    IsImportExportFrom,
    IsIdentifier,
    toString,
    NonWS,
    LeadingWS,
    ImportFile,
    ImportType,
    ImportIds,
    ImportIdMap,
    FileMap,
    NumericRange,
    ArrayWriter,
    DummyWriter,
    FdWriter,
    FileWriter,
    OutputImports,
    ResolveAlias,
    Range,
    MergeImports
  });

  globalThis.console = new Console(std.err, {
    inspectOptions: {
      colors: true,
      depth: 2,
      stringBreakNewline: true,
      maxStringLength: 1000,
      maxArrayLength: 100,
      compact: 1,
      hideKeys: [Symbol.toStringTag /*, 'code'*/]
    }
  });
  let optind = 0,
    code = 'c',
    exp = true;

  let out /* = FdWriter(1, 'stdout')*/;
  let stream = define(
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
      }
    }
  );

  let opts;
  let params = (globalThis.params = getOpt(
    (opts = {
      help: [false, null, 'h'],
      debug: [false, () => ++debug, 'x'],
      interactive: [false, null, 'y'],
      log: [true, file => (logFile = FileWriter(file)), 'v'],
      sort: [false, null, 's'],
      'case-sensitive': [false, null, 'c'],
      quiet: [false, (quiet = (quiet | 0) + 1), 'q'],
      export: [false, () => (exp = true), 'e'],
      imports: [false, () => (onlyImports = true), 'i'],
      'relative-to': [true, arg => (relativeTo = path.absolute(arg)), 'R'],
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
      'read-package': [false, () => (readPackage = true), 'p'],
      'no-read-package': [false, () => (readPackage = false), 'P'],
      userscript: [false, () => (userScript = true), 'U'],
      'remove-unused': [false, null, 'u'],
      time: [false, () => (showTiming = true), 't'],
      '@': 'files'
    }),
    args
  ));

  if(debug > 1)
    console.log('main', {
      debug,
      logFile,
      quiet,
      exp,
      onlyImports,
      relativeTo,
      outputFile,
      recursive,
      removeExports,
      removeImports,
      removeComments,
      globalExports,
      showDeps,
      printImports,
      printFiles,
      readPackage,
      userScript,
      showTiming
    });

  function ShowHelp() {
    let entries = Object.entries(opts);
    let maxlen = entries.reduce((acc, [name]) => (acc > name.length ? acc : name.length), 0);

    let s = Object.entries(opts).reduce(
      (acc, [name, [hasArg, fn, shortOpt]]) =>
        acc +
        (
          `    ${(shortOpt ? '-' + shortOpt + ',' : '').padStart(4, ' ')} --${name.padEnd(maxlen, ' ')} ` +
          (hasArg ? (typeof hasArg == 'boolean' ? 'ARG' : hasArg) : '')
        ).padEnd(40, ' ') +
        '\n',
      `Usage: ${path.basename(scriptArgs[0])} [OPTIONS] <FILES...>\n\n`
    );
    std.puts(s + '\n');
    std.exit(0);
  }

  if(params.help) ShowHelp();

  let doOutput = true;

  let files = params['@'];


  if(/check-import/.test(scriptArgs[0])) {
    if(printFiles === undefined) printFiles = false;
    onlyImports = false;
    outputFile = null;
    out = DummyWriter('/dev/null');
  } else if(/list-import/.test(scriptArgs[0])) {
    if(printFiles === undefined && printImports === undefined) printImports = true;
    //    if(printFiles === undefined) printFiles = true;
    quiet = true;
    onlyImports = true;
    outputFile = null;
    out = DummyWriter('/dev/null');
  } else if(!params.merge) {
    if(typeof recursive == 'undefined') recursive = true;
  }

  if(params.merge) {
    onlyImports = !params['remove-unused'];
  if(params['remove-unused'])
     identifiersUsed = globalThis.identifiersUsed ??= memoize(arg => new Set());
  } 


  //  console.log(scriptArgs[0], { printFiles, onlyImports });

  if(typeof recursive == 'undefined') recursive = false;

  const { sort, 'case-sensitive': caseSensitive } = params;

  if(outputFile) out = FileWriter(outputFile);
  //globalThis.out = out;

  let argList = [...scriptArgs];
  log = /*quiet ? () => {} :*/ (...args) => console.log(`${file}:`, ...args);

  logFile(`Start of: ${argList.join(' ')}\n`);

  const RelativePath = file => (path.isAbsolute(file) ? file : file.startsWith('./') ? file : './' + file);

  if(!files.length) throw new ArgumentError('Expecting argument <files...>');

  let results = (globalThis.results = []);

  for(let file of files) {
    file = RelativePath(file);
    file = path.resolve(file);

    let result = ProcessFile(file, (...args) => console.log(`${file}:`, ...args), recursive, 0);

    if(debug > 1) console.log('result', compact(false, { depth: Infinity }), result);

    results.push(result);

    if(params.merge) {
      const map = FileMap.for(file);
      map.reset();

      if(result.imports[0]) {
        const [start] = result.imports[0].range;

        for(let imp of result.imports) {
          map.replaceRange(imp.range, '');
        }

        const merged = MergeImports(result.imports);

        let used = identifiersUsed(file);

        const outstr = merged
          .map(i =>
            i.toString(local => {
              if(!used.has(local)) {
                console.log(`Removing unused import identifier '${local}' imported from '${i.file}'`);
                return false;
              }
              return true;
            })
          )
          .filter(istr => istr !== '')
          .join('\n');

        //map.insertAt(start, ...merged.map(imp => toArrayBuffer(imp + '')));
        map.insertAt(start, toArrayBuffer(outstr));
        map.trim();

        out ??= FileReplacer(file);

        map.write(out);
        out.close();
      }

      doOutput = false;
    }
  }
  if(debug > 1) console.log('results', console.config({ compact: 1 }), results);

  /*console.log('header', console.config({ compact: 2 }), header);
  console.log('globalImports', console.config({ compact: 1 }), globalImports);*/

  for(let imp of allImports()) {
    let { file, source, tokens } = imp;
    try {
      let idmap = imp.idmap();
      //console.log('imp', { file, source, idmap });
      if(IsFileImport(file)) continue;
      let map = (globalImports[ResolveAlias(file)] ??= {});

      if(idmap) for(let [local, name] of idmap) map[local] = name;
    } catch(e) {}
  }

  if(doOutput) {
    let str = OutputImports(globalImports);
    console.log('OutputImports() =', str);

    out ??= FdWriter(1, 'stdout');

    //  results[0].insert(1, toArrayBuffer(str));

    if(globalSheBang) {
      out.puts(globalSheBang.lexeme);
    }

    if(!removeImports) out.puts(str + '\n');

    if(userScript) {
      for(let line of PrintUserscriptBanner({
        name: out.file,
        'run-at': 'document-start',
        version: '1.0',
        description: files.join(', '),
        downloadURL: `https://localhost:9000/${out.file}`,
        updateURL: `https://localhost:9000/${out.file}`
      }))
        stream.puts(line);

      stream.puts("\n(function() {\n  'use strict';\n");
      ++stream.indent;
    }

    if(debug > 1) console.log('out', out, out.file);

    if(out.file) {
      /* console.log('results[0]', results[0]);
      console.log('results[0].map', results[0].map);*/
      let nbytes;
      try {
        nbytes = results[0].map.write(stream);
      } catch(error) {
        console.log(`write error ('${out.file}'):`, error);
        throw error;
      }

      if(debug >= 1) console.log(`${nbytes} bytes written to '${out.file}'`);
    }

    if(globalExports) {
      let maxDepth = globalExports - 1;
      let exportedNames = footer.filter(({ depth }) => depth <= maxDepth);
      // console.log(`footer`, exportedNames.map(impexp =>  [impexp.depth,impexp.name]));

      if(debug > 3) console.log(`exportedNames`, exportedNames);

      exportedNames = exportedNames
        .map(({ name }) => name)
        .unique()
        .filter(name => typeof name == 'string');

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
  console.log(`${error.constructor.name}: ${error.message}${error.stack ? '\n' + error.stack : ''}`);
  os.kill(process.pid, os.SIGUSR1);
} finally {
}
