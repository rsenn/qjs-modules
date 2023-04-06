import * as os from 'os';
import * as std from 'std';
import { escape, quote, isObject, define, getClassName, mapObject, getset, gettersetter, once, memoize, getOpt, glob } from '../lib/util.js';
import inspect from 'inspect';
import * as xml from 'xml';
import * as fs from 'fs';
import * as path from 'path';
import * as deep from 'deep';
import Console from '../lib/console.js';
import { nodeTypes, Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory } from '../lib/dom.js';
import REPL from '../lib/repl.js';

let repl = {
  printStatus(...args) {
    console.log(...args);
  }
};

function StartREPL() {
  repl = new REPL(
    '\x1b[38;2;80;200;255m' + path.basename(process.argv[1], '.js').replace(/test_/, '') + ' \x1b[0m',
    false
  );
  repl.show = repl.printFunction((...args) => console.log(...args));
  repl.historyLoad();
  repl.loadSaveOptions();
  return repl.run();
}

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: 10,
      stringBreakNewline: false,
      maxArrayLength: 10000,
      compact: false,
      maxStringLength: Infinity,
      customInspect: true /*,
      hideKeys: [Symbol.iterator, Symbol.for('quickjs.inspect.custom'), Symbol.inspect]*/
    }
  });

  let params = getOpt(
    {
      output: [true, null, 'o'],
      interactive: [false, null, 'i'],
      '@': 'xml'
    },
    args
  );

  console.log('params', params);

  if(params.interactive) StartREPL();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
