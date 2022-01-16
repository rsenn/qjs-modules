import * as os from 'os';
import * as std from 'std';
import { escape, quote, isObject, define, getClassName, mapObject, getset, gettersetter, memoize } from '../lib/util.js';
import inspect from 'inspect';
import * as xml from 'xml';
import * as path from 'path';
import * as deep from 'deep';
import Console from '../lib/console.js';
import { Document, Element, Node, Attr, Factory, NamedNodeMap } from '../lib/dom.js';

function main(...args) {
  globalThis.console = new Console(process.stdout, {
    inspectOptions: {
      colors: true,
      depth: Infinity,
      //stringBreakNewline: false,
      maxArrayLength: Infinity,
      compact: false,
      maxStringLength: 60,
      customInspect: true,
      hideKeys: [Symbol.iterator, Symbol.for('quickjs.inspect.custom'), Symbol.inspect]
    }
  });

  let file = args[0] ?? '../../../an-tronics/eagle/555-Oscillator.sch';

  let base = path.basename(file, path.extname(file));

  let data = std.loadFile(file, 'utf-8');

  let start = Date.now();

  let result = xml.read(data, file, false);

  let doc = new Document(result[0]);

  let rawDoc = Node.raw(doc);

  let gs = gettersetter(new Map());
  console.log('gs', gs);
  let mo = mapObject(gs);
  console.log('mo', mo);
  gs = gettersetter(mo);
  console.log('gs', gs);
  console.log('getset', getset(gs));
  console.log('getset', getset(mo));

  memoize((node, ...args) => new Node(node, parent));

  console.log('doc', inspect(doc, { depth: 20, compact: false }));
  console.log('doc.children', doc.children);
  console.log('doc.attributes', doc.attributes);
  console.log('doc.firstChild', doc.firstChild);
  console.log('doc.firstElementChild', doc.firstElementChild);
  console.log('doc.firstElementChild?.nextSibling', doc.firstElementChild?.nextSibling);
  /*  console.log('Object.getOwnPropertyDescriptors(Element.prototype)',Object.getOwnPropertyDescriptors(Element.prototype));
  console.log(`Element.prototype[Symbol.for('quickjs.inspect.custom')]`,Element.prototype[Symbol.for('quickjs.inspect.custom')]+'');
*/
  for(let value of deep.iterate(doc, deep.RETURN_VALUE)) {
    console.log('value', value);
  }

  Recurse(doc, (node, stack) => {
    const raw = Node.raw(node);

    //console.log('node.path', Node.path(node));
    //console.log('node.ownerDocument', node.ownerDocument);
    if(node.nodeType != node.ELEMENT_NODE && node.nodeType != node.DOCUMENT_NODE) {
      /*console.log('node.nodeType', Node.types[node.nodeType]);
      console.log('node', node);
      console.log('node.path', Node.path(node));*/
      return;
    }

    //console.log('raw.children', raw.children);
    if(raw.children) {
      let cl = Factory(raw.children, node);
      console.log('cl', cl);

      if(raw.children[0]) {
        //console.log('raw.children[0]', raw.children[0]);
        console.log('cl.path', cl.path);
        let elm = Factory(raw.children[0], cl, Element);
        console.log('elm', elm);
        console.log('elm.path', elm.path);
 console.log('elm.ownerDocument', elm.ownerDocument);
      }
    }

    if(raw.attributes) {
      //console.log('raw.attributes', raw.attributes);
      let al = Factory(raw.attributes, node, NamedNodeMap);
      console.log('al', al, al.path);

      let at = al[Object.keys(raw.attributes)[0]];

      if(at) {
        console.log('at', at,at.path);
        console.log('at.ownerElement', at.ownerElement, at.ownerElement.path);
      }
    }

    console.log('node.nextSibling', node.nextSibling, Node.types[node.nodeType], Node.path(node).slice(-2));

    /*else {
      console.log('node.tagName', node.tagName);
      console.log('node.parentElement', node.parentElement);
      console.log('node.parentNode', node.parentNode);
      const { attributes } = Node.raw(node);
      if(isObject(attributes)) {
        console.log('attributes', attributes);
        if(attributes[Symbol.iterator]) for(let attr in attributes) Recurse([attr, attributes[attr]], fn, [...stack, node]);
      }
    }
*/
    console.log(node.tagName ? `<${node.tagName} ${node.nodeType} ${stack.length}>` : node[Symbol.toStringTag] ?? node);
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

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
}
