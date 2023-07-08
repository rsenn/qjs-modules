import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import { camelize } from 'util';
import { curry } from 'util';
import { define } from 'util';
import { escape } from 'util';
import { extendArray } from 'util';
import { getOpt } from 'util';
import { split } from 'util';
import { startInteractive } from 'util';
import { toString } from 'util';
import { unique } from 'util';
import { Console } from 'console';
import inspect from 'inspect';
import { Lexer } from 'lexer';
import { Token } from 'lexer';
import ECMAScriptLexer from 'lexer/ecmascript.js';
import * as std from 'std';
#!/usr/bin/env qjsm
let buffers = {},
  modules = {};
let T,
  log,
  code = 'c',
  debug,
  verbose,
  caseSensitive,
  quiet,
  exp,
  relativeTo,
  printFiles,
  filter = null,
  match = new Set(),
  identifiers;

('use strict');
('use math');

extendArray(Array.prototype);

const AddUnique = (arr, item) => (arr.indexOf(item) == -1 ? arr.push(item) : null);

const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);

const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

const bufferRef = new WeakMap();

function compareFn() {
  let compare = (a, b) => ('' + a).localeCompare('' + b);

  if(!caseSensitive) {
    let fn = compare;
    compare = (...args) => fn(...args.map(s => ('' + s).toLowerCase()));
  }
  return compare;
}

function BufferFile(file) {
  //console.log('BufferFile', file);
  if(buffers[file]) return buffers[file];
  let b = (buffers[file] = fs.readFileSync(file, { flag: 'r' }));
  //console.log('bufferRef', bufferRef, bufferRef.set, b);
  if(typeof b == 'object' && b !== null) bufferRef.set(b, file);
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

const TokIs = curry((type, lexeme, tok) => {
  if(lexeme != undefined) {
    if(typeof lexeme == 'string' && tok.lexeme != lexeme) return false;
    else if(Array.isArray(lexeme) && lexeme.indexOf(tok.lexeme) == -1) return false;
  }
  if(type != undefined) {
    if(typeof type == 'string' && tok.type != type) return false;
    if(typeof type == 'number' && tok.id != type) return false;
  }
  return true;
});

const IsKeyword = TokIs('keyword');
const IsPunctuator = TokIs('punctuator');
const IsIdentifier = TokIs('identifier');
const IsStringLiteral = TokIs('stringLiteral');

function ImportType(seq) {
  if(IsKeyword('import', seq[0])) seq.shift();
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
  seq = seq.filter(({ type }) => type != 'whitespace');
  let idx = seq.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok));
  if(seq[idx + 1] && IsKeyword('as', seq[idx + 1])) idx += 2;
  let ret = seq[idx]?.lexeme;
  return ret;
}

function IsWS(tok) {
  return tok.type == 'whitespace';
}

function NonWS(tokens) {
  return tokens.filter(tok => !IsWS(tok));
}

function RemoveKeyword(tokens) {
  if(tokens[0].type == 'keyword') return tokens.shift();
}

function HasStar(tokens) {
  if(tokens.some(({ type, lexeme }) => type == 'punctuator' && lexeme == '*')) return '*';
}

function HasFrom(tokens) {
  let idx;
  if((idx = tokens.findIndex(tok => IsKeyword('from', tok))) != -1) {
    return tokens[idx + 1].lexeme.slice(1, -1);
  }
}

function AddExport(tokens) {
  let code = tokens.map(tok => tok.lexeme).join('');
  tokens = NonWS(tokens);
  let offset = tokens.findIndex(tok => IsIdentifier(undefined, tok) || IsKeyword('default', tok));
  if(offset != -1) tokens = tokens.slice(0, offset + 1);
  let file = HasFrom(tokens);
  let exported = ExportName(tokens) ?? HasStar(tokens) ? ModuleExports(file) : undefined;
  let exp = define(
    {
      type: ImportType(tokens),
      tokens,
      exported,
      range: [+tokens[0]?.loc, +tokens.last?.loc]
    },
    { code, loc: tokens[0]?.loc },
    file ? { file } : {}
  );
  /*  console.log('AddExport', { exp });
  os.kill(process.pid, os.SIGUSR1);
  globalThis.exp = exp;*/
  return exp;
}

function AddImport(tokens) {
  //console.log('tokens:', tokens);
  let range = [+tokens[0].loc, +tokens.last.loc],
    code = tokens.map(tok => tok.lexeme).join('');
  tokens = tokens.filter(tok => tok.type != 'whitespace');
  let type = ImportType(tokens),
    file = ImportFile(tokens);
  const { loc, seq } = tokens[0];
  let imp = define({ type, file, loc, seq, range }, { tokens, code });
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
      if(IsKeyword('import', tokens[idx])) ++idx;
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

function* GetTokens(file, pred = ({ type }) => type == 'identifier') {
  console.log('file', file);

  let str = BufferFile(file);
  let tok,
    lex = new ECMAScriptLexer(str, file);

  while((tok = lex.next())) {
    const { token } = lex;

    if(pred(token)) yield token;
  }
}

function* TransformLexeme(gen) {
  for(let token of gen) yield token.lexeme;
}

function* GetCommands(file) {
  let lex = GetTokens(file, ({ type }) => type != 'whitespace');
  let a = [];
  for(let tok of lex) {
    a.push(tok);

    if(tok.type == 'punctuator' && tok.lexeme == ';') {
      yield a;
      a = [];
    }
  }
}

function SortAndFilter(list, { sort, exclude, include, uppercase, 'case-sensitive': caseSensitive }) {
  if(sort) list.sort(compareFn());

  const flags = caseSensitive ? '' : 'i';

  if(exclude) {
    const re = new RegExp(exclude, flags);
    list = list.filter(n => !re.test(n));
  }

  if(include) {
    const re = new RegExp(include, flags);
    list = list.filter(n => re.test(n));
  }

  if(uppercase) list = list.filter(name => /^[A-Z]/.test(name));

  return list;
}

function ListExports(file, output, params) {
  if(printFiles) {
    std.out.puts(`${file}\n`);
    std.out.flush();
  }

  if(debug >= 3) log('ListExports', { file, output, log: log + '' });

  let str = file ? BufferFile(file) : code[1],
    len = str.length,
    type = path.extname(file).substring(1),
    base = camelize(path.basename(file, '.' + type).replace(/[^0-9A-Za-z_]/g, '_'));

  let lex = {
    js: new ECMAScriptLexer(str, file)
  };
  lex.es = lex.js;
  lex.mjs = lex.js;

  const lexer = (globalThis.lexer = lex[type]);

  T = lexer.tokens.reduce((acc, name, id) => ({ ...acc, [name]: id }), {});
  log('ListExports', { lexer, T, verbose });

  let e = new SyntaxError();

  lexer.handler = lex => {
    const { loc, mode, pos, start, byteLength, state } = lex;
    log(' '.repeat(loc.column - 1) + '^');
  };
  let tokenList = [],
    declarations = [];
  const colSizes = [12, 8, 4, 20, 32, 10, 0];
  const printTok =
    debug > 3
      ? (tok, prefix) => {
          const range = tok.charRange;
          const cols = [prefix, `tok[${tok.byteLength}]`, tok.id, tok.type, tok.lexeme, tok.lexeme.length, tok.loc];
          std.err.puts(cols.reduce((acc, col, i) => acc + (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i]), '') + '\n');
        }
      : () => {};

  let tok,
    i = 0,
    mask = IntToBinary(lexer.mask),
    state = lexer.topState();
  lexer.beginCode = () => (code == 'js' ? 0b1000 : 0b0100);
  let tokens = [],
    start = Date.now();
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
      if((lexer.constructor != ECMAScriptLexer && tok.type != 'whitespace') || /^((im|ex)port|from|as)$/.test(tok.lexeme)) {
        let a = [/*(file + ':' + tok.loc).padEnd(file.length+10),*/ tok.type.padEnd(20, ' '), escape(tok.lexeme)];
        std.err.puts(a.join('') + '\n');
      }
    };

  for(;;) {
    let { stateDepth } = lexer;
    let value = lexer.next();
    let done = value === undefined;
    //if(debug >= 2) log('ListExports', console.config({ compact: 2 }), { value, done });

    if(done) break;
    let newState = lexer.topState();
    //showToken(tok);
    if(newState != state) {
      if(state == 'TEMPLATE' && lexer.stateDepth > stateDepth) balancers.push(balancer());
      if(newState == 'TEMPLATE' && lexer.stateDepth < stateDepth) balancers.pop();
    }
    let n = balancers.last.depth;
    tok = lexer.token;

    if(tok == null) break;
    if(debug >= 4) log('ListExports', console.config({ compact: 2 }), tok);

    if(n == 0 && tok.lexeme == '}' && lexer.stateDepth > 0) {
      lexer.popState();
    } else {
      balancer(tok);
      if(n > 0 && balancers.last.depth == 0) log('balancer');
      if(['import', 'export'].indexOf(tok.lexeme) >= 0) {
        impexp = What[tok.lexeme.toUpperCase()];
        let prev = tokens[tokens.length - 1];
        cond = true;
        imp = [];
      }
      if(cond == true) {
        imp.push(tok);

        if([';', '\n'].indexOf(tok.lexeme) != -1) {
          //log('imp', imp);
          cond = false;

          if(imp.some(i => i.lexeme == 'from')) {
            if(impexp == What.IMPORT) imports.push(AddImport(imp));
          }

          if(impexp == What.EXPORT) exports.push(AddExport(imp));
        }
      }
      printTok(tok, newState);
      tokens.push(tok);
    }
    state = newState;
  }

  const exportTokens = tokens.reduce((acc, tok, i) => (tok.lexeme == 'export' ? acc.concat([i]) : acc), []);

  let exportNames = exportTokens.map(index => ExportName(tokens.slice(index))).filter(n => n !== undefined);

  log('Export names', exportNames);

  for(let exp of exports) {
    let { exported } = exp;

    if(exported) exportNames.push(...exported);
  }

  //log('Export names', exportNames);

  /*log('ES6 imports', imports.map(PrintES6Import));
    log('CJS imports', imports.map(PrintCJSImport));*/

  exportNames = SortAndFilter(exportNames, params);

  let idx;
  if((idx = exportNames.indexOf(base)) != -1 && exportNames.indexOf('default') != -1) {
    exportNames.splice(idx, 1);
    log(`\x1b[1;31mremoving '${base}'\x1b[0m`);
  }

  let source = file;

  if(relativeTo) {
    let rel = path.resolve(relativeTo);
    source = path.absolute(source);

    if(path.exists(rel) && !path.isDirectory(rel)) rel = path.dirname(rel);

    //log('\x1b[1;33mrelativeTo\x1b[0m', { rel, source });

    source = path.relative(source, rel);
  }

  if(path.isRelative(source) && !/^(\.|\.\.)\//.test(source)) source = './' + source;

  if(debug >= 3) log('exportNames', exportNames);

  if(exportNames.length) {
    let names = exportNames.map(t => (t == 'default' ? t + ' as ' + base : t));
    const keyword = exp ? 'export' : 'import';

    if(filter) names = names.filter(filter);

    if(identifiers) {
      identifiers.delete(...names);
      match.add(...names);
    }

    if(params.raw) output.puts(names.join('\n') + '\n');
    else if(names.length == 1 && /^default as/.test(names[0])) output.puts(keyword + ` ${base} from '${source}'\n`);
    else if(names.length > 0) output.puts(keyword + ` { ${names.join(', ')} } from '${source}'\n`);
  }

  modules[source] = { imports, exports };

  let fileImports = imports.filter(imp => /\.js$/i.test(imp.source));
  let splitPoints = unique(fileImports.reduce((acc, imp) => [...acc, ...imp.range], []));
  buffers[source] = [...split(BufferFile(source), ...splitPoints)].map(b => b ?? toString(b, 0, b.byteLength));

  //log('fileImports', fileImports.map(imp => imp.source));

  let dir = path.dirname(source);

  fileImports.forEach(imp => {
    let p = path.normalize(path.join(dir, imp.file));
    //log('p', p);

    AddUnique(files, p);
  });

  let end = Date.now();

  /*if(verbose)*/ log(`took ${end - start}ms`);

  //std.gc();
}

function ModuleExports(file) {
  let m;
  try {
    if((m = moduleList.find(m => new RegExp(file).test(getModuleName(m)))) || (m = loadModule(file))) {
      let list = getModuleExports(m);

      let keys = Object.keys(list);

      return keys;
    }
  } catch(error) {
    //log('ERROR', error.message + '\n' + error.stack);
  }
}

function main(...args) {
  Object.assign(globalThis, {
    buffers,
    modules,
    BufferFile,
    BufferLengths,
    BufferOffsets,
    BufferRanges,
    WriteFile,
    DumpLexer,
    DumpToken,
    ImportType,
    ImportFile,
    ExportName,
    IsWS,
    NonWS,
    AddExport,
    AddImport,
    PrintES6Import,
    PrintCJSImport,
    GetTokens,
    TransformLexeme,
    GetCommands,
    ListExports,
    HasFrom,
    HasStar,
    RemoveKeyword,
    ModuleExports,
    ProcessFile
  });

  globalThis.console = new Console(process.stderr, {
    inspectOptions: {
      colors: true,
      depth: 8,
      breakLength: 160,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: 0,
      stringBreakNewline: false,
      hideKeys: [Symbol.toStringTag /*, 'code'*/]
    }
  });

  let output = std.out,
    outputFile,
    params;

  params = getOpt(
    {
      help: [
        false,
        (__a, __x, params) => {
          console.log('help', { __a, __x, params });
          std.puts(`Usage: ${scriptArgs[0]} [OPTIONS] <files...>\n\n`);

          std.puts(params.map(([name, [hasArg, , letter]]) => `  -${letter}, --${name} ${(hasArg ? '<ARG>' : '').padEnd(10)}\n`).join('') + '\n');
          std.exit(0);
        },
        'h'
      ],
      debug: [false, () => (debug = (debug | 0) + 1), 'x'],
      verbose: [false, () => (verbose = (verbose | 0) + 1), 'v'],
      sort: [false, null, 's'],
      include: [true, (v, old) => (old ? old + '|' : '') + v, 'I'],
      exclude: [true, (v, old) => (old ? old + '|' : '') + v, 'X'],
      'case-sensitive': [false, () => (caseSensitive = true), 'c'],
      raw: [false, null, 'R'],
      quiet: [false, () => (quiet = true), 'q'],
      export: [false, () => (exp = true), 'e'],
      for: [true, null, 'f'],
      'print-files': [false, () => (printFiles = true), 'p'],
      output: [true, filename => (outputFile = filename) /* output = std.open(filename, 'w+')*/, 'o'],
      'relative-to': [true, arg => (relativeTo = path.absolute(arg)), 'r'],
      uppercase: [false, null, 'u'],
      interactive: [false, null, 'y'],
      '@': 'files'
    },
    args
  );
  let files = params['@'];

  console.log('params', params);

  if(outputFile) output = std.open(outputFile, 'w+');

  const RelativePath = file => path.join(path.dirname(process.argv[1]), '..', file);

  if(!files.length) files.push(RelativePath('./lib/util.js'));

  if(params['for']) {
    identifiers = new Set(TransformLexeme(GetTokens(params['for'])));
    //console.log('identifiers', identifiers);

    filter = (() => {
      const re = new RegExp('^(' + [...identifiers].join('|') + ')$');

      return name => re.test(name);
    })();
  }

  for(let file of files) {
    log = quiet ? () => {} : (...args) => console.log(`${file}:`, console.config({ compact: 10 }), ...args);

    try {
      ProcessFile(file);
    } catch(error) {
      console.log('ERROR:', error.message + '\n' + error.stack);
    }

    if(params.interactive) startInteractive();
  }

  function ProcessFile(file) {
    if(!/\.(js|es|mjs)$/.test(file)) {
      let keys;
      if((keys = ModuleExports(file))) {
        keys = SortAndFilter(keys, params);

        if(params.raw) output.puts(keys.join('\n') + '\n');
        else if(keys.length > 0) output.puts(`import { ${keys.join(', ')} } from '${file}'\n`);
        return;
      }
    }
    ListExports(file, output, params);
  }
  /*  if(identifiers.size) {
    std.err.puts(`${identifiers.size} identifiers could not be matched:\n${[...identifiers].join('\n')}\n`);
  }*/
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}