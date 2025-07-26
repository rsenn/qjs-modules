#!/usr/bin/env qjsm
import { readFileSync, writeFileSync } from 'fs';
import { basename } from 'path';
import { getOpt } from 'util';
import Console from 'console';
import { REPL } from 'repl';
import { read, write } from 'xml';
import { Attr, CSSStyleDeclaration, Comment, Document, Element, Entities, Factory, GetType, Interface, NamedNodeMap, Node, NodeList, Parser, Prototypes, Serializer, Text, TokenList, nodeTypes, } from 'dom';

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
      hideKeys: [Symbol.iterator, Symbol.for('quickjs.inspect.custom'), Symbol.inspect]*/,
    },
  });

  let params = getOpt(
    {
      output: [true, null, 'o'],
      interactive: [true, null, 'i'],
      '@': 'xml',
    },
    args,
  );

  Object.assign(globalThis, {
    xml: {
      parse,
      load,
      save,
      serialize,
      read: read,
      write: write,
    },
    json: {
      read(...args) {
        return JSON.parse(...args);
      },
      write(...args) {
        return JSON.stringify(...args);
      },
      load,
      save,
    },
    ['@']: params['@'],
  });

  const dom = (globalThis.dom = {
    Attr,
    CSSStyleDeclaration,
    Comment,
    Document,
    Element,
    Entities,
    Factory,
    GetType,
    Interface,
    NamedNodeMap,
    Node,
    NodeList,
    Parser,
    Prototypes,
    Serializer,
    Text,
    TokenList,
    nodeTypes,
  });

  Object.assign(globalThis, {
    ...globalThis.xml,
    ...globalThis.dom,
    dom,
  });

  globalThis.parser ??= new Parser();
  globalThis.documents ??= [];

  for(let arg of params['@']) {
    globalThis.document = parser.parseFromFile(arg);
    documents.push(document);
  }

  repl = globalThis.repl = new REPL('\x1b[38;2;80;200;255m' + basename(process.argv[1], '.js').replace(/test_/, '') + ' \x1b[0m', false);
  repl.historyLoad(null);
  repl.loadSaveOptions();

  for(let arg of params['@']) {
    parse(arg);
  }
  repl.run();
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

  if(data) return read(data, filename, ...args);
}

function parse(filename, ...args) {
  let doc,
    parser = (globalThis.parser ??= new Parser());

  try {
    doc = parser.parseFromFile(filename, 'utf-8');
  } catch(e) {}
  return (globalThis.document = doc);
}

function serialize(...args) {
  let [filename, doc, wfn = (filename, data) => fs.writeFileSync(filename, data)] = args.length == 1 ? [null, ...args] : args;
  let data,
    s = (globalThis.serializer ??= new Serializer());

  try {
    data = s.serializeToString(doc);
  } catch(e) {}
  if(data && filename) return wfn(filename, data.trimEnd() + '\n');
  return data;
}

function save(filename, obj, wfn = (filename, data) => fs.writeFileSync(filename, data)) {
  let data, err;

  try {
    data = write(Node.raw(obj) ?? obj);
  } catch(e) {
    err = e;
  }
  if(data && !err) return wfn(filename, data.trimEnd() + '\n');
  if(err) throw err;
}
