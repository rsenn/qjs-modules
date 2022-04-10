#!/usr/bin/env qjsm
import * as os from 'os';
import * as std from 'std';
import * as fs from 'fs';
import * as path from 'path';
import { Console } from 'console';
import CLexer from 'lib/lexer/c.js';
import { getOpt, escape, extendArray } from 'util';

extendArray();

let jsCFuncs, noStructs;

const NonWS = t => (Array.isArray(t) ? t.filter(NonWS) : t.type != 'whitespace');
const Lexeme = t => (Array.isArray(t) ? t.map(Lexeme) : t.lexeme);

const WS = t => t.type == 'whitespace';

const Paren = (toklist, t = tok => tok) => {
  let start = toklist.findIndex(tok => t(tok) == '(');
  let end = toklist.findIndex(tok => t(tok) == ')');

  if(start == -1 || end == -1) return [];
  if(end == -1) end = toklist.length;

  return toklist.slice(start + 1, end);
};

const CommaList = (toklist, t = tok => tok.lexeme) => {
  let list = [];
  let s = '';
  for(let tok of toklist) {
    if(t(tok) == ',') {
      list.push(s);
      s = '';
      continue;
    }
    s += t(tok);
  }
  if(s != '') list.push(s);
  return list;
};

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
  globalThis.console = new Console({
    stdout: std.out,
    stderr: std.err,
    inspectOptions: {
      colors: true,
      maxStringLength: 100,
      maxArrayLength: Infinity,
      compact: false
    }
  });
  //console.log('args', scriptArgs);

  let outputName,
    debug,
    output = std.out;
  let params = getOpt(
    {
      output: [true, file => (output = std.open((outputName = file), 'w+')), 'o'],
      debug: [false, () => (debug = (debug | 0) + 1), 'x'],
      'no-structs': [false, () => (noStructs = true), 'S'],
      cfunc: [false, (jsCFuncs = true), 'c'],
      '@': 'files'
    },
    args
  );
  let cFuncLists = [];
  let files = params['@'];

  for(let file of files) {
    //console.log('file', file);

    let str = std.loadFile(file, 'utf-8');

    /*console.log('str', str);
    console.log('str', str.split('\n')[0]);*/

    let lexer = new CLexer(str, file);

    const { rules, tokens } = lexer;
    let line = [],
      block = [];
    let toklist = [];
    let isCFuncList = false;

    //console.log('rules',  rules);

    for(let id of lexer) {
      line.push(id);
      block.push(lexer.token);

      if(!noStructs)
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

      if(jsCFuncs) {
        if(id == rules['identifier']) {
          if(lexer.lexeme == 'JSCFunctionListEntry') isCFuncList = true;
          toklist[lexer.seq] = lexer.lexeme;
        }
      }

      if(isCFuncList) {
        if(id == rules['rbrace'] && lexer.loc.column == 1) {
          while(block.length > 0 && block[0].type == 'whitespace') block.shift();
          let firstLine = block[0].loc.line;
          let rows = NonWS(block)
            .reduce((acc, token) => {
              if(['preprocessor', 'singleLineComment', 'multiLineComment'].indexOf(token.type) == -1)
                (acc[token.loc.line - firstLine] ??= []).push(token);
              return acc;
            }, [])
            .filter(l => l.length);
          let cfuncList = rows
            .slice(0, -1)
            .map((row, i) =>
              i > 0
                ? [row[0].lexeme].concat(CommaList(Paren(row, t => t.lexeme)))
                : row.filter(tok => tok.type == 'identifier').last?.lexeme
            );
          console.log('cfuncList', console.config({ compact: 1 }), cfuncList);
          cFuncLists.push([cfuncList.shift(), cfuncList]);

          block.splice(0, block.length);
        }
      }
      if(id == rules['whitespace']) {
        const { pos, seq, lexeme, value, charLength } = lexer;

        if(lexeme == '\n') {
          line.splice(0, line.length);
        }
      } else if(id == rules['semi']) {
        isCFuncList = false;
        block.splice(0, block.length);
      } else if(lexer.loc.column == 1 && id != rules['whitespace'] && id != rules['preprocessor']) {
      }

      output.flush();
    }
    // console.log('toklist', console.config({ compact: false, depth: Infinity, breakLength: 80 }), toklist);
  }

  output.puts(JSON.stringify(cFuncLists, null, 2));

  if(outputName) console.log(output.tell() + ` bytes written to '${outputName}'`);
  output.close();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
