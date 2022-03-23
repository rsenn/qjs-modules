import { Pointer } from 'pointer';
import { Predicate } from 'predicate';
import { define, isObject, isFunction, extendArray, getClassName } from 'util';
import { Node } from './dom.js';
import * as deep from 'deep';
import inspect from 'inspect';

export class XPath extends Pointer {
  constructor(...args) {
    super();

    if(args.length == 1) {
      if(typeof args[0] == 'string') args = args[0].split(/\//g);
      else if(Array.isArray(args[0])) args = args[0];
    }

    if(args.length > 1 && args[0] === '') args.shift();

    for(let arg of args) {
      if(typeof arg == 'string') {
        if(arg != 'children' && arg != 'attributes') {
          let m = /^([A-Za-z][-\w]*)(\[[0-9]+\]|)$/.exec(arg);
          if(m) {
            m = [...m].slice(1, m[2] === '' ? 2 : 3);
            //console.log('m', m);
            if(m.length >= 1) {
              arg = Predicate.property('tagName', /*s => s == m[0] ??*/ Predicate.string(m[0]));
            }

            if(m.length >= 2) {
              let num = +m[1].slice(1, -1);
              arg = Predicate.and(arg, Predicate.equal(num));
              console.log('arg', arg);
            }
          }
        }
      }

      this.push(arg);
    }
    console.log('XPath.constructor', this);
  }

  [Symbol.inspect](depth, opts = {}) {
    let obj = [...this].map(GetKey);
    //console.log('obj', obj);
    return (
      `\x1b[1;31m${this.constructor.name ?? util.getClassName(this)}\x1b[0m ` +
      inspect(obj ?? { __proto__: { [Symbol.toStringTag]: 'XPath' }, ...obj }, {
        ...opts,
        customInspect: true
      })
    );
  }

  static from = buildXPath;
}

define(XPath.prototype, { [Symbol.toStringTag]: 'XPath' });

export const ImmutableXPath = XPath;
export const MutableXPath = XPath;

export function parseXPath(str) {}

export function getSiblings(ptr, root) {}

export function buildXPath(ptr, root) {
  let node = root,
    parent,
    prevKey,
    i = 0,
    parts = [],
    siblings,
    siblingSame,
    keys,
    type,
    same,
    hier = [];
  for(let key of ptr) {
    if(typeof key == 'function') {
      if(isObject(node) && 'length' in node) {
        let a = Array.from(node),
          fn = key;

        key = a.findIndex(fn);
      }
    }

    type = key == 'children' ? 'object' : NodeType(node);

    //  console.log('parent',parent);

    if(parent && 'children' in parent) {
      siblings = [...parent.children];
      //console.log('siblings',siblings);
      siblingSame = siblings.reduce((acc, n, i) => {
        if(isObject(n) && isObject(node[key])) if (n.tagName == node[key].tagName) acc.push([i, n.tagName]);
        return acc;
      }, []);
    } else {
      siblings = [];
      siblingSame = [];
    }
    //if(siblings.length >= 2) console.log('siblingsSame',siblingSame);

    keys = isObject(node) ? Object.keys(node) : [];

    if(key == 'children') {
      //parts.push('/');
    } else {
      same = siblingSame.length;

      // console.log('same',same)

      let part = AddKey(node[key], key, i, same);

      parts.push(part);
    }

    if(!isObject(node))
      throw new Error(
        `Error indexing <${typeof node}> with <${key}> [${hier.join(', ')}]: ${parts.map(GetKey).join(' -> ')}`
      );

    parent = node;
    node = node[key];
    hier.push(key);
    prevKey = key;
    i++;
  }

  function AddKey(node, k, i, same) {
    let s = '';

    if(isObject(node) && 'tagName' in node) s = node.tagName;

    if(typeof k == 'number') {
      if(same > 1) s += `[${k}]`;
    } else if(typeof k == 'string') {
    }
    if(s === '') s = { children: '/' }[k] ?? k ?? '/';
    return s;
  }

  function NodeType(node) {
    return isObject(node)
      ? 'length' in node && typeof node.length == 'number'
        ? 'array'
        : getClassName(node).toLowerCase()
      : typeof node;
  }
  //console.log('buildXPath', parts);
  return Object.setPrototypeOf(parts, XPath.prototype);
}

function GetKey(p) {
  let k;
  if((isObject(p) || isFunction(p)) && p instanceof Predicate) {
    if(p.id == Predicate.PROPERTY) {
      const str = p.values()[1];
      k = str.values()[0];
    }
  } else if(typeof p == 'object' && ['tagName', 'children', 'attributes'].some(prop => prop in p)) {
    k = p.tagName;
  } else if(typeof p == 'function' /* && !(p instanceof Predicate)*/) {
    if(/tagName\s*==/.test(p + '')) k = (p + '').replace(/.*'(.*)'$/, '$1');
    else k = p;
  } else if(typeof p == 'number' || typeof p == 'string') {
    k = p;
  } else throw new Error(`Invalid part (${util.getTypeStr(p)} ${typeof p}): ${inspect(p, { colors: true })}`);
  return k ?? p;
}

extendArray(XPath.prototype);

export default XPath;
