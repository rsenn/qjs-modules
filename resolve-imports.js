import * as os from 'os';
import * as std from 'std';
import * as fs from 'fs';
import inspect from 'inspect';
import * as path from 'path';
import { Predicate } from 'predicate';
import { Location, Lexer, Token } from 'lexer';
import { Console } from 'console';
import JSLexer from './lib/jslexer.js';
import { escape, quote, toString, define, curry, unique, split, extendArray, camelize, decamelize } from './lib/util.js';

('use strict');
('use math');

let buffers = {},
  modules = {};
let T;

extendArray(Array.prototype);

const AddUnique = (arr, item) => (arr.indexOf(item) == -1 ? arr.push(item) : null);

const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

const bufferRef = new WeakMap();
const fileBuffers = new Map();
const fileMaps = new Map();

function NonWS(tokens) {
  return tokens.filter(tok => tok.type != 'whitespace');
}

function TokenSequence(tokens) {
  return NonWS(tokens).map(tok => tok.lexeme);
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

class FileMap extends Array {
  constructor(file, buf) {
    super();

    if(typeof file != 'number') {
      buf ??= fileBuffers.get(file);
      if(buf == -1) throw new Error(`FileMap buf == -1`);
      this.push([[0, buf.byteLength], buf]);
      fileMaps.set(file, this);
    }
  }

  static for(file, b) {
    if(fileMaps.has(file)) return fileMaps.get(file);
    return new FileMap(file, b);
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

  replaceRange([start, end], file) {
    const sliceIndex = n => {
      let r;
      if((r = this.findIndex(([range, buf]) => (range ? InRange(range, n) : false))) == -1) throw new Error(`replaceRange n=${n} r=${r}`);
      return r;
    };
    let i = sliceIndex(start);
    let j = sliceIndex(end);
    if(i == j) {
      let [range, buf] = this[i];
      let insert = [[...range], buf];
      this.splice(++j, 0, insert);
    }

    this[i][0][1] = start;
    this[j][0][0] = end;
    this.splice(i + 1, 0, [null, file]);
  }

  at(i) {
    const [range, buf] = this[i];

    if(!range) return ` \x1b[38;5;69m<${buf}>\x1b[0m `;
    if(!buf || !range) return null;

    const [start, end] = range;
    return toString(buf, start, end - start);
  }

  toString() {
    const n = this.length;
    let s = '',
      i;
    for(i = 0; i < n; i++) s += this.at(i);
    return s;
  }
}

function BufferFile(file) {
  //console.log('BufferFile', file);
  if(buffers[file]) return buffers[file];
  let b = (buffers[file] = fs.readFileSync(file, { flag: 'r' }));
  //console.log('bufferRef', bufferRef, bufferRef.set, b);
  if(typeof b == 'object' && b !== null) bufferRef.set(b, file);
  if(typeof b == 'object' && b !== null) fileBuffers.set(file, b);
  //FileMap.for(file, b);
  return b;
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
const What = {
  IMPORT: 0,
  EXPORT: 1
};

const ImportTypes = {
  IMPORT: 0,
  IMPORT_DEFAULT: 1,
  IMPORT_NAMESPACE: 2
};

const IsOneOf = curry((n, value) => (Array.isArray(n) ? n.some(num => num === value) : n === value));

const TokIs = curry((type, lexeme, tok) => {
  if(lexeme != undefined) {
    if(typeof lexeme == 'string' && !IsOneOf(lexeme, tok.lexeme)) return false;
  }
  if(type != undefined) {
    if(typeof type == 'string' && !IsOneOf(type, tok.type)) return false;
    if(typeof type == 'number' && !IsOneOf(type, tok.id)) return false;
  }
  return true;
});

const IsKeyword = TokIs('keyword');
const IsPunctuator = TokIs('punctuator');
const IsIdentifier = TokIs('identifier');
const IsStringLiteral = TokIs('stringLiteral');

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
  let idx = seq.findIndex(tok => IsKeyword('from', tok));
  while(seq[idx] && seq[idx].type != 'stringLiteral') ++idx;
  return seq[idx].lexeme.replace(/^[\'\"\`](.*)[\'\"\`]$/g, '$1');
}

function ExportName(seq) {
  let idx = seq.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok));
  return seq[idx]?.lexeme;
}

function AddExport(tokens, relativePath = s => s) {
  let code = LiteralSequence(tokens);
  tokens = NonWS(tokens);
  let len = tokens.length;
  if(tokens[1].lexeme != '{') len = tokens.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok)) + 1;

  let fromIndex = tokens.findIndex(tok => tok.lexeme == 'from');
  let file = fromIndex != -1 ? Unquote(tokens[fromIndex + 1].lexeme) : null;

  tokens = tokens.slice(0, len);

  const first = tokens[0],
    last = tokens.last;

  const range = [first.byteOffset, last.byteOffset + last.byteLength];

  //console.log('range',range);

  let exp = define(
    {
      type: tokens[1]?.lexeme,
      file: relativePath(file),
      tokens,
      exported: ExportName(tokens),
      range //: [+tokens[0]?.loc, +tokens.last?.loc]
    },
    { code /*, loc: tokens[0]?.loc*/ }
  );
  return exp;
}

function AddImport(tokens, relativePath = s => s) {
  //console.log('tokens:', tokens);
  const first = tokens[0],
    last = tokens.last;
  const range = [first.byteOffset, last.byteOffset + last.byteLength];

  let code = tokens.map(tok => tok.lexeme).join('');
  tokens = tokens.filter(tok => tok.type != 'whitespace');
  let type = ImportType(tokens),
    file = relativePath(ImportFile(tokens));
  const { loc, seq } = tokens[0];

  let imp = define({ type, file, loc, seq, range, tokens }, { code });
  imp.local = {
    [ImportTypes.IMPORT_NAMESPACE]: () => {
      let idx = tokens.findIndex(tok => IsKeyword('as', tok));
      return tokens[idx + 1].lexeme;
    },
    [ImportTypes.IMPORT_DEFAULT]: () => {
      let idx = tokens.findIndex(tok => IsKeyword('import', tok));
      return tokens[idx + 1].lexeme;
    },
    [ImportTypes.IMPORT]: () => {
      let idx = 0,
        specifier = [],
        specifiers = [];
      if(IsKeyword(['import', 'export'], tokens[idx])) ++idx;
      if(IsPunctuator('{', tokens[idx])) ++idx;
      for(; !IsKeyword('from', tokens[idx]); ++idx) {
        if(IsPunctuator([',', '}'], tokens[idx])) {
          if(specifier.length) specifiers.push(specifier);
          specifier = [];
        } else {
          specifier.push(tokens[idx].lexeme);
        }
      }
      return specifiers;
    }
  }[type]();

  return imp;
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
      breakLength: 160,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: false,
      stringBreakNewline: false,
      hideKeys: [Symbol.toStringTag /*, 'code'*/]
    }
  });

  let optind = 0,
    code = 'c',
    debug,
    sort,
    caseSensitive,
    quiet,
    exp,
    relativeTo,
    files = [];

  while(args[optind]) {
    if(args[optind].startsWith('-')) {
      if(/code/.test(args[optind])) code = args[++optind];
      else if(/(debug|^-x)/.test(args[optind])) debug = true;
      else if(/(sort|^-s)/.test(args[optind])) sort = true;
      else if(/(case|^-c)/.test(args[optind])) caseSensitive = true;
      else if(/(quiet|^-q)/.test(args[optind])) quiet = true;
      else if(/(export|^-e)/.test(args[optind])) exp = true;
      else if(/(relative|^-r)/.test(args[optind])) relativeTo = path.absolute(args[++optind]);
    } else files.push(args[optind]);

    optind++;
  }

  const RelativePath = file => path.join(path.dirname(process.argv[1]), '..', file);

  if(!files.length) files.push(RelativePath('lib/util.js'));

  console.log('files', files);

  for(let file of files) {
    ProcessFile(file, quiet ? () => {} : (...args) => console.log(`${file}:`, ...args));
  }

  let map = FileMap.for(files[0]);
  console.log(`FileMap.for(${files[0]})`, map.toString());
}

function ProcessFile(file, log = () => {}) {
  console.log('ProcessFile', file);
  const start = Date.now();
  const dir = path.dirname(file);

  let str = file ? BufferFile(file) : code[1],
    len = str.length,
    type = path.extname(file).substring(1),
    base = camelize(path.basename(file, '.' + type).replace(/[^0-9A-Za-z_]/g, '_'));

  let lex = {
    js: new JSLexer(str, file)
  };

  const lexer = lex[type];

  T = lexer.tokens.reduce((acc, name, id) => ({ ...acc, [name]: id }), {});

  let e = new SyntaxError();

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
  let tokens = [];
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
    //    console.log('PathAdjust', { dir, s });
    return path.collapse(path.join(dir, s));
  };

  for(;;) {
    let { stateDepth } = lexer;
    let value = lexer.next();
    let done = value === undefined;

    //    log('value',{value,done});
    if(done) break;
    let newState = lexer.topState();
    //showToken(tok);
    if(newState != state) {
      if(state == 'TEMPLATE' && lexer.stateDepth > stateDepth) balancers.push(balancer());
      if(newState == 'TEMPLATE' && lexer.stateDepth < stateDepth) balancers.pop();
    }
    let n = balancers.last.depth;
    const { token } = lexer;
    const { loc, length } = token;
    const { pos } = loc;
    let s = toString(str).slice(pos, pos + length);
    //  console.log('',token.lexeme, {pos, s, length})

    if(n == 0 && token.lexeme == '}' && lexer.stateDepth > 0) {
      lexer.popState();
    } else {
      balancer(token);
      if(n > 0 && balancers.last.depth == 0) log('balancer');
      if(['import', 'export'].indexOf(token.lexeme) >= 0) {
        impexp = What[token.lexeme.toUpperCase()];
        let prev = tokens[tokens.length - 1];
        cond = true;
        imp = [];
      }
      if(cond == true) {
        imp.push(token);
        if([';', '\n'].indexOf(token.lexeme) != -1) {
          cond = false;
          if(imp.some(i => i.lexeme == 'from')) {
            if(impexp == What.IMPORT) imports.push(AddImport(imp, PathAdjust));
          }

          if(impexp == What.EXPORT) exports.push(AddExport(imp, PathAdjust));
        }
      }
      //        printTok(token, newState);
      tokens.push(token);
    }
    state = newState;
  }

  let exportsFrom = exports.filter(exp => exp.tokens).filter(exp => exp.tokens.some(tok => tok.lexeme == 'from'));
  let source = file;

  if(path.isRelative(source) && !/^(\.|\.\.)\//.test(source)) source = './' + source;

  /*log('exports', exportsFrom);
  log('imports', imports);*/

  modules[source] = { imports, exports };

  let fileImports = imports.filter(imp => /\.js$/i.test(imp.file));
  let splitPoints = unique(fileImports.reduce((acc, imp) => [...acc, ...imp.range], []));
  buffers[source] = [...split(BufferFile(source), ...splitPoints)].map(b => b ?? toString(b, 0, b.byteLength));

  fileImports.forEach(imp => {
    let p = imp.file;
    //  log('p', p);

    ProcessFile(imp.file, log);

    //      AddUnique(files, p);
  });

  let map = FileMap.for(file);

  for(let impexp of exportsFrom.concat(imports)) {
    const { file, range, code } = impexp;
    const [s, e] = range;

    console.log('impexp', impexp, { code, file, range: [s, e] });

    map.replaceRange([s + 1, e], file);
  }

  let end = Date.now();

  log(`took ${end - start}ms`);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
