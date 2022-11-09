#!/usr/bin/env qjsm
import * as path from 'path';
import * as xml from 'xml';
import * as fs from 'fs';
import * as pointer from 'pointer';
import * as location from 'location';
import Console from 'console';
import { nodeTypes, Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory } from 'dom';
import { define, getOpt } from 'util';
import * as util from 'util';
import * as dom from 'dom';
import REPL from 'repl';

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
      parse,
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
  Object.assign(globalThis, {
    ...globalThis.xml,
    ...dom,
    ...util,
    ...pointer,
    ...location
  });

  globalThis.parser ??= new dom.Parser();
  globalThis.documents ??= [];

  for(let arg of params['@']) {
    globalThis.document = parser.parseFromFile(arg);
    documents.push(document);
  }

  repl = globalThis.repl = new REPL(
    '\x1b[38;2;80;200;255m' + path.basename(process.argv[1], '.js').replace(/test_/, '') + ' \x1b[0m',
    false
  );
  repl.show = repl.printFunction((...args) => console.log(...args));
  repl.historyLoad(null, fs);
  repl.directives = {
    i: [
      name => {
        import(name).then(m => {
          globalThis[name] = m;
          repl.printStatus(`Loaded '${name}'.`);
        });
      },
      'import a module'
    ]
  };

  for(let arg of params['@']) {
    parse(arg);
  }
  repl.runSync();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}

function load(filename, ...args) {
  let data;

  try {
    data = fs.readFileSync(filename, 'utf-8');
  } catch(e) {}

  if(data) return xml.read(data, filename, ...args);
}

function parse(filename, ...args) {
  let doc,
    parser = new Parser();

  try {
    doc = parser.parseFromFile(filename, 'utf-8');
  } catch(e) {}
  return (globalThis.document = doc);
}

function save(filename, obj) {
  let data;

  try {
    data = xml.write(obj);
  } catch(e) {}
  if(data) fs.writeFileSync(filename, data);
}
