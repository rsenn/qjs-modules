import * as path from 'path';
import * as xml from 'xml';
import * as fs from 'fs';
import * as pointer from 'pointer';
import * as location from 'location';
import Console from 'console';
import { nodeTypes, Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory } from './lib/dom.js';
import { define, getOpt } from './lib/util.js';
import * as util from './lib/util.js';
import * as dom from './lib/dom.js';
import REPL from './lib/repl.js';

let repl;

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
      interactive: [true, null, 'i'],
      '@': 'xml'
    },
    args
  );

  Object.assign(globalThis, {
    xml: {
      load,
      save,
      read: xml.read,
      write: xml.write
    },
    json: {
      read(...args) {
        return JSON.parse(...args);
      },
      write(...args) {
        return JSON.stringify(...args);
      },
      load,
      save
    },
    dom,
    util,
    fs,
    path,
    pointer,
    location
  });
  Object.assign(globalThis, { ...globalThis.xml, ...dom, ...util, ...pointer, ...location });

  console.log('params', params);

  repl = new REPL(
    '\x1b[38;2;80;200;255m' + path.basename(process.argv[1], '.js').replace(/test_/, '') + ' \x1b[0m',
    false
  );
  repl.show = repl.printFunction((...args) => console.log(...args));
  repl.historyLoad();
  repl.runSync();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}

function load(filename) {
  let data;

  try {
    data = fs.readFileSync(filename, 'utf-8');
  } catch(e) {}

  if(data) return this.read(data);
}
function save(filename, obj) {
  let data;

  try {
    data = this.write(obj);
  } catch(e) {}
  if(data) fs.writeFileSync(filename, data);
}
