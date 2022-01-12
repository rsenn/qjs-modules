import { Pointer } from 'pointer';
import { Predicate } from 'predicate';
import { isObject, extendArray } from 'util';
import * as deep from 'deep';
import inspect from 'inspect';

export class XPath extends Array {
  constructor(...args) {
    super();

    if(args.length == 1) {
      if(typeof args[0] == 'string') args = args[0].split(/\//g);
      else if(Array.isArray(args[0])) args = args[0];
    }
    /*
    this.splice(
      0,
      this.length,
      ...args.map(arg => {
        if(/^[a-z]/.test(arg)) {

        } else 
        if(isObject(arg) && arg instanceof Predicate) return arg;
        let fn = { object: arg => arg, string: elem => elem.tagName == arg, number: (elem, key) => key == arg }[
          typeof arg
        ]; 
      })
    );*/
    if(args.length > 1 && args[0] === '') args.shift();

    for(let arg of args) {
      let m = /^[A-Za-z][-\w]*/.exec(arg);
      console.log('m', m);
      if(m.length >= 1) {
        arg = Predicate.property('tagName', /*s => s == m[0] ??*/ Predicate.string(m[0]));
      }

      this.push(arg);
    }
  }

  [Symbol.inspect](depth, opts = {}) {
    let obj = [...this];
    return (
      `\x1b[1;31m${this.constructor.name ?? util.getClassName(this)}\x1b[0m ` +
      inspect(obj ?? { __proto__: { [Symbol.toStringTag]: 'XPath' }, ...obj }, opts)
    );
  }

  static from = buildXPath;
}

XPath.prototype[Symbol.toStringTag] = 'XPath';

export const ImmutableXPath = XPath;
export const MutableXPath = XPath;

export function parseXPath(str) {}

export function buildXPath(ptr, root) {
  let node = root,
    parent,
    prevKey,
    i = 0,
    parts = [''],
    siblings,
    siblingTags,
    siblingSame,
    keys,
    prevKeys,
    type,
    same;
  for(let key of ptr) {
    type = NodeType(node);

    if(type != 'array') {
      siblingSame = [];
    } else {
      siblings = [...node];
      siblingTags = siblings.map((n, i) => n.tagName) /*.map(([i,n]) => i != key).map(([i,n]) => n)*/;
      siblingSame = siblings.reduce((acc, n, i) => {
        if(n.tagName == node[key].tagName) /*if (i != key)*/ acc.push([i, n.tagName]);
        return acc;
      }, []);
    }
    keys = Object.keys(node);
    /*  if(NodeType(node) == 'array') {
         console.log('', { siblingSameLength: siblingSame.length });
        let len = siblingSame.length;
      }*/
    console.log('', { type, key, i });
    if(key == 'children') {
      //parts.push('/');
    } else {
      same = siblingSame.length || undefined;

      const part = AddKey(node[key], key, i, same);
      parts.push(part);
    }
    parent = node;
    node = node[key];
    prevKey = key;
    prevKeys = keys;
    i++;
  }
  function AddKey(node, k, i, same) {
    let s = '';
    if(isObject(node) && 'tagName' in node) {
      s = node.tagName;
    }
    if(s === '' && typeof k == 'number' && same > 1) {
      /*if(node?.attributes?.name) s += `[@name="${node.attribtues.name}"]`;
          else*/ s += `[${k}]`;
    }

    if(s === '') {
      s = { children: '/' }[k] ?? k ?? '/';
    }
    console.log('', { type, k, i, same, s });
    return s;
  }
  function NodeType(node) {
    return isObject(node)
      ? 'length' in node && typeof node.length == 'number'
        ? 'array'
        : util.getClassName(node).toLowerCase()
      : typeof node;
  }
  return new XPath(parts);
}

extendArray(XPath.prototype);

export default XPath;
