import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import * as path from 'path';
import { Predicate } from 'predicate';
import { Location, Lexer, Token, SyntaxError } from 'lexer';
import { Console } from 'console';
import JSLexer from '../lib/jslexer.js';
import CLexer from '../lib/clexer.js';
import BNFLexer from '../lib/bnflexer.js';
import { extendArray } from 'util';
import { escape, quote } from 'misc';
import { define, curry } from 'util';

let imports = [];
let T;

('use strict');
('use math');

extendArray(Array.prototype);

const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

//const code = ["const str = stack.toString().replace(/\\n\\s*at /g, '\\n');", "/^(.*)\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);" ];
const code = [
  "const str = stack.toString().replace(/\\n\\s*at /g, '\\n');",
  '/Reg.*Ex/i.test(n)',
  '/\\n/g',
  'const [match, pattern, flags] = /^\\/(.*)\\/([a-z]*)$/.exec(token.value);',
  '/^\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);'
];

extendArray(Array.prototype);

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

  console.log('idx', idx);
  console.log('seq[idx]', seq[idx]);

  return seq[idx].lexeme.replace(/^['"`](.*)['"`]$/g, '$1');
}

function AddImport(tokens) {
  //console.log('tokens:', tokens);
  let code = tokens.map(tok => tok.lexeme).join('');
  tokens = tokens.filter(tok => tok.type != 'whitespace');
  let type = ImportType(tokens);
  let file = ImportFile(tokens);
  let imp = define(
    { type, file, loc: tokens[0].loc, range: [+tokens[0].loc, +tokens.last.loc] },
    { tokens, code }
  );
  imports.push(imp);
  switch (type) {
    case ImportTypes.IMPORT_NAMESPACE: {
      let idx = tokens.findIndex(tok => IsKeyword('as', tok));
      imp.local = tokens[idx + 1].lexeme;
      break;
    }
    case ImportTypes.IMPORT_DEFAULT: {
      let idx = tokens.findIndex(tok => IsKeyword('import', tok));
      imp.local = tokens[idx + 1].lexeme;
      break;
    }
    case ImportTypes.IMPORT: {
      let idx = 0;
      let specifier = [];
      imp.local = [];
      if(IsKeyword('import', tokens[idx])) ++idx;
      if(IsPunctuator('{', tokens[idx])) ++idx;
      for(; !IsKeyword('from', tokens[idx]); ++idx) {
        if(IsPunctuator([',', '}'], tokens[idx])) {
          if(specifier.length) imp.local.push(specifier);
          specifier = [];
        } else {
          specifier.push(tokens[idx].lexeme);
        }
      }
      break;
    }
    default: {
      imp.local = null;
    }
  }
  return imp;
  console.log('imp:', imp);
}

function PrintES6Import(imp) {
  let o = '';
  switch (imp.type) {
    case ImportTypes.IMPORT_NAMESPACE: {
      o += `import * as ${imp.local} from '${imp.file}';`;
      break;
    }
    case ImportTypes.IMPORT_DEFAULT: {
      o += `import ${imp.local} from '${imp.file}';`;
      break;
    }
    case ImportTypes.IMPORT: {
      o += `import { `;

      o += imp.local.join(', ');

      o += ` } from '${imp.file}';`;
      break;
    }
  }
  return o;
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      breakLength: 160,
      maxStringLength: 100,
      maxArrayLength: 40,
      compact: 1,stringBreakNewline: false,
      hideKeys: [Symbol.toStringTag /*, 'code'*/]
    }
  });

  let optind = 0;
  let code = 'c';
  let debug;

  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) code = args[++optind];
    if(/(debug|^-x$)/.test(args[optind])) debug = true;

    optind++;
  }

  const RelativePath = file => path.join(path.dirname(process.argv[1]), '..', file);

  let file = args[optind] ?? /*process.argv[1] ??*/ RelativePath('lib/util.js');
  console.log(`Loading '${file}'...`);

  let str = file ? std.loadFile(file, 'utf-8') : code[1];
  //str = '  return new Map(ret.map(([name, description]) => [name, { url: `https://github.com/${user}/${name}`, description }]));';
  let len = str.length;
  let type = path.extname(file).substring(1);
  console.log('data:', escape(str.slice(0, 100)));

  let lex = {
    js: new JSLexer(str, file),
    c: new CLexer(str, CLexer.LONGEST, file),
    bnf: new BNFLexer(str, file)
  };

  lex.g4 = lex.bnf;
  lex.ebnf = lex.bnf;
  lex.l = lex.bnf;
  lex.y = lex.bnf;

  const lexer = lex[type];

  T = lexer.tokens.reduce((acc, name, id) => ({ ...acc, [name]: id }), {});

  console.log('lexer:', lexer[Symbol.toStringTag]);
  console.log('code:', code);

  let e = new SyntaxError();
  console.log('new SyntaxError()', e);

  lexer.handler = lex => {
    const { loc, mode, pos, start, byteLength, state } = lex;
    //console.log(`${this.currentLine()}`);
    //console.log(`handler loc=${loc} mode=${IntToBinary(mode)} state=${lex.topState()}`, { pos, start, byteLength }, `\n${lex.currentLine()}` );
    console.log(' '.repeat(loc.column - 1) + '^');
  };
  let tokenList = [],
    declarations = [];
  const colSizes = [12, 8, 4, 16, 32, 10, 0];

  const printTok = debug
    ? (tok, prefix) => {
        const range = tok.charRange;
        const cols = [
          prefix,
          `tok[${tok.byteLength}]`,
          tok.id,
          tok.type,
          tok.lexeme,
          tok.lexeme.length,
          tok.loc
        ];
        console.log(
          ...cols.map((col, i) => (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i]))
        );
      }
    : () => {};

  let tok,
    i = 0;

  console.log('now', Date.now());

  console.log(lexer.ruleNames.length, 'rules', lexer.ruleNames.unique().length, 'unique rules');

  console.log('lexer.mask', IntToBinary(lexer.mask));
  console.log('lexer.skip', lexer.skip);
  console.log('lexer.skip', IntToBinary(lexer.skip));
  console.log('lexer.states', lexer.states);

  console.log(
    'new SyntaxError("test")',
    new SyntaxError('test', new Location(10, 3, 28, 'file.txt'))
  );
  let mask = IntToBinary(lexer.mask);
  let state = lexer.topState();
  lexer.beginCode = () => (code == 'js' ? 0b1000 : 0b0100);
  let tokens = [];
  let start = Date.now();
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
            throw new Error(
              `top '${stack.last}' != '${tok.lexeme}' [ ${stack.map(s => `'${s}'`).join(', ')} ]`
            );

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
  let balancers = [balancer()];
  let cond,
    imp = [];

  for(;;) {
    let newState, state;
    let { stateDepth } = lexer;
    state = lexer.topState();
    let { done, value } = lexer.next();
    if(done) break;
    newState = lexer.topState();
    tok = value;
    if(newState != state) {
      if(state == 'TEMPLATE' && lexer.stateDepth > stateDepth) balancers.push(balancer());
      if(newState == 'TEMPLATE' && lexer.stateDepth < stateDepth) balancers.pop();
    }

    //console.log('lexer.topState()', state);
    let n = balancers.last.depth;
    if(n == 0 && tok.lexeme == '}' && lexer.stateDepth > 0) {
      lexer.popState();
      continue;
    } else {
      balancer(tok);

      if(n > 0 && balancers.last.depth == 0) console.log('balancer');
    }
    //console.log('loc', lexer.loc+'');

    if(tok.lexeme == 'import') {
      let prev = tokens[tokens.length - 1];
      if(tokens.length == 0 || prev.lexeme.endsWith('\n')) {
        cond = true;
        imp = [];
      }
    }

    if(cond == true) {
      /*  if(tok.lexeme.trim() != '')*/ imp.push(tok);

      if([';', '\n'].indexOf(tok.lexeme) != -1) {
        cond = false;

        if(imp.some(i => i.lexeme == 'from')) {
          AddImport(imp);
        }
      }
    }

    printTok(tok, lexer.topState());
    tokens.push(tok);
  }
  console.log('imports', imports.map(PrintES6Import));
  let end = Date.now();

  console.log(`took ${end - start}ms`);
  console.log('lexer', lexer);
  console.log('lexer.tokens');
  /*// console.log('lexer.rules', new Map(lexer.ruleNames.map(name => lexer.getRule(name)).map(([name, expr, states]) => [name, new RegExp(expr, 'gmy'), states.join(',')])));
  console.log(`lexer.topState()`, lexer.topState());
  console.log(`lexer.states `, lexer.states);
  //console.log(`tokens`, tokens.map((tok, i) => [i, tok]));
  const tokindex = 1;
  console.log(`tokens[tokindex]`, tokens[tokindex]);
  console.log(`lexer.back(tokens[tokindex])`, lexer.back(tokens[tokindex]));
  console.log(`lexer.lex() `, lexer.lex());
  console.log(`lexer.loc`, lexer.loc + '');
  const lexeme = tokens[tokindex - 1].lexeme;
  console.log(`lexeme`, lexeme);
  console.log(`lexer.back(lexeme)`, lexer.back(lexeme));
  console.log(`lexer.next() `, lexer.next());
  console.log(`lexer.next() `, lexer.next());
  console.log(`lexer.next() `, lexer.next());
  console.log(`Location.count('blah\\nblah\\nblah\\nblah')`, Location.count('blah\nblah\nblah\nblah'));*/

  /*for(let j = 0; j < lexer.ruleNames.length; j++) {
    console.log(`lexer.rule[${j}]`, lexer.getRule(j));
  }*/
  return;

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
