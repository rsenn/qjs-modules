#!/usr/bin/env qjsm
import * as os from 'os';
import * as std from 'std';
import * as fs from 'fs';
import * as path from 'path';
import { Console } from 'console';
import CLexer from 'lib/clexer.js';
import { getOpt, escape, extendArray } from 'util';

extendArray();

const NonWS = t => t.type != 'whitespace';
const WS = t => t.type == 'whitespace';

function parse(lexer, fn = (tok, arr) => {}, ...args) {
  let i = 0,
    tok,
    arr = [...args];

  while((tok = lexer.nextToken())) {
    //console.log('parse', { i: i++, tok, fn: fn + '' });
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

  let outputName,
    output = std.out;
  let params = getOpt(
    {
      output: [true, file => (output = std.open((outputName = file), 'w+')), 'o'],
      '@': 'files'
    },
    args
  );
  let files = params['@'];

  for(let file of files) {
    console.log('file', file);

    let str = std.loadFile(file);
    console.log('str', str.split('\n')[0]);

    let lexer = new CLexer(str, file);
    /*  console.log('lexer', lexer);
    console.log(`lexer.tokens`, lexer.tokens);
    console.log(`lexer.rules['struct']`, lexer.rules['struct']);
    console.log(`lexer.rules['typedef']`, lexer.rules['typedef']);
    console.log(`lexer.getRule('struct')`, lexer.getRule('struct'));*/
    const { rules, tokens } = lexer;

    //let id; while((id = lexer.next()))
    for(let id of lexer) {
      if(id == rules['struct'] || id == rules['typedef']) {
        const { loc, token: tok } = lexer;

        if(loc.column == 1) {
          let seq,
            line = loc.line,
            text = '';
          seq = parse(lexer, (tok, arr) => tok.loc.line != line && -1, tok);
          const last = seq.filter(NonWS).at(-1);
          if(last.type == 'lbrace') {
            seq = parse(lexer, (tok, arr) => tok.loc.column == 1 && tok.type == 'rbrace', ...seq);
            seq = parse(lexer, (tok, arr) => tok.type == 'semi', ...seq);
          }
          output.puts(seq.reduce((s, t) => s + t.lexeme, '').trim() + '\n\n');
        }
      }

      output.flush();
    }
  }

  if(outputName) console.log(output.tell() + ` bytes written to '${outputName}'`);
  output.close();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
