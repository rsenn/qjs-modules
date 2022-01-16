import * as os from 'os';
import * as std from 'std';
import { escape, quote, isObject, define, getClassName, mapObject, getset, gettersetter, memoize } from '../lib/util.js';
import inspect from 'inspect';
import * as xml from 'xml';
import * as path from 'path';
import * as deep from 'deep';
import Console from '../lib/console.js';
import { Document, Element, Node, Attr, Factory, NamedNodeMap } from '../lib/dom.js';
import { ImmutableXPath, MutableXPath, buildXPath, parseXPath, XPath } from '../lib/xpath.js';

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: 0,
      //stringBreakNewline: false,
      maxArrayLength: Infinity,
      compact: false,
      maxStringLength: 60,
      customInspect: true /*,
      hideKeys: [Symbol.iterator, Symbol.for('quickjs.inspect.custom'), Symbol.inspect]*/
    }
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
 
  console.log('doc', inspect(doc, { depth: Infinity, compact: false }));
 
  for(let value of deep.iterate(doc, deep.RETURN_VALUE)) {
    console.log('value', value);
  }
  let count = 0;
  Recurse(doc, (node, stack) => {
    const raw = Node.raw(node);

    count++;

    if(node.nodeType != node.ELEMENT_NODE && node.nodeType != node.DOCUMENT_NODE) {
      return;
    }

    //console.log('xpath:',buildXPath(Node.path(node), Node.document(node)));

    //    if(node[Symbol.inspect ?? Symbol.for('quickjs.inspect.custom')]) console.log('node', node[Symbol.inspect ?? Symbol.for('quickjs.inspect.custom')](0, { depth: 1 }));

    if(raw.children) {
      let cl = node.children;

      if(raw.children[0]) {
        let y = cl.path;

        let elm = cl[0];
        if(cl.length) {
          if(elm) {
            /*  if(isObject(elm) && 'tagName' in elm) console.log('elm', elm.tagName, elm.path);
            else */ //console.log('elm', elm);
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
          //          console.log('Attr', attr, Node.path(attr));
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
