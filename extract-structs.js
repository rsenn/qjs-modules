import * as os from 'os';
import * as std from 'std';
import * as fs from 'fs';
import inspect from 'inspect';
import * as path from 'path';
import { Predicate } from 'predicate';
import { Location, Lexer, Token } from 'lexer';
import { Console } from 'console';
import JSLexer from './lib/jslexer.js';
import CLexer from './lib/clexer.js';
import BNFLexer from './lib/bnflexer.js';
import { define, curry, toString, escape, quote, unique, split, extendArray } from 'util';

extendArray();

const NonWS = t => t.type != 'whitespace';
const WS = t => t.type == 'whitespace';

function parse(lexer, fn = (tok, arr) => {}, ...args) {
  let tok,
    arr = [...args];

  while((tok = lexer.nextObj())) {
    let v = fn(tok, arr);
    if(v < 0) break;
    arr.push(tok);
    if(v) break;
  }
  return arr;
}

function main(...args) {
  globalThis.console = new Console(process.stderr, {
    inspectOptions: {
      colors: true,
      maxStringLength: 100,
      maxArrayLength: Infinity,
      compact: false
    }
  });

  for(let arg of args) {
    console.log('arg', arg);

    let str = std.loadFile(arg);
    console.log('str', escape(str).substring(0, 100));

    let lexer = new CLexer(str, arg);
    console.log('lexer', lexer);
    let tok;
    while((tok = lexer.nextObj())) {
      const { loc } = tok;
      if({ struct: 1, typedef: 1 }[tok.type]) {
        if(loc.column == 1) {
          let seq,
            line = loc.line,
            text = '';

          seq = parse(lexer, (tok, arr) => tok.loc.line != line && -1, tok);
          const last = seq.filter(NonWS).at(-1);

          //          console.log(`line ${loc} last:`, last);

          if(last.type == 'lbrace') {
            seq = parse(lexer, (tok, arr) => tok.loc.column == 1 && tok.type == 'rbrace', ...seq);
            seq = parse(lexer, (tok, arr) => tok.type == 'semi', ...seq);
          }

          std.puts(
            seq
              .filter(t => !t.type.endsWith('Comment'))
              .map(t => t.lexeme)
              .join('')
              .trim() + '\n\n'
          );

          /*
          do {
            console.log('tok', tok, tok.loc + '', { line });
            text += tok.lexeme;
            tok = lexer.nextObj();
          } while(tok.loc.line == line);
*/
        }
      }
    }
  }
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
