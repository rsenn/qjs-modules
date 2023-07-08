import * as os from 'os';
import { define } from 'util';
import { extendArray } from 'util';
import { getOpt } from 'util';
import { Console } from 'console';
import CLexer from 'lexer/c.js';
import * as std from 'std';
#!/usr/bin/env qjsm
extendArray();

let jsCFuncs, noStructs;

const MaybeNumber = t => (Array.isArray(t) ? t.map(MaybeNumber) : isNaN(+t) ? t : +t);

const NonWS = t => (Array.isArray(t) ? t.filter(NonWS) : t.type != 'whitespace');
const TrimWS = t =>
  t.reduce((acc, item) => {
    if(acc.length == 0 && item.type == 'whitespace') return acc;
    acc.push(item);
    return acc;
  }, []);

const Lexeme = t => (Array.isArray(t) ? t.map(Lexeme).join('') : t.lexeme);

const WS = t => t.type == 'whitespace';

const Paren = (toklist, t = tok => tok.lexeme) => {
  let start = toklist.findIndex(tok => t(tok) == '(');
  let end = toklist.findLastIndex(tok => t(tok) == ')');

  if(start == -1 || end == -1) return [];
  if(end == -1) end = toklist.length;

  return toklist.slice(start + 1, end);
};

const CommaList = (toklist, t = tok => tok.lexeme) => {
  let list = [];
  let s = '';
  for(let tok of NonWS(toklist)) {
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
const CommaJoin = list => list.reduce((acc, item) => acc.concat(acc.length ? [',', item] : [item]), []);

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
    /* stdout: std.out,
    stderr: std.err,
   */ inspectOptions: {
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

  if(/bindings/.test(scriptArgs[0])) noStructs = jsCFuncs = true;

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
    let isCFuncList = false,
      isCFuncCall = false,
      cFuncListNames = [],
      cFuncListObjects = {};

    //console.log('rules',  rules);

    for(let id of lexer) {
      //line.push(lexer.token);
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
          while(block.length > 0 && ['whitespace', 'rbrace', 'singleLineComment', 'multiLineComment'].indexOf(block[0].type) != -1) block.shift();
          //console.log('block.slice(0,10)', block.slice(0,10));
          let firstLine = block[0].loc.line;
          let rows = NonWS(block)
            .reduce((acc, token) => {
              if(['preprocessor', 'singleLineComment', 'multiLineComment'].indexOf(token.type) == -1) (acc[token.loc.line - firstLine] ??= []).push(token);
              return acc;
            }, [])
            .filter(l => l.length);
          let cfuncList = rows.slice(0, -1).map((row, i) => (i > 0 ? [row[0].lexeme].concat(CommaList(Paren(row, t => t.lexeme))) : row.filter(tok => tok.type == 'identifier').last?.lexeme));
          let header = [cfuncList.shift(), block[0].loc + ''];
          cfuncList = [header, cfuncList];

          cFuncListNames.push(header[0]);

          console.log('cfuncList', console.config({ compact: 1 }), cfuncList);
          cFuncLists.push(cfuncList);

          block.splice(0, block.length);
        }
      }
      if(id == rules['whitespace']) {
        const { pos, seq, lexeme, value, charLength } = lexer;

        /* if(lexeme == '\n') {
          line.splice(0, line.length);
        }*/
      } else if(id == rules['semi']) {
        block = TrimWS(block);

        if(block.length >= 3 && NonWS(block)[1]?.type == 'equal' && block[0]?.type == 'identifier' && /_(proto|ctor)$/.test(block[0].lexeme)) {
          //console.log('ASSIGN', Lexeme(block));

          isCFuncCall = true;
        }

        if(block.some(tok => ['JS_SetClassProto', 'JS_NewClass'].indexOf(tok.lexeme) != -1)) {
          isCFuncCall = true;
          while(['JS_SetClassProto', 'JS_NewClass'].indexOf(block[0].lexeme) == -1) block.shift();
          //console.log('block', block);
        }

        if(isCFuncCall) {
          let fnIndex = block.findIndex(tok => tok.lexeme == '(') - 1;
          let fnName = block[fnIndex].lexeme;
          let fnArgs = CommaList(Paren(block));

          while(['ctx', 'm'].indexOf(fnArgs[0]) != -1) fnArgs.shift();

          //console.log('isCFuncCall', { fnIndex, fnName, fnArgs });
          let { loc } = block[0];

          if(fnName == 'JS_SetPropertyFunctionList') {
            let [objName, cFuncListName] = fnArgs;
            cFuncListObjects[cFuncListName] = define([objName], { loc });
          } else if(fnName == 'JS_NewClass' || fnName == 'JS_SetClassProto') {
            if(fnName == 'JS_NewClass') fnArgs.shift(), fnArgs.reverse();
            let [objName, cClassIdName] = fnArgs.slice(-2);
            cFuncListObjects[cClassIdName] = define([objName], { loc });
          } else if(fnIndex > 0) {
            let objName = Lexeme(block[0]);
            let [cCall, cName, ...rest] = fnArgs;

            //console.log('', { objName, cName });

            if(cName) cName = cName.replace(/^"|"$/g, '');

            let args = MaybeNumber(cName ? [cCall, cName, ...rest] : rest);
            cFuncListObjects[objName] = define(args, { loc });
          }

          //console.log('cFuncCall()', Lexeme(block));
        }
        isCFuncList = false;
        isCFuncCall = false;

        block.splice(0, block.length);
      } else if(lexer.loc.column == 1 && id != rules['whitespace'] && id != rules['preprocessor']) {
      }
      if(cFuncListNames.indexOf(lexer.lexeme) != -1) {
        const { lexeme, loc } = lexer;
        isCFuncCall = true;
      }

      output.flush();
    }
    console.log('cFuncListObjects', console.config({ depth: 10, compact: 1 }), cFuncListObjects);
  }

  output.puts(JSON.stringify(cFuncLists, null, 2) + '\n');

  if(outputName) console.log(output.tell() + ` bytes written to '${outputName}'`);
  output.close();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}