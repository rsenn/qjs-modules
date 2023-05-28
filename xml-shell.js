#!/usr/bin/env qjsm
import * as path from 'path';
import * as xml from 'xml';
import * as fs from 'fs';
import * as pointer from 'pointer';
import * as location from 'location';
import Console from 'console';
import { Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory } from 'dom';
import { define, getOpt, weakDefine } from 'util';
import * as util from 'util';
import * as dom from 'dom';
import { REPL } from 'repl';

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
      serialize,
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
    ...{ Parser, Node, NodeList, NamedNodeMap, Element, Document, Attr, Text, TokenList, Factory },
    ...util,
    ...pointer,
    ...location
  });

  globalThis.parser ??= new Parser();
  globalThis.documents ??= [];

  for(let arg of params['@']) {
    globalThis.document = parser.parseFromFile(arg);
    documents.push(document);
  }

  repl = globalThis.repl = new REPL(
    '\x1b[38;2;80;200;255m' +
      path.basename(process.argv[1], '.js').replace(/test_/, '') +
      ' \x1b[0m',
    false
  );
  repl.historyLoad(null);
  repl.loadSaveOptions();
  /*repl.show = repl.printFunction((...args) => console.log(...args));
  repl.directives = {
    i: [
      name => {
        const all = name[0] == '*';
        if(name[0] == '*') name = name.slice(1);

        import(name)
          .then(m => {
            //repl.printStatus(`Loaded '${name}'.`);
            const sym = name.replace(/.*\//g, '').replace(/\.[^.]+$/gi, '');
            let err = false;
            if(all) {
              try {
                weakDefine(globalThis, m);
              } catch(e) {
                err = e;
              }
              repl.printStatus(
                err
                  ? `Error importing '${name}': ${err.message}`
                  : `Imported from '${sym}': ${Object.getOwnPropertyNames(m).join(' ')}`
              );
            } else {
              globalThis[sym] = m;
              repl.printStatus(`Imported '${sym}' as '${sym}'`);
            }
          })
          .catch(err => {
            repl.printStatus(`ERROR: ${err.message}`);
          });
      },
      'import a module'
    ]
  };*/

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

  if(data) return xml.read(data, filename, ...args);
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
  let [filename, doc, wfn = (filename, data) => fs.writeFileSync(filename, data)] =
    args.length == 1 ? [null, ...args] : args;
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
    data = xml.write(Node.raw(obj) ?? obj);
  } catch(e) {
    err = e;
  }
  if(data && !err) return wfn(filename, data.trimEnd() + '\n');
  if(err) throw err;
}
