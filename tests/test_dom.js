import * as os from 'os';
import * as std from 'std';
import {
  escape,
  quote,
  isObject,
  define,
  getClassName,
  mapObject,
  getset,
  gettersetter,
  memoize
} from '../lib/util.js';
import inspect from 'inspect';
import * as xml from 'xml';
import * as fs from 'fs';
import * as path from 'path';
import { Pointer } from 'pointer';
import * as deep from 'deep';
import Console from '../lib/console.js';
import { Document, Element, Node, Attr, Factory, NamedNodeMap } from '../lib/dom.js';
import { ImmutableXPath, MutableXPath, buildXPath, parseXPath, XPath } from '../lib/xpath.js';
import REPL from '../lib/repl.js';

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: 0,
      //stringBreakNewline: false,
      maxArrayLength: 10000,
      compact: 2,
      maxStringLength: 60,
      customInspect: true /*,
      hideKeys: [Symbol.iterator, Symbol.for('quickjs.inspect.custom'), Symbol.inspect]*/
    }
  });
  Object.assign(globalThis, {
    os,
    std,
    ...{ escape, quote, isObject, define, getClassName, mapObject, getset, gettersetter, memoize },
    xml,
    path,
    Pointer,
    deep,
    ...{ Document, Element, Node, Attr, Factory, NamedNodeMap },
    ...{ ImmutableXPath, MutableXPath, buildXPath, parseXPath, XPath }
  });

  let file = args[0] ?? '../../../an-tronics/eagle/555-Oscillator.sch';

  let base = path.basename(file, path.extname(file));

  let data = std.loadFile(file, 'utf-8');
  let start = Date.now();
  let result = xml.read(data, file, false);
  let end = Date.now();
  console.log(`parsing took ${end - start}ms`);

  start = Date.now();
  let doc = new Document(result[0]);

  let rawDoc = Node.raw(doc);
  Object.assign(globalThis, { rawDoc, doc });

  console.log('doc', inspect(doc, { depth: Infinity, compact: false }));

  /*  for(let value of deep.iterate(doc, deep.RETURN_VALUE)) {
    console.log('value', value);
  }
*/

  /*  let [libraries, lp] = deep.find(rawDoc, e => e.tagName == 'libraries', deep.RETURN_VALUE_PATH);
    console.log('lp', lp);
    let libs = deep.get(rawDoc, lp);
    let results = deep.select(rawDoc, e => e.tagName == 'library', deep.RETURN_VALUE);
    console.log('results', results);
    console.log('libs', libs);

    let flattened = deep.flatten(libraries, []).filter(([p, n]) => isObject(n) && 'tagName' in n);
    flattened = new Map(flattened);
    Object.assign(globalThis, { flattened, libraries, lp, results });*/

  /*  for(let [p, n] of flattened) {
      let ptr = new Pointer(p);
      console.log('ptr/path', { ptr, path: [...ptr] });
      let node = [...ptr].reduce((o, p) => o[p], libs);

      console.log('p/n', { p, n });
      console.log('p/n', { node });
      console.log('ptr.hier', ptr.hier());
    }
*/
  let hist;
  globalThis.fs = fs;
  let repl = new REPL(null);
  repl.show = repl.printFunction((...args) => console.log(...args));
  repl.historyLoad(hist);
  repl.addCleanupHandler(() => repl.historySave(hist));
  repl.run();
  repl.historySave(hist);

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

  console.log(`walking took ${end - start}ms (${count} nodes)`);
  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
