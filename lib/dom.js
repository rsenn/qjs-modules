//import { Pointer } from 'pointer';
import * as deep from 'deep';
import inspect from 'inspect';
import { isObject, define, escape, quote, range, assert, memoize, getClassName, getClassID, mapObject, getset, gettersetter } from './util.js';

//const Path = Pointer;

const inspectSymbol = Symbol.inspect ?? Symbol.for('quickjs.inspect.custom');

const rawNode = new WeakMap();
const parentNode = new Map();
const ownerElement = new Map();

const ELEMENT_NODE = 1;
const ATTRIBUTE_NODE = 2;
const TEXT_NODE = 3;
const CDATA_SECTION_NODE = 4;
const ENTITY_REFERENCE_NODE = 5;
const ENTITY_NODE = 6;
const PROCESSING_INSTRUCTION_NODE = 7;
const COMMENT_NODE = 8;
const DOCUMENT_NODE = 9;
const DOCUMENT_TYPE_NODE = 10;
const DOCUMENT_FRAGMENT_NODE = 11;
const NOTATION_NODE = 12;

const keyOf = (obj, value) => {
  for(let key in obj) if(obj[key] === value) return key;
  return -1;
};

export const nodeTypes = [, 'ELEMENT_NODE', 'ATTRIBUTE_NODE', 'TEXT_NODE', 'CDATA_SECTION_NODE', 'ENTITY_REFERENCE_NODE', 'ENTITY_NODE', 'PROCESSING_INSTRUCTION_NODE', 'COMMENT_NODE', 'DOCUMENT_NODE', 'DOCUMENT_TYPE_NODE', 'DOCUMENT_FRAGMENT_NODE', 'NOTATION_NODE'];

function checkNode(node) {
  if(!isObject(node)) throw new TypeError('node is not an object');
  if(!node[Symbol.toStringTag]) throw new TypeError('node does not have [Symbol.toStringTag]');
  if(Object.getPrototypeOf(node) == Object.prototype) throw new TypeError('node is Object');
}

export class Node {
  constructor(obj, parent, proto = Node.prototype) {
    if(isObject(proto)) assert(Object.getPrototypeOf(this) instanceof Node, true, 'proto');
    assert(this instanceof Node, true, 'instanceof Node');

    rawNode.set(this, obj);

    parentNode.set(this, parent);
    //parentNode.set(obj, parent);
  }

  static raw(node) {
    checkNode(node);
    return rawNode.get(node);
  }

  static owner(node) {
    checkNode(node);
    let owner;
    if((owner = ownerElement.get(node))) return owner;

    node = Node.raw(node);
    return ownerElement.get(node);
  }

  static parent(node) {
    checkNode(node);
    let ret = parentNode.get(node);
    return ret;
  }

  static hier(node, pred = node => true) {
    const r = [];
    checkNode(node);
    let next;
    do {
      if(pred(node)) r.unshift(node);
      next = ({ NodeList: Node.owner, NamedNodeMap: Node.owner }[node.constructor.name] ?? Node.parent).call(Node, node);
    } while(next && (node = next));
    return r;
  }

  static document(node) {
    const hier = Node.hier(node);
    return hier.find(({ nodeType }) => nodeType == DOCUMENT_NODE);
  }

  static path(arg, path = []) {
    if(arg.ownerElement) return Node.path(arg.ownerElement).concat({ NodeList: ['children'], NamedNodeMap: ['attributes'], Attr: ['attributes', arg.name] }[arg.constructor.name]);
    const hier = Node.hier(arg);
    while(hier.length >= 2) {
      let index = keyOf(Node.raw(hier[0]), Node.raw(hier[1]));
      if(index == -1) index = keyOf(hier[0], hier[1]);

      if(!isNaN(+index)) index = +index;
      path.push(index);
      hier.shift();
    }
    return path;
  }

  get parentNode() {
    let r = Node.parent(this);
    if(isObject(r) && !(r instanceof Node)) r = Node.owner(r);
    return r;
  }

  get ownerDocument() {
    let node = this;
    while(node && node.nodeType != DOCUMENT_NODE) node = node.parentNode;
    return node;
  }

  get nextSibling() {
    let parent;
    if((parent = Node.parent(this))) {
      let index = parent.indexOf(this);
      if(index != -1) return parent[index + 1];
    }
  }

  get previousSibling() {
    let parent;
    if((parent = Node.parent(this))) {
      let index = parent.indexOf(this);
      if(index != -1) return parent[index - 1];
    }
  }
}

Node.types = nodeTypes;
const NODE_TYPES = {
  ATTRIBUTE_NODE,
  CDATA_SECTION_NODE,
  COMMENT_NODE,
  DOCUMENT_FRAGMENT_NODE,
  DOCUMENT_NODE,
  DOCUMENT_TYPE_NODE,
  ELEMENT_NODE,
  ENTITY_NODE,
  ENTITY_REFERENCE_NODE,
  NOTATION_NODE,
  PROCESSING_INSTRUCTION_NODE,
  TEXT_NODE
};
define(Node.prototype, NODE_TYPES);
define(Node, NODE_TYPES);

Node.prototype[Symbol.toStringTag] = 'Node';

export class NodeList {
  constructor(obj, owner) {
    let nodeList;
    const isElement = prop => typeof prop == 'string' && !isNaN(+prop);
    const isList = prop => isElement(prop) || prop == 'length';
    const wrapElement = (value, prop) => (typeof value == 'string' ? new Text(value, nodeList) : isObject(value) && 'tagName' in value ? new Element(value, nodeList) : value);

    rawNode.set(this, obj);
    //parentNode.set(obj, owner);
    ownerElement.set(this, owner);

    nodeList = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(isList(prop)) {
          if(prop == 'length') return obj.length;
          if(prop >= 0 && prop < obj.length) return wrapElement(obj[prop], prop);
        }
        return Reflect.get(target, prop, receiver);
      },
      getOwnPropertyDescriptor: (target, prop) => {
        if(isList(prop)) if (prop >= 0 && prop < obj.length) return { configurable: true, enumerable: true, value: obj[prop] };
        return Reflect.getOwnPropertyDescriptor(target, prop);
      },
      ownKeys: target =>
        range(0, obj.length - 1)
          .map(prop => prop + '')
          .concat(['length']),
      getPrototypeOf: target => NodeList.prototype
    });

    rawNode.set(nodeList, obj);
    ownerElement.set(nodeList, owner);

    return nodeList;
  }
}

NodeList.prototype.__proto__ = Array.prototype;

define(NodeList.prototype, {
  [Symbol.toStringTag]: 'NodeList',
  get path() {
    let owner = Node.owner(this);
    return Node.path(owner).concat(['children']);
  },

  [inspectSymbol](depth, opts) {
    const raw = Node.raw(this);
    return util
      .range(0, raw.length - 1)
      .map(n => inspect(this[n], { ...opts, depth: depth + 1 /*, inspectCustom: true */ })) // Element.prototype[inspectSymbol].call(raw[n], depth + 1, opts))
      .join(',\n  ');
  }
});

export function NamedNodeMap(obj, owner) {
  if(!this) return new NamedNodeMap(obj, owner);

  let nodeMap;
  const isAttr = prop => typeof prop == 'string' && prop in obj;
  const wrapAttr = (value, prop) => new Attr([obj, prop], nodeMap);

  rawNode.set(this, obj);
  //parentNode.set(obj,owner);
  ownerElement.set(this, owner);

  nodeMap = new Proxy(this, {
    get: (target, prop, receiver) => {
      //  if(prop == 'item') return i => isNaN(+i) ? obj[i] : obj[Object.keys(obj)[+i]];
      if(prop == 'length') return Object.keys(obj).length;
      if(typeof prop == 'string') {
        if(!isNaN(+prop)) {
          let keys = Object.keys(obj);
          if(prop >= 0 && prop < keys.length) return wrapAttr(obj[keys[+prop]], keys[+prop]);
        } else if(prop in obj) return wrapAttr(obj[prop], prop);
      }
      return Reflect.get(target, prop, receiver);
    },
    ownKeys: target => Object.keys(obj)
  });

  rawNode.set(nodeMap, obj);
  ownerElement.set(nodeMap, owner);

  return nodeMap;
}

Object.setPrototypeOf(NamedNodeMap.prototype, Array.prototype);

define(NamedNodeMap.prototype, {
  get path() {
    let owner = Node.owner(this);
    return Node.path(owner).concat(['attributes']);
  },

  item(key) {
    return this[key];
    /*    const raw = Node.raw(this);
    let item = isNaN(+key) ? raw[key] : raw[Object.keys(raw)[+key]];
    if(item) item = wrapAttr(item, key);
    return item;*/
  },
  setNamedItem(attr) {
    const raw = Node.raw(this);
    const { name, value } = attr;
    raw[name] = value;
  },
  removeNamedItem(name) {
    const raw = Node.raw(this);
    delete raw[name];
  },
  getNamedItem(name) {
    const raw = Node.raw(this);
    return raw[name];
  },

  *[Symbol.iterator]() {
    const { length } = this;
    for(let i = 0; i < length; i++) yield this.item(i);
  },
  [Symbol.toStringTag]: 'NamedNodeMap',
  [inspectSymbol]() {
    const raw = Node.raw(this);
    let s = '';
    for(let attr in raw) {
      s += ' \x1b[1;35m' + attr + '\x1b[1;36m="' + raw[attr] + '"\x1b[0m';
    }
    return '{' + s + ' }';
    return Object.getOwnPropertyNames(this).reduce((acc, prop) => ({ ...acc, [prop]: this[prop] }), {});
  }
});

export class Element extends Node {
  constructor(obj, parent) {
    super(obj, parent);
  }

  get parentElement() {
    let obj = this;
    while(isObject(obj) && obj.nodeType != ELEMENT_NODE) obj = Node.parent(obj);
    return obj;
  }
  get attributes() {
    return new NamedNodeMap((Node.raw(this).attributes ??= {}), this);
  }

  set tagName(v) {
    Node.raw(this).tagName = v;
  }
  get tagName() {
    return Node.raw(this).tagName;
  }

get path() {
  return Node.path(this); 
}

  setAttribute(name, value) {
    (Node.raw(this).attributes ??= {})[name] = value;
  }
  removeAttribute(name) {
    delete (Node.raw(this).attributes ??= {})[name];
  }
  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    return `<${tagName} attributes: ${attributes[inspectSymbol](depth + 1, opts)} children: ${children[inspectSymbol](depth + 1, opts)})}>`;
  }

  get children() {
    return new NodeList((Node.raw(this).children ??= []), this);
  }
  get childElementCount() {
    return Node.raw(this).children?.length ?? 0;
  }
  get firstChild() {
    const { children } = Node.raw(this);
    if(children?.length) {
      let [first] = children;
      return new { object: Element, string: Text }[typeof first](first, this);
    }
  }
  get lastChild() {
    const { children } = Node.raw(this);
    if(children?.length) {
      let last = children[children.length - 1];
      return new { object: Element, string: Text }[typeof last](last, this);
    }
  }
  get firstElementChild() {
    const { children } = Node.raw(this);
    let element = children.find(n => n.nodeType == ELEMENT_NODE);
    if(element) return new Element(element, this);
  }
  get lastElementChild() {
    const { children } = Node.raw(this);
    if(children?.length) for(let i = children.length - 1; i >= 0; i--) if (children[i].nodeType == ELEMENT_NODE) return new Element(children[i], this);
  }
}

define(Element.prototype, { [Symbol.toStringTag]: 'Element', nodeType: ELEMENT_NODE });

export class Document extends Element {
  constructor(obj) {
    super(obj, null, Document.prototype);
  }

  createAttribute(name, value) {
    return new Attr([null, name], null);
  }

  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    return `\x1b[1;31mDocument\x1b[0m ${tagName} attributes: ${attributes[inspectSymbol](depth + 1, opts)} children: ${inspect(children.map(c => c[inspectSymbol](depth + 2, opts)))}>`;
  }
}

define(Document.prototype, { [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE });

export class Attr extends Node {
  constructor(raw, owner) {
   // console.log('Attr', { raw, owner });
    super(raw, owner);

rawNode.set(this, raw);
    ownerElement.set(this,owner);

    define(this, {});
  }
}

define(Attr.prototype, {
  nodeType: ATTRIBUTE_NODE,
  get path() {
     let {ownerElement}=this;
     let [obj, name] = Node.raw(this);
     //console.log('path',{owner,parent});
    return Node.path(ownerElement).concat(['attributes',name]);
  },
  get ownerElement() {
    return Node.owner(Node.owner(this));
  },
  get ownerDocument() {
    return Node.document(this);
  },
  get name() {
    const [name] = Node.raw(this);
    return name;
  },
  get value() {
    const [name, obj] = Node.raw(this);
    return obj[name];
  },
  set name(v) {
    const raw = Node.raw(this);
    raw[0] = v;
  },
  set value(v) {
    const [name, obj] = Node.raw(this);
    obj[name] = v;
  },
  [Symbol.toStringTag]: 'Attr[]',
  [inspectSymbol]() {
    const { name, value } = this;
    return `\x1b[1;35m${name}\x1b[1;34m=${quote(value, '"')}\x1b[0m`;
  }
});

export class Text extends Node {
  constructor(value, parent) {
    super(value, parent);

    define(this, { nodeType: TEXT_NODE, value });
  }
}

Text.prototype.__proto__ = Node.prototype;
define(Text.prototype, {
  [Symbol.toStringTag]: Text.name,
  [inspectSymbol]() {
    return `\x1b[1;32m'${escape(this.value)}'\x1b[0m`;
  }
});

function Membrane(instance, obj, proto, wrapProp, wrapElement) {
  throw new Error('Membrane');
  return new Proxy(instance, {
    get: (target, prop, receiver) => (wrapProp(prop) ? wrapElement(obj[prop], prop) : Reflect.get(target, prop, receiver)),
    has: (target, prop) => (wrapProp(prop) ? true : Reflect.has(target, prop)),
    getOwnPropertyDescriptor: (target, prop) => (wrapProp(prop) ? { configurable: true, enumerable: true, writable: true, value: wrapElement(obj[prop], prop) } : Reflect.getOwnPropertyDescriptor(target, prop)),
    getPrototypeOf: target => proto ?? Object.getPrototypeOf(instance),
    setPrototypeOf: (target, p) => (proto = p),
    ownKeys: target => [...Reflect.ownKeys(target)]
  });
}

/*export function MakeFactory() {
  const memo = memoize((raw, owner, ctor) => {
    const isArray = ctor == NodeList || owner.children == raw;
    const isElement = ctor == Element || (owner.children != raw && owner.attributes != raw); 
    console.log('\x1b[38;2;112;112;252mMemoize\x1b[0m', { isArray, isElement, raw });

    if(Symbol.toStringTag in owner) owner = Node.raw(owner);
    const key = isArray ? 'children' : !isElement ? 'attributes' : keyOf(owner, raw);
    const list = owner[key];
    ctor ??= owner.attributes == raw ? NamedNodeMap : owner.children == raw ? NodeList : Element;
    let ret = new ctor(raw, owner, ctor);
    //console.log('\x1b[38;2;112;112;252mMemoize\x1b[0m', { key, ctor, ret });
    return ret;
  });

  return define(
    (raw, owner, ctor) => {
      let ret;
      if((ret = memo.get(raw))) {
        ret instanceof Node ? parentNode.set(raw, owner) : ownerNode.set(raw, owner);
        return ret;
      }
      return memo(raw, owner, ctor);
    },
    { memo }
  );
}*/

let map = new WeakMap();

export function Factory(raw, owner, ctor) {
  let obj;
  if((obj = map.get(raw))) {
    ownerElement.set(obj, owner);
    return obj;
  }

  const isArray = arg => Array.isArray(arg);
  const isAttributes = arg => isObject(arg) && !isArray(arg) && !('tagName' in arg);

  ctor ??= isArray(raw) ? NodeList : isAttributes(raw) ? NamedNodeMap : typeof raw == 'string' ? Text : Element;

  obj = new ctor(raw, owner);
  return obj;
}
