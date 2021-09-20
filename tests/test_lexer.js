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

('use strict');
('use math');

extendArray(Array.prototype);

const IntToDWord = ival => (isNaN(ival) === false && ival < 0 ? ival + 4294967296 : ival);
const IntToBinary = i => (i == -1 || typeof i != 'number' ? i : '0b' + IntToDWord(i).toString(2));

//const code = ["const str = stack.toString().replace(/\\n\\s*at /g, '\\n');", "/^(.*)\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);" ];
const code = ["const str = stack.toString().replace(/\\n\\s*at /g, '\\n');", '/Reg.*Ex/i.test(n)', '/\\n/g', 'const [match, pattern, flags] = /^\\/(.*)\\/([a-z]*)$/.exec(token.value);', '/^\\s\\((.*):([0-9]*):([0-9]*)\\)$/.exec(line);'];

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

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      colors: true,
      depth: 8,
      breakLength: 100,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: 0,
      showHidden: false
    }
  });
  console.log('console.options', console.options);

  let optind = 0;
  let code = 'c';

  while(args[optind] && args[optind].startsWith('-')) {
    if(/code/.test(args[optind])) {
      code = args[++optind];
    }

    optind++;
  }

  let file = args[optind] ?? 'tests/test_lexer.js';
  console.log(`Loading '${file}'...`);

  let str = file ? std.loadFile(file, 'utf-8') : code[1];
  //str = '  return new Map(ret.map(([name, description]) => [name, { url: `https://github.com/${user}/${name}`, description }]));';
  let len = str.length;
  let type = path.extname(file).substring(1);
  console.log('file:', file, str.slice(0,100));

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
    //console.log(`handler loc=${loc} mode=${IntToBinary(mode)} state=${lex.topState()}`, { pos, start, byteLength }, `\n${lex.currentLine()}` );
    console.log(' '.repeat(loc.column - 1) + '^');
  };

  let tokenList = [],
    declarations = [];
  const colSizes = [12, 8, 4, 16, 32, 10, 0];

  function printTok(tok, prefix) {
    const range = tok.charRange;
    const cols = [prefix, `tok[${tok.byteLength}]`, tok.id, tok.type, tok.lexeme, tok.lexeme.length, tok.loc];
    console.log(...cols.map((col, i) => (col + '').replaceAll('\n', '\\n').padEnd(colSizes[i])));
  }

  let tok,
    i = 0;

  console.log('now', Date.now());

  console.log(lexer.ruleNames.length, 'rules', lexer.ruleNames.unique().length, 'unique rules');

  console.log('lexer.mask', IntToBinary(lexer.mask));
  console.log('lexer.skip', lexer.skip);
  console.log('lexer.skip', IntToBinary(lexer.skip));
  console.log('lexer.states', lexer.states);

  console.log('new SyntaxError("test")', new SyntaxError('test', new Location(10, 3, 28, 'file.txt')));
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
  let balancers = [balancer()];

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
    //console.log('tok', tok);

    printTok(tok, lexer.topState());
    tokens.push(tok);
  }

  let end = Date.now();

  console.log(`took ${end - start}ms`);
  console.log('lexer',lexer);
  /*console.log('lexer.tokens', lexer.tokens);
  // console.log('lexer.rules', new Map(lexer.ruleNames.map(name => lexer.getRule(name)).map(([name, expr, states]) => [name, new RegExp(expr, 'gmy'), states.join(',')])));
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
