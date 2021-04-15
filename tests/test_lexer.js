import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';
import * as xml from 'xml.so';
import { Predicate } from 'predicate.so';
import { Location, Lexer, Token, SyntaxError } from 'lexer.so';
import Console from './console.js';
import JSLexer from './jslexer.js';
import CLexer from './clexer.js';

('use strict');
('use math');

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
  console = new Console({
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

  let file = args[0] ?? scriptArgs[0];
  let str = args[0] ? std.loadFile(args[0], 'utf-8') : code[1];
  let len = str.length;

  let jslex = new JSLexer(str, file);
  let clex = new CLexer(str, file, CLexer.LONGEST);

  console.log('lexers:', { jslex, clex });
  console.log('jslex.tokens:', jslex.tokens);
  console.log('clex.tokens:', clex.tokens);
  let e = new SyntaxError();
  console.log('new SyntaxError()', e);

  jslex.handler = function(state, skip) {
    console.log(`handler state=${state} skip=${skip}`);
  };

  let tokenList = [];
  let declarations = [];
  const colSizes = [8, 4, 16, 32, 0];

  function printTok(tok) {
    const cols = [`tok[${tok.byteLength}]`, +tok, tok.type, tok.lexeme, tok.loc];
    console.log(...cols.map((col, i) => (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i])));
    //console.log((tok.loc + '').padEnd(16), tok.type.padEnd(20), tok.toString());
  }

for(let name of [...jslex.ruleNames, 'RegularExpressionNonTerminator', 'RegularExpressionBackslashSequence', 'RegularExpressionClassChar', 'RegularExpressionClass', 'RegularExpressionFlags', 'RegularExpressionFirstChar', 'RegularExpressionChar', 'RegularExpressionBody', 'RegularExpressionLiteral'
].filter(n => new RegExp('reg.*ex','i').test(n)))
  console.log(`RULE ${name}`, jslex.getRule(name)[1]);
   try {

    let tok,
      i = 0;

    console.log('now', now());
    console.log('jslex.ruleNames', jslex.ruleNames);

    for(let tok of jslex(-1, 0 /*1*/)) {
      if(tok.rule[0] == 'whitespace') continue;
      console.log(`token(${i}).value:`, tok.value);

      tokenList.push(tok);
      //      console.log(`token(${i}) ${tok.rule[0]}: '${Lexer.escape(tokenList.at(-1).lexeme)}'`);
      printTok(tok);

      if((tokenList.at(-1).lexeme == ';' && tokenList.at(-2).lexeme == ')') ||
        (tokenList.last.lexeme == '}' && tokenList.last.loc.column == 1) ||
        jslex.tokenClass(tok) == 'preprocessor'
      ) {
        declarations.push(tokenList);
        tokenList = [];
      }
      i++;
    }
  } catch(err) {
    console.log('err:', err.message);
    return;
    /*console.log(jslex.currentLine());
    console.log('^'.padStart(jslex.loc.column));*/
  }

  /*  for(let decl of declarations) {
    console.log('\n' + decl[0].loc);
    console.log('declaration', decl.join('').trim());
  }*/
  return;

  std.gc();
}

main(...scriptArgs.slice(1));
