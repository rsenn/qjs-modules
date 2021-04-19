import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as path from 'path.so';
import { Predicate } from 'predicate.so';
import { Location, Lexer, Token, SyntaxError } from 'lexer.so';
import Console from '../lib/console.js';
import JSLexer from '../lib/jslexer.js';
import CLexer from '../lib/clexer.js';
import BNFLexer from '../lib/bnflexer.js';

('use strict');
('use math');

const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 ? i : '0b' + IntToDWord(i).toString(2));

//const code = [`const str = stack.toString().replace(/\\n\\s*at /g, '\\n');`, `/^(.*)\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);` ];
const code = [
  `const str = stack.toString().replace(/\\n\\s*at /g, '\\n');`,
  `/Reg.*Ex/i.test(n)`,
  `/\\n/g`,
  `const [match, pattern, flags] = /^\\/(.*)\\/([a-z]*)$/.exec(token.value);`,
  `/^\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);`
];

let gettime;

const CLOCK_REALTIME = 0;
const CLOCK_MONOTONIC = 1;
const CLOCK_MONOTONIC_RAW = 4;
const CLOCK_BOOTTIME = 7;

globalThis.inspect = inspect;

Object.defineProperties(Array.prototype, {
  last: {
    get() {
      return this[this.length - 1];
    }
  },
  at: {
    value(index) {
      const { length } = this;
      return this[((index % length) + length) % length];
    }
  },
  clear: {
    value() {
      this.splice(0, this.length);
    }
  },
  findLastIndex: {
    value(predicate) {
      for(let i = this.length - 1; i >= 0; --i) {
        const x = this[i];
        if(predicate(x, i, this)) return i;
      }
      return -1;
    }
  },
  findLast: {
    value(predicate) {
      let i;
      if((i = this.findLastIndex(predicate)) == -1) return null;
      return this[i];
    }
  },
  unique: {
    value() {
      return [...new Set(this)];
    }
  }
});

function WriteFile(file, tok) {
  let f = std.open(file, 'w+');
  f.puts(tok);
  console.log('Wrote "' + file + '": ' + tok.length + ' bytes');
}

function DumpLexer(lex) {
  const { size, pos, start, line, column, lineStart, lineEnd, columnIndex } = lex;

  return `Lexer ${inspect({
    start,
    pos,
    size /*, line, column, lineStart, lineEnd, columnIndex*/
  })}`;
}
function DumpToken(tok) {
  const { length, offset, chars, loc } = tok;

  return `★ Token ${inspect({ chars, offset, length, loc }, { depth: Infinity })}`;
}
function hrtime(clock = CLOCK_MONOTONIC_RAW) {
  let data = new ArrayBuffer(16);

  gettime(clock, data);
  return new BigUint64Array(data, 0, 2);
}
function now(clock = CLOCK_MONOTONIC_RAW) {
  let data = new ArrayBuffer(16);

  gettime(clock, data);
  let [secs, nsecs] = new BigUint64Array(data, 0, 2);

  return Number(secs) * 10e3 + Number(nsecs) * 10e-6;
}

async function main(...args) {
  globalThis.console = new Console({
    colors: true,
    depth: 8,
    maxArrayLength: 100,
    maxStringLength: Infinity,
    compact: 1,
    showHidden: false
  });
  if(!gettime) {
    const { dlsym, RTLD_DEFAULT, define, call } = await import('ffi.so');
    const clock_gettime = dlsym(RTLD_DEFAULT, 'clock_gettime');
    define('clock_gettime', clock_gettime, null, 'int', 'int', 'void *');
    gettime = (clk_id, tp) => call('clock_gettime', clk_id, tp);
  }
  let optind = 0;
  let code = 'c';

  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) {
      code = args[++optind];
    }

    optind++;
  }

  let file = args[optind] ?? scriptArgs[optind];
  let str = args[optind] ? std.loadFile(file, 'utf-8') : code[1];
  let len = str.length;
  let type = path.extname(file).substring(1);

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

  console.log('lexer:', lexer[Symbol.toStringTag]);
  console.log('code:', code);
  // console.log('lexer.tokens:', lexer.tokens);N
  let e = new SyntaxError();
  console.log('new SyntaxError()', e);

  lexer.handler = function(state, skip) {
    const { mode, pos, start, byteLength } = this;
    console.log(`handler mode=${IntToBinary(mode)} state=${
        this.state || IntToBinary(state)
      } skip=${IntToBinary(skip)}`,
      { pos, start, byteLength }
    );
  };

  let tokenList = [];
  let declarations = [];
  const colSizes = [12, 8, 4, 16, 32, 10, 0];

  function printTok(tok, prefix) {
    const range = tok.charRange;
    const cols = [
      prefix,
      `tok[${tok.byteLength}]`,
      +tok,
      tok.type,
      tok.lexeme,
      tok.lexeme.length,
      range,
      tok.loc
    ];
    console.log(...cols.map((col, i) => (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i])));
    //console.log((tok.loc + '').padEnd(16), tok.type.padEnd(20), tok.toString());
  }

  let tok,
    i = 0;

  console.log('now', now());

  /* for(let j = 0; j < lexer.ruleNames.length; j++) {
    console.log(`lexer.rule[${j}]`, lexer.getRule(j));
  }*/

  console.log(lexer.ruleNames.length, 'rules', lexer.ruleNames.unique().length, 'unique rules');

  console.log('lexer.mask', IntToBinary(lexer.mask));
  console.log('lexer.skip', lexer.skip);
  console.log('lexer.skip', IntToBinary(lexer.skip));
  let mask = IntToBinary(lexer.mask);
  let state = lexer.state;
  lexer.beginCode = () => (code == 'js' ? 0b1000 : 0b0100);

  for(let tok of lexer()) {
    if(tok.rule[0] == 'whitespace') continue;

    /*console.log(tok.loc[Symbol.toStringTag]);
console.log(tok.loc.toString());*/

    if(tok.type == 'cstart') {
      lexer.mode = code == 'js' ? Lexer.LAST : Lexer.LONGEST;
      lexer.mask = code == 'js' ? 0b1000 : 0b0100;
    }
    if(tok.type == 'lbrace' || tok.type == 'inline') {
      lexer.mode = code == 'js' ? Lexer.LAST : Lexer.LONGEST;
      lexer.mask = code == 'js' ? 0b1000 : 0b0100;
    }

   /* if(lexer.mask > 0b1 && tok.type == '}') {
      lexer.mode = Lexer.FIRST;
      lexer.mask = 0b001;
    }*/

    tokenList.push(tok);
    printTok(tok, `${state /*mask*/}`);
    mask = IntToBinary(lexer.mask);
    state = lexer.state;
    if((tokenList.at(-1).lexeme == ';' && tokenList.at(-2).lexeme == ')') ||
      (tokenList.last.lexeme == '}' && tokenList.last.loc.column == 1) ||
      lex.js.tokenClass(tok) == 'preprocessor'
    ) {
      declarations.push(tokenList);
      tokenList = [];
    }
    i++;
  }

  /*console.log(lex.js.currentLine());
    console.log('^'.padStart(lex.js.loc.column));*/

  /*  for(let decl of declarations) {
    console.log('\n' + decl[0].loc);
    console.log('declaration', decl.join('').trim());
  }*/
  return;

  std.gc();
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
