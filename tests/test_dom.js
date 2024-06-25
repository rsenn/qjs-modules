import * as fs from 'fs';
import * as path from 'path';
import { getOpt, isObject } from 'util';
import extendArray from 'extendArray';
import { parseSelectors } from '../lib/css3-selectors.js';
import { Node, Parser } from '../lib/dom.js';
import Console from 'console';
import { REPL } from 'repl';
import { write as writeXML } from 'xml';

extendArray();

let repl = {
  printStatus(...args) {
    console.log(...args);
  }
};

function StartREPL() {
  //return import('repl').then(REPL => {
  repl = new REPL('\x1b[38;2;80;200;255m' + path.basename(process.argv[1], '.js').replace(/test_/, '') + ' \x1b[0m', false);
  repl.show = repl.printFunction((...args) => console.log(...args));
  repl.historyLoad();
  repl.loadSaveOptions();
  return repl.run();
  // });
}

function main(...args) {
  globalThis.console = new Console({
    stdout: process.stdout,
    stderr: process.stderr,
    inspectOptions: {
      depth: 10,
      maxArrayLength: 10000,
      compact: false,
      customInspect: true
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

  console.log('params', params);

  /*  const dom = {
    nodeTypes,
    Parser,
    Node,
    NodeList,
    NamedNodeMap,
    Element,
    Document,
    Attr,
    Text,
    TokenList,
    Factory
  };
  Object.assign(globalThis, {
    ...{ escape, quote, isObject, define, mapObject, getset, gettersetter, memoize },
    path,
    ...dom,
    dom,
    ...{ ImmutableXPath, MutableXPath, buildXPath, parseXPath, XPath }
  });
*/
  let files = params['@'].length ? params['@'] : [/*'tests/test1.xml', 'tests/test2.xml', */ 'test3.xml'];

  files.forEach(processFile);

  function processFile(file) {
    file = path.join(path.dirname(scriptArgs[0]), file);
    console.log('Processing file:', file);
    let base = path.basename(file, path.extname(file));

    let data = std.loadFile(file, 'utf-8');
    let start = Date.now();
    let end = Date.now();
    console.log(`parsing took ${end - start}ms`);

    start = Date.now();

    /*  let result = readXML(data, file, false);
  let doc=new Document(result[0]);*/

    let parser = new Parser();
    let doc = parser.parseFromString(data, file, { tolerant: true });

    /*  let walker = doc.createTreeWalker(doc.body);

  console.log('walker', walker);*/

    let rawDoc = Node.raw(doc);
    Object.assign(globalThis, { rawDoc, doc });

    console.log('writeXML', writeXML);

    fs.writeFileSync('output.xml', writeXML(rawDoc));

    console.log('doc', doc);

    let [sel] = [...parseSelectors('layer')];
    console.log('sel', sel);

    console.log('sel()', sel({ tagName: 'layer' }));

    let firstLayer = doc.querySelector('layer');
    console.log('firstLayer', firstLayer);
    let allLayers = doc.querySelectorAll('layer');
    console.log('allLayers', console.config({ compact: false, maxArrayLength: Infinity }), allLayers);

    let ll = allLayers.last;
    console.log('ll', ll);
    console.log('ll.path', ll?.path);

    let lt = doc.querySelector('layer[name=Top]');
    console.log('lt', lt);

    console.log('lt.path', lt.path);

    let l1 = doc.querySelector('layer:nth-child(2)');
    console.log('l1', l1);

    console.log('l1.path', l1.path);

    let count = 0;

    if(0)
      Recurse(doc, (node, stack) => {
        const raw = Node.raw(node);
        count++;
        if(node.nodeType != node.ELEMENT_NODE && node.nodeType != node.DOCUMENT_NODE) {
          return;
        }
        if(raw.children) {
          let cl = node.children;
          if(raw.children[0]) {
            let y = cl.path;
            let elm = cl[0];
            if(cl.length) {
              if(elm) {
                /*  if(isObject(elm) && 'tagName' in elm) console.log('elm', elm.tagName, elm.path); else */
                //console.log('elm', elm);
              }
            }
          }
        }
        if(raw.attributes) {
          let al = node.attributes;
          let z = al.path;
          let at = al[Object.keys(raw.attributes)[0]];
          if(at) {
            let x = at.path;
          }
        }
      });

    function Recurse(node, fn, stack = []) {
      if(isObject(node)) {
        if(isObject(node.children))
          for(let child of node.children) {
            Recurse(child, fn, [...stack, node]);
          }
        if(isObject(node.attributes)) {
          const attributes = /*Node.raw(node)?.attributes ??*/ node.attributes;
          for(let attr of /*Node.raw(node).*/ attributes) {
            Recurse(attr, fn, [...stack, node]);
            //console.log('Attr', attr, Node.path(attr));
          } //Recurse({ name: attr, value:attributes[attr],[Symbol.toStringTag]: 'Attr', __proto__: Attr.prototype }, fn, [...stack, node]);
        }
      }
      fn(node, stack);
    }
    end = Date.now();

    repl.printStatus(`walking took ${end - start}ms (${count} nodes)`);
  }

  if(params.interactive) StartREPL();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
}
