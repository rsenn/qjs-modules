import { Pointer } from 'pointer';
import { Predicate } from 'predicate';
import { isObject } from 'util';
import * as deep from 'deep';

export class XPath extends Array {
  constructor(...args) {
    super();

    this.splice(
      0,
      this.length,
      ...args.map(arg => {
        if(isObject(arg) && arg instanceof Predicate) return arg;
        let fn = { object: arg => arg, string: elem => elem.tagName == arg, number: (elem, key) => key == arg }[
          typeof arg
        ];
        /*    if(isObject(arg) && arg instanceof Predicate)return arg;
      if(typeof arg == 'string') return elem => elem.tagName==arg;
      if(typeof arg == 'number') return (elem,key) => key==arg;
*/
      })
    );
  }

  static from = buildXPath;
}

export const ImmutableXPath = XPath;
export const MutableXPath = XPath;

export function parseXPath(str) {}

export function buildXPath(ptr, root) {
  let node = root,
    parent,
    prevKey,
    i = 0,
    parts = [],
    siblings,
    siblingTags,
    siblingSame,
    nSame,
    keys,
    prevKeys;
  for(let key of ptr) {
    if(isNaN(+key)) {
      siblingSame = [];
    } else {
      siblings = [...node];
      siblingTags = siblings.map((n, i) => n.tagName) /*.map(([i,n]) => i != key).map(([i,n]) => n)*/;
      siblingSame = siblings.reduce((acc, n, i) => {
        if(n.tagName == node[key].tagName) if (i != key) acc.push([i, n.tagName]);
        return acc;
      }, []);
    }
    nSame = siblingSame.length;
    keys = Object.keys(node);
    /*  if(NodeType(node) == 'array') {
         console.log('', { siblingSameLength: siblingSame.length });
        let len = siblingSame.length;
      }*/
    if(key == 'children') parts.push('/');
    else AddKey(node, key, i, nSame);
    parent = node;
    node = node[key];
    prevKey = key;
    prevKeys = keys;
    i++;
  }
  function AddKey(node, k, i, same) {
    const type = NodeType(node);
    //console.log('', { type, k, i,same });
    if(same == 0 && type == 'array') parts.push(node.tagName);

    parts.push({ children: '/' }[k] ?? k ?? '/');
  }
  function NodeType(node) {
    return isObject(node)
      ? 'length' in node && typeof node.length == 'number'
        ? 'array'
        : util.getClassName(node).toLowerCase()
      : typeof node;
  }
  return parts.join('');
}

export default XPath;
