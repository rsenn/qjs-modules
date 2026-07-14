import { className, define, isFunction, isObject } from 'util';
import { Pointer } from './pointer.js';
import { Predicate } from './predicate.js';

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

export class DereferenceError extends Error {
  constructor(message) {
    super(message);
  }
}

DereferenceError.prototype[Symbol.toStringTag] = 'DereferenceError';

export class XPath {
  constructor(...args) {
    if(args.length == 1) {
      if(typeof args[0] == 'string') args = args[0].split(/\//g);
      else if(Array.isArray(args[0])) args = args[0];
    }

    if(args.length > 1 && args[0] === '') args.shift();
    let i = 0;

    for(let arg of args) {
      if(typeof arg == 'string') {
        if(arg != 'children' && arg != 'attributes') {
          let m = /^([A-Za-z][-\w]*)(\[[0-9]+\]|)$/.exec(arg);

          if(m) {
            m = [...m].slice(1, m[2] === '' ? 2 : 3);

            if(m.length >= 1) arg = Predicate.property('tagName', Predicate.string(m[0]));

            if(m.length >= 2) {
              const num = +m[1].slice(1, -1);
              arg = Predicate.and(arg, Predicate.shift(1, Predicate.string(num - 1)));
            }
          }
        }
      }

      this[i++] = arg;
    }
  }

  get length() {
    let i;

    for(i = 0; i in this; i++) {}

    return i;
  }

  deref(obj) {
    for(let i = 0; i in this; i++) {
      const pred = this[i];

      // Plain (non-predicate) segments - e.g. 'children'/'attributes', or any
      // path part that didn't parse as a tagName[index] pattern - are a direct
      // property step, not a search through obj.children.
      if(typeof pred != 'function') {
        if(obj == null || typeof obj != 'object' || !(pred in obj))
          throw new DereferenceError(`Dereference Error i=${i}: property '${pred}' not found`);

        obj = obj[pred];
        continue;
      }

      let found = false,
        prop,
        k;

      if(!obj?.children) throw new DereferenceError(`Dereference Error i=${i}: ${typeof obj} has no 'children' property`);

      if(obj?.children)
        for(const key in obj.children) {
          k = key;
          if(pred(obj.children[key], key, obj)) {
            found = true;
            prop = key;
            break;
          }
        }

      if(!found) throw new DereferenceError(`Dereference Error i=${i} prop=${prop} key=${k}`);

      obj = obj.children[prop];
    }

    return obj;
  }

  *[Symbol.iterator]() {
    for(let i = 0; i in this; i++) yield this[i];
  }

  *toPointer(obj) {
    const props = [];

    for(let i = 0; i in this; i++) {
      const pred = this[i];

      if(typeof pred != 'function') {
        if(obj == null || typeof obj != 'object' || !(pred in obj)) throw new Error(`Dereference Error i=${i} prop=${pred} props=[${props}]`);

        obj = obj[pred];
        yield pred;
        props.push(pred);
        continue;
      }

      let found = false,
        prop;

      for(const key in obj.children) {
        if(pred(obj.children[key], key, obj)) {
          found = true;
          prop = key;
          break;
        }
      }

      if(!found) throw new Error(`Dereference Error i=${i} pred=${pred} props=[${props}]`);

      obj = obj.children[prop];

      yield 'children';
      yield prop;

      props.push(prop);
    }

    return obj;
  }

  [inspectSymbol](depth, opts = {}) {
    return `${this.constructor.name ?? className(this)} { ${[...this].map(GetKey).join(', ')} }`;
  }

  get [Symbol.toStringTag]() {
    return 'XPath';
  }

  static from = buildXPath;
}

define(XPath.prototype, { [Symbol.toStringTag]: 'XPath' });

export const ImmutableXPath = XPath;

export const MutableXPath = XPath;

export function parseXPath(str) {
  return new XPath(str);
}

export function getSiblings(ptr, root) {
  // ptr addresses a node as a sequence of ['children', key, 'children', key, ...]
  // steps (the same convention XPath.toPointer()/buildXPath() use); the parent
  // whose .children are the siblings is reached by dropping the trailing
  // ['children', key] pair.
  const keys = [...ptr];
  let node = root;

  for(const key of keys.slice(0, -2)) node = node[key];

  if(!node?.children) throw new DereferenceError(`getSiblings: parent has no 'children' property`);

  return Object.keys(node.children).map(key => node.children[key]);
}

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
        const a = Array.from(node),
          fn = key;

        key = a.findIndex(fn);
      }
    }

    type = key == 'children' ? 'object' : NodeType(node);

    if(parent && 'children' in parent) {
      siblings = [...parent.children];

      siblingSame = siblings.reduce((acc, n, i) => {
        if(isObject(n) && isObject(node[key])) if (n.tagName == node[key].tagName) acc.push([i, n.tagName]);
        return acc;
      }, []);
    } else {
      siblings = [];
      siblingSame = [];
    }

    keys = isObject(node) ? Object.keys(node) : [];

    if(key == 'children') {
      //parts.push('/');
    } else {
      same = siblingSame.length;

      const partKey = AddKey(node[key], key, i, same);

      parts.push(partKey);
    }

    if(!isObject(node)) throw new Error(`Error indexing <${typeof node}> with <${key}> [${hier.join(', ')}]: ${parts.map(GetKey).join(' -> ')}`);

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
      // Bracket indices are 1-based to match the tagName[N] parsing in the
      // XPath constructor (which subtracts 1 to get the 0-based key).
      if(same > 1) s += `[${k + 1}]`;
    } else if(typeof k == 'string') {
    }

    if(s === '') s = { children: '/' }[k] ?? k ?? '/';

    return s;
  }

  function NodeType(node) {
    return isObject(node) ? ('length' in node && typeof node.length == 'number' ? 'array' : className(node).toLowerCase()) : typeof node;
  }

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
  } else
    throw new Error(
      `Invalid part (${util.getTypeStr(p)} ${typeof p}): ${inspect(p, {
        colors: true,
      })}`,
    );

  return k ?? p;
}

export class XPathEvaluator {
  createExpression(expression, resolver) {
    return new XPath(expression);
  }
}

export class XPathException {
  constructor(code) {
    define(this, { code });
  }
}

export class XPathResult {
  constructor(obj = {}) {
    const { booleanValue, invalidIteratorState, numberValue, resultType, singleNodeValue, snapshotLength } = obj;

    Object.assign(this, {
      booleanValue,
      invalidIteratorState,
      numberValue,
      resultType,
      singleNodeValue,
      snapshotLength,
    });
  }
}

define(XPathResult.prototype, {
  booleanValue: undefined,
  invalidIteratorState: undefined,
  numberValue: undefined,
  resultType: undefined,
  singleNodeValue: undefined,
  snapshotLength: undefined,
});

export const XPathExpression = XPath;

export default XPath;
