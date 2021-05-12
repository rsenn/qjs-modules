import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import * as path from 'path';
import { Predicate } from 'predicate';
import { Location, Lexer, Token, SyntaxError } from 'lexer';
import Console from '../lib/console.js';
import JSLexer from '../lib/jslexer.js';
import CLexer from '../lib/clexer.js';
import BNFLexer from '../lib/bnflexer.js';
import extendArray from '../lib/extendArray.js';

('use strict');
('use math');

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

async function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      maxArrayLength: 100,
      maxStringLength: Infinity,
      compact: 1,
      showHidden: false
    }
  });

  let optind = 0;
  let code = 'c';

  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) {
      code = args[++optind];
    }

    optind++;
  }

  let file = args[optind] ?? 'tests/Shell-Grammar.l';
  console.log('file:', file);
  let str = file ? std.loadFile(file, 'utf-8') : code[1];
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

  lexer.handler = lex => {
    const { loc, mode, pos, start, byteLength, state } = lex;
    //console.log(`${this.currentLine()}`);
    console.log(`handler loc=${loc} mode=${IntToBinary(mode)} state=${lex.topState()}`, { pos, start, byteLength },
      `\n${lex.currentLine()}`
    );
    console.log(' '.repeat(loc.column - 1) + '^');
  };

  let tokenList = [];
  let declarations = [];
  const colSizes = [12, 8, 4, 16, 32, 10, 0];

  function printTok(tok, prefix) {
    const range = tok.charRange;
    const cols = [
      prefix,
      `tok[${tok.byteLength}]`,
      tok.id,
      tok.type,
      tok.lexeme,
      tok.lexeme.length,
      //range,
      tok.loc
    ];
    console.log(...cols.map((col, i) => (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i])));
    //console.log((tok.loc + '').padEnd(16), tok.type.padEnd(20), tok.toString());
  }

  let tok,
    i = 0;

  console.log('now', Date.now());

  for(let j = 0; j < lexer.ruleNames.length; j++) {
    console.log(`lexer.rule[${j}]`, lexer.getRule(j));
  }

  console.log(lexer.ruleNames.length, 'rules', lexer.ruleNames.unique().length, 'unique rules');

  console.log('lexer.mask', IntToBinary(lexer.mask));
  console.log('lexer.skip', lexer.skip);
  console.log('lexer.skip', IntToBinary(lexer.skip));
  console.log('lexer.states', lexer.states);
  console.log('lexer.tokens', lexer.tokens);
  console.log('lexer.pushState("JS")', lexer.pushState('JS'));
  console.log('lexer.stateStack', lexer.stateStack);
  console.log('lexer.topState()', lexer.topState());
  let mask = IntToBinary(lexer.mask);
  let state = lexer.topState();
  lexer.beginCode = () => (code == 'js' ? 0b1000 : 0b0100);
  let stack = [];

  let start = Date.now();
  const balancer = (() => {
    const table = { '}': '{', ']': '[', ')': '(' };
    return function ParentheseBalancer(tok) {
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
            throw new Error(`top '${stack.last}' != '${tok.lexeme}' [ ${stack.map(s => `'${s}'`).join(', ')} ]`
            );

          stack.pop();
          break;
        }
      }
    };
  })();

  for(let tok of lexer()) {
    // console.log(lexer.topState(), 'tok', tok);
    //console.log(`[${lexer.stateStack.length}]` + lexer.topState(1));
    balancer(tok);

    if(lexer.topState(1) == 'TEMPLATE' && tok.lexeme == '}') {
      lexer.popState();
      continue;
    }

    if(tok.rule[0] == 'whitespace') continue;

    if(tok.type == 'cstart') {
      lexer.mode = code == 'js' ? Lexer.LAST : Lexer.LONGEST;
      lexer.mask = code == 'js' ? 0b1000 : 0b0100;
    }
    if(tok.type == 'lbrace' || tok.type == 'inline') {
      lexer.mode = code == 'js' ? Lexer.LAST : Lexer.LONGEST;
      lexer.mask = code == 'js' ? 0b1000 : 0b0100;
    }

    printTok(tok, lexer.topState());

    mask = IntToBinary(lexer.mask);
    state = lexer.topState();
  }

  let end = Date.now();

  console.log(`took ${end - start}ms`);

  return;

  std.gc();
}

main(...scriptArgs.slice(1))
  .then(() => console.log('SUCCESS'))
  .catch(error => {
    console.log(`FAIL: ${error.message}\n${error.stack}`);
    std.exit(1);
  });
