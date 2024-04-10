import { readFileSync } from 'fs';
import { arrayFacade, assert, camelize, decamelize, define, getset, gettersetter, isObject, lazyProperties, memoize, modifier, quote, range, properties } from 'util';
import { parseSelectors } from './css3-selectors.js';
import { get, iterate, RETURN_PATH } from 'deep';
import { TreeWalker } from 'tree_walker';
import { read as readXML, write as writeXML } from 'xml';

const inspectSymbol = Symbol('noinspect'); //Symbol.for('quickjs.inspect.custom');

const rawNode = new WeakMap();
const parentNodes = new Map();
const ownerElements = new Map();
const textValues = gettersetter(new WeakMap());

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

const createFunctions = [
  undefined,
  'createElement',
  'createAttribute',
  'createText',
  'createCDATASection',
  'createEntityReference',
  'createEntity',
  'createProcessingInstruction',
  'createComment',
  'createDocument',
  'createDocumentType',
  'createDocumentFragment',
  'createNotation'
];

const EntityNames = ['Document', 'Node', 'NodeList', 'Element', 'NamedNodeMap', 'Attr', 'Text', 'Comment', 'TokenList', 'CSSStyleDeclaration'];
const EntityType = name => EntityNames.indexOf(name);

export const Entities = EntityNames.reduce((obj, name, id) => ({ [name]: id, ...obj }), {});

const keyOf = (obj, value) => {
  for(let key in obj) if(obj[key] === value) return key;
  return -1;
};

class DereferenceError extends Error {
  constructor(obj, i, path, error) {
    super(`dereference error of <${obj}> at ${i} '${path.slice(0, i)}': ${error.message}`);
  }
}

//const applyPath = (path, obj) => path.reduce((acc, part) => acc[part], obj);

function applyPath(path, obj) {
  const { length } = path;
  let raw = rawNode.get(obj);

  for(let i = 0; i < length; i++) {
    const k = path[i];

    try {
      obj = obj[k];
    } catch(error) {
      throw new DereferenceError(obj, i, path, error);
    }

    if(raw)
      try {
        raw = raw[k];

        rawNode.set(obj, raw);
      } catch(error) {
        raw = undefined;
      }

    //console.log(`applyPath[${i + 1}/${length}]`, { k, obj });
  }

  return obj;
}

function* query(root, selectors, t = (path, root) => path) {
  for(let selector of selectors) for (let path of iterate(root, selector, RETURN_PATH)) yield t(path, root);
}

export const nodeTypes = [
  undefined,
  'ELEMENT_NODE',
  'ATTRIBUTE_NODE',
  'TEXT_NODE',
  'CDATA_SECTION_NODE',
  'ENTITY_REFERENCE_NODE',
  'ENTITY_NODE',
  'PROCESSING_INSTRUCTION_NODE',
  'COMMENT_NODE',
  'DOCUMENT_NODE',
  'DOCUMENT_TYPE_NODE',
  'DOCUMENT_FRAGMENT_NODE',
  'NOTATION_NODE'
];

function checkNode(node) {
  if(!isObject(node)) throw new TypeError('node is not an object');
}

export function Prototypes(
  constructors = {
    Document,
    Node,
    NodeList,
    Element,
    NamedNodeMap,
    Attr,
    Text,
    Comment,
    TokenList,
    CSSStyleDeclaration
  }
) {
  const prototypes = {};

  for(let key in constructors) prototypes[key] = constructors[key].prototype;

  return prototypes;
}

let factories = gettersetter(new WeakMap());

export function Factory(types = Prototypes()) {
  let ret;

  if(new.target) {
    ret = function Factory(type) {
      if(typeof type == 'number') type = EntityNames[type];
      // if(typeof type == 'string') type = EntityType(type);

      return ret[type].new;
    };

    if(Array.isArray(types)) types = types.reduce((acc, proto, i) => ({ [EntityNames[i]]: proto, ...acc }), {});

    if(typeof types != 'function') {
      const obj = types;

      types = (type, ...args) => {
        if(typeof type == 'number') type = EntityNames[type];
        let proto = obj[type];
        return new proto.constructor(...args);
      };
      types.cache = (type, ...args) => {
        if(typeof type == 'number') type = EntityNames[type];

        const proto = obj[type];

        return (proto.constructor.cache ?? MakeCache((...a) => new proto.constructor(...a)))(...args);
      };
    }

    for(let i = 0; i < EntityNames.length; i++) {
      const name = EntityNames[i];

      ret[name] = {
        new: (...args) => types(name, ...args),
        cache: (...args) => types.cache(name, ...args)
      };
    }

    return Object.setPrototypeOf(ret, Factory.prototype);
  }

  return factories(types);
}

Object.setPrototypeOf(Factory.prototype, Function.prototype);

define(Factory, {
  for: node => {
    //return Node.hier(node).reduceRight((acc, node) => acc ?? factories(node));
    let p = node;
    do {
      let f = factories(Node.raw(p));
      if(f) return f;
    } while((p = Node.parent(p) ?? Node.owner(p)));

    for(let n of Node.hier(node)) {
      let f = factories(Node.raw(n));
      if(f) return f;
    }
  },
  set: (node, factory) => factories(Node.raw(node), factory)
});

export class Parser {
  constructor(factory) {
    factory ??= new Factory();

    this.factory = factory;
  }

  parseFromString(str, file) {
    let data = readXML(str, file);

    if(Array.isArray(data)) {
      if(data[0].tagName != '?xml')
        data = {
          tagName: '?xml',
          attributes: { version: '1.0', encoding: 'utf-8' },
          children: data
        };
      else if(data.length == 1) data = data[0];
    }

    const { factory } = this;
    const doc = factory.Document.new(data, factory);
    Factory.set(doc, factory);

    return doc;
  }

  parseFromFile(file) {
    const xml = readFileSync(file);
    const { factory } = this;

    return this.parseFromString(xml, file, factory);
  }
}

function GetNode(obj, owner, factory) {
  const type = GetType(obj, owner);

  if(type === Entities.Text && typeof obj == 'string') obj = owner.indexOf(obj);

  factory ??= Factory.for(owner);

  if(!factory) console.log('No factory for', owner, owner.constructor.name);
  let ctor = factory(type);

  if(!ctor) throw new Error(`No such node type for ${obj[inspectSymbol]()}`);
  if(ctor.cache) return ctor.cache(obj, owner);

  return ctor(obj, owner);
}

export class Interface {
  get parentNode() {
    let r = Node.parent(this);

    if(isObject(r) && !(r instanceof Node)) r = Node.owner(r);

    return r;
  }

  get parentElement() {
    const { parentNode } = this;

    return parentNode.nodeType == ELEMENT_NODE ? parentNode : null;
  }

  get ownerDocument() {
    let node = this;

    while(node && node.nodeType != DOCUMENT_NODE) node = node.parentNode;

    return node;
  }

  get childNodes() {
    const raw = Node.raw(this);

    return Factory.for(this).NodeList.cache((raw.children ??= []), this, NodeList);
  }

  get firstChild() {
    const { children } = Node.raw(this);

    if(children?.length) {
      const [first] = children;

      return GetNode(first, this);
    }
  }

  get lastChild() {
    const { children } = Node.raw(this);

    if(children?.length) {
      const last = children[children.length - 1];

      return GetNode(last, this);
    }
  }

  get nextSibling() {
    const { parentNode } = this;
    const { children } = Node.raw(parentNode);

    if(children) {
      const r = Node.raw(this);

      const i = children.indexOf(r);
      const o = Node.owner(this) ?? Node.o(r) ?? parentNode;

      //console.log('nextSibling', console.config({ depth: 2, compact: false }), { children, r, i, o });

      if(i != -1 && children[i + 1]) return GetNode(children[i + 1], o);
    }
  }

  get previousSibling() {
    const { parentNode } = this;
    const { children } = Node.raw(parentNode);

    if(children) {
      const r = Node.raw(this);

      const i = children.indexOf(r);
      const o = Node.owner(this) ?? Node.owner(r) ?? parentNode;

      if(i != -1 && children[i - 1]) return GetNode(children[i - 1], o);
    }
  }

  appendChild(node) {
    const { children } = Node.raw(this);
    const o = Node.raw(node);

    if(node instanceof Text) {
      const k = children.length;

      textValues(
        this,
        modifier(
          () => children[k] ?? '',
          v => (children[k] = v)
        )
      );
    }

    const { parentNode, ownerElement } = this;

    ownerElements.set(node, this.childNodes);
    parentNodes.set(node, this);

    children.push(o);

    return node;
  }

  insertBefore(node, refNode) {
    const { children } = Node.raw(this);

    let i,
      o = isObject(node) && node instanceof Node ? Node.raw(node) : node,
      r = isObject(refNode) && refNode instanceof Node ? Node.raw(refNode) : refNode;

    if((i = children.indexOf(r)) == -1) i = children.length;

    children.splice(i, 0, o);
    return node;
  }

  removeChild(node) {
    const { children } = Node.raw(this);
    let i,
      o = isObject(node) && node instanceof Node ? Node.raw(node) : node;
    if((i = children.indexOf(o)) == -1) throw new Error(`Node.removeChild no such child!`);
    children.splice(i, 1);
    return node;
  }

  replaceChild(newChild, oldChild) {
    const { children } = Node.raw(this);

    let i,
      o = isObject(oldChild) && oldChild instanceof Node ? Node.raw(oldChild) : oldChild,
      r = isObject(newChild) && newChild instanceof Node ? Node.raw(newChild) : newChild;

    if((i = children.indexOf(o)) == -1) throw new Error(`Node.replaceChild no such child!`);

    children.splice(i, 1, r);
    return newChild;
  }

  querySelector(...selectors) {
    let o = Node.raw(this);

    if(typeof selectors[0] == 'string') selectors = [...parseSelectors(...selectors)];
    //console.log('selectors', console.config({ depth: Infinity, compact: 0 }), selectors);
    let gen = query(o, selectors);
    let { value, done } = gen.next();

    if(value) return applyPath(value, this);
  }

  querySelectorAll(...selectors) {
    let o = Node.raw(this);

    if(typeof selectors[0] == 'string') selectors = [...parseSelectors(...selectors)];

    //console.log('selectors', (globalThis.selectors = selectors));

    return query(o, selectors, p => applyPath(p, this));
  }
}

export class Node {
  constructor(obj, parent) {
    //  if(isObject(proto)) assert(Object.getPrototypeOf(this) instanceof Node, true, 'proto');
    //assert(this instanceof Node, true, 'instanceof Node');

    //if(!isObject(obj)) console.log('Node.constructor', { obj, parent, proto });
    if(isObject(obj)) rawNode.set(this, obj);

    parentNodes.set(this, parent);
    //parentNodes.set(obj, parent);
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Node.prototype, Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  get path() {
    return Node.path(this);
  }

  [inspectSymbol]() {
    return `\x1b[1;31m${this.constructor.name}\x1b[0m`;
  }
}

Node.types = nodeTypes;
const NODE_TYPES = properties(
  {
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
  },
  { enumerable: false }
);

define(Node.prototype, Interface.prototype, NODE_TYPES, { [Symbol.toStringTag]: 'Node' });
define(Node, {
  raw(node) {
    checkNode(node);

    if(isObject(node) && Object.getPrototypeOf(node) == Object.prototype) return node;

    return rawNode.get(node) ?? undefined;
  },

  children(node) {
    return Node.raw(node)?.children;
  },

  owner(node) {
    checkNode(node);
    let owner, parent;
    if((parent = parentNodes.get(node)) && Array.isArray(parent.children)) return parent.children;

    let raw = Node.raw(node);
    if((parent = parentNodes.get(raw)) && Array.isArray(parent.children)) return parent.children;

    if((owner = ownerElements.get(node))) return owner;
    return ownerElements.get(raw);
  },

  parent(node) {
    checkNode(node);
    let ret = parentNodes.get(node);
    return ret;
  },

  hier(node, pred = node => true) {
    const r = [];
    checkNode(node);
    let next;
    do {
      if(pred(node)) r.unshift(node);
      next = Node.owner(node) ?? Node.parent(node) /* ?? (
        { NodeList: Node.owner, NamedNodeMap: Node.owner, Element: Node.parent, Text: Node.owner }[
          node.constructor.name
        ] ?? Node.parent
      ).call(Node, node)*/;
      if(r.indexOf(next) != -1) throw new Error(`circular loop`);
    } while(next && (node = next));
    return r;
  },

  document(node) {
    const hier = Node.hier(node);
    return hier.find(({ nodeType }) => nodeType == DOCUMENT_NODE);
  },

  path(arg, path) {
    if(arg.ownerElement) {
      let child = {
        NodeList: ['children'],
        NamedNodeMap: ['attributes'],
        Attr: ['attributes', arg.name]
      }[arg.constructor.name];
      if(child.reduce((acc, key) => acc[key], arg.ownerElement)) return Node.path(arg.ownerElement).concat(child);
    }
    const hier = Node.hier(arg);

    if(!Array.isArray(path)) path = [];

    while(hier.length >= 2) {
      let index = keyOf(Node.raw(hier[0]), Node.raw(hier[1]));
      if(index == -1) index = keyOf(hier[0], hier[1]);

      if(!isNaN(+index)) index = +index;
      path.push(index);
      hier.shift();
    }
    return path;
  },

  from(obj) {}
});

function MakeCache(ctor, store = new WeakMap()) {
  let [get, set] = getset(store);
  return (k, ...args) => {
    let v;
    if(!(v = get(k))) {
      v = ctor(k, ...args);
      set(k, v);
    }
    ownerElements.set(v, args[0]);
    return v;
  };
}

function MakeCache2(ctor, store = new WeakMap()) {
  let mkch = memoize(key => [], store);

  return (id, owner) => {
    // console.log('Cache2',{id,owner });
    if(!isNaN(+id)) id = +id;
    let textList = mkch(owner);
    textList[id] ??= ctor(id, owner);
    // console.log('Cache2',{ textList });
    return textList[id];
  };
}

export class NodeList {
  constructor(obj, owner) {
    let nodeList;
    const isElement = prop => typeof prop == 'string' && !isNaN(+prop);
    const isList = prop => isElement(prop) || prop == 'length';

    const factory = Factory.for(owner);
    /*const wrapElement = (value, prop) =>
      typeof value == 'string' ? Text.cache(prop, nodeList) : Element.cache(value, nodeList);*/

    rawNode.set(this, obj);
    //parentNodes.set(obj, owner);
    ownerElements.set(this, owner);

    nodeList = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop === Symbol.iterator) return NodeList.prototype[prop].bind(this);
        if(isList(prop)) {
          if(prop == 'length') return obj.length;
          if(prop >= 0 && prop < obj.length) return GetNode(obj[prop], nodeList, factory); // wrapElement(obj[prop], prop);
        }
        return Reflect.get(target, prop, receiver);
      },
      getOwnPropertyDescriptor: (target, prop) => {
        if(isList(prop)) {
          if(prop == 'length' || (prop >= 0 && prop < obj.length)) return { configurable: true, enumerable: true, value: obj[prop] };
        }
        return Reflect.getOwnPropertyDescriptor(target, prop);
      },
      ownKeys: target =>
        range(0, obj.length - 1)
          .map(prop => prop + '')
          .concat(['length']),
      getPrototypeOf: target => NodeList.prototype
    });

    rawNode.set(nodeList, obj);
    ownerElements.set(nodeList, owner);

    return nodeList;
  }

  [inspectSymbol](depth, opts) {
    let str = inspect([...this], depth + 1, { ...opts, customInspect: true });
    let indexes = str[0] == '[' ? [str.indexOf('[') + 1, str.lastIndexOf(']')] : [0];
    return str.substring(...indexes);
  }
}

//NodeList.prototype.__proto__ = Array.prototype;

define(NodeList.prototype, {
  [Symbol.toStringTag]: 'NodeList',
  /*get path() {
    let owner = Node.owner(this);
    return Node.path(owner).concat(['children']);
  },*/

  *[Symbol.iterator]() {
    const raw = Node.raw(this);
    const fac = Factory.for(this);
    for(let node of raw) {
      let fn = fac(typeof node == 'string' ? Entities.Text : Entities.Element);
      yield fn(node, this);
    }
  }
});

export function NamedNodeMap(obj, owner) {
  if(!this) return new NamedNodeMap(obj, owner);

  let nodeMap;
  const isAttr = prop => typeof prop == 'string' && prop in obj;
  const wrapAttr = (value, prop) => new Attr([obj, prop], nodeMap);

  rawNode.set(this, obj);
  //parentNodes.set(obj,owner);
  ownerElements.set(this, owner);

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
  ownerElements.set(nodeMap, owner);

  return nodeMap;
}

Object.setPrototypeOf(NamedNodeMap.prototype, Array.prototype);

define(NamedNodeMap.prototype, {
  constructor: NamedNodeMap,
  get path() {
    let owner = Node.owner(this);
    return Node.path(owner).concat(['attributes']);
  },

  item(key) {
    return this[key];
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
/*
 Element methods:
     after
    animate
    append
    attachShadow
    before
    closest
    computedStyleMap
    createShadowRoot
    getAnimations
    getAttribute
    getAttributeNames
    getAttributeNode
    getAttributeNodeNS
    getAttributeNS
    getBoundingClientRect
    getClientRects
    getElementsByClassName
    getElementsByTagName
    getElementsByTagNameNS
    hasAttribute
    hasAttributeNS
    hasAttributes
    hasPointerCapture
    insertAdjacentElement
    insertAdjacentHTML
    insertAdjacentText
    matches
    msZoomTo
    prepend
    querySelector
    querySelectorAll
    releasePointerCapture
    remove
    removeAttribute
    removeAttributeNode
    removeAttributeNS
    replaceChildren
    replaceWith
    requestFullscreen
    requestPointerLock
    scroll
    scrollBy
    scrollIntoView
    scrollIntoViewIfNeeded
    scrollTo
    setAttribute
    setAttributeNode
    setAttributeNodeNS
    setAttributeNS
    setCapture
    setHTML
    setPointerCapture
Element properties:
    assignedSlotRead
    attributes
    childElementCount
    children
    classList
    className
    clientHeight
    clientLeft
    clientTop
    clientWidth
    firstElementChild
    id
    innerHTML
    lastElementChild
    localName
    namespaceURI
    nextElementSibling
    outerHTML
    openOrClosedShadowRoot
    part
    prefix
    previousElementSibling
    scrollHeight
    scrollLeft
    scrollLeftMax
    scrollTop
    scrollTopMax
    scrollWidth
    shadowRootRead
    slot

 */
export class Element extends Node {
  constructor(obj, parent) {
    super(obj, parent);

    /* lazyProperty(this, 'classList', () => new TokenList('class', this));
    define(this, { classList: new TokenList(this, 'class') });*/
    lazyProperties(this, { classList: () => new TokenList(this, 'class') });
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  get parentElement() {
    let obj = this;
    do {
      obj = obj.parentNode;
    } while(obj.nodeType != ELEMENT_NODE);
    return obj;
  }

  get attributes() {
    let factory = Factory.for(this.ownerDocument);
    if(!factory) console.log(this.constructor.name, 'No factory', this);
    return factory.NamedNodeMap.cache((Node.raw(this).attributes ??= {}), this);
  }

  /*  set tagName(v) {
    Node.raw(this).tagName = v;
  }
  get tagName() {
    return Node.raw(this).tagName;
  }*/
  set nodeName(v) {
    this.tagName = v;
  }
  get nodeName() {
    return this.tagName;
  }

  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    let s = `<${tagName}`;
    const a = attributes[inspectSymbol](depth + 1, opts)
      .slice(1, -1)
      .trim();
    let c;
    try {
      c = children[inspectSymbol](depth + 1, opts);
    } catch(err) {
      c = '';
    }
    if(a) s += ' ' + a;
    s += '>';
    if(c) s += `${c}</${tagName}>`;
    //console.log(this.constructor.name + '.inspect', s);
    return s;
  }

  get children() {
    const raw = Node.raw(this);

    // console.log('raw',inspect(raw, {depth: 0}));
    let factory = Factory.for(this.ownerDocument);
    if(!factory) console.log(this.constructor.name, 'No factory', this);
    return factory.NodeList.cache((raw.children ??= []), this);
  }

  get style() {
    const raw = Node.raw(this);

    // console.log('raw',inspect(raw, {depth: 0}));
    let factory = Factory.for(this.ownerDocument);
    if(!factory) console.log(this.constructor.name, 'No factory', this);
    return factory.CSSStyleDeclaration.cache((raw.attributes ??= {}), this);
  }

  get childElementCount() {
    return Node.raw(this).children?.length ?? 0;
  }

  get firstElementChild() {
    const { children } = Node.raw(this);
    let element = children.find(n => isObject(n) && 'tagName' in n);
    if(element) return Element.cache(element, this.children);
  }

  get lastElementChild() {
    const { children } = Node.raw(this);
    if(children?.length) for(let i = children.length - 1; i >= 0; i--) if (isObject(children[i]) && 'tagName' in children[i]) return Element.cache(children[i], this.children);
  }

  get id() {
    return this.getAttribute('id');
  }

  getAttribute(name) {
    return Element.attributes(this)(attributes => attributes[name]);
  }

  getAttributeNames() {
    return Element.attributes(this)(attributes => Object.keys(attributes));
  }

  hasAttribute(name) {
    return Element.attributes(this)(attributes => name in attributes);
  }

  hasAttributes() {
    return this.getAttributeNames().length > 0;
  }

  removeAttribute(name) {
    Element.attributes(this)(attributes => delete attributes[name]);
  }

  getAttributeNode(name) {
    return this.attributes[name];
  }

  setAttribute(name, value) {
    if(!(typeof value == 'string' || (value !== null && value !== undefined && value.toString))) throw new TypeError(`Element.setAttribute(): value not of type 'string': ${value}`);

    value = value + '';

    Element.attributes(this)(attributes => (attributes[name] = value));
  }

  get innerText() {
    let raw = Node.raw(this);
    let texts = [];
    for(let [value, path] of iterate(raw, (n, k) => typeof n == 'string' && k[k.length - 2] == 'children')) {
      texts.push(value.replace(/<s+/g, ' '));
    }
    return texts.join(' ');
  }

  static cache = MakeCache((obj, owner) => new Element(obj, owner));
}

define(Element, {
  attributes(elem) {
    return modifier(Node.raw(elem), 'attributes');
  },
  toString(elem) {
    return elem[inspectSymbol](0, {}).replace(/\x1b\[[^a-z]*[a-z]/g, '');
  }
});

//define(Element, { cache: MakeCache(Element) });
define(Element.prototype, { [Symbol.toStringTag]: 'Element', nodeType: ELEMENT_NODE });

Object.defineProperty(Element.prototype, 'tagName', {
  set(v) {
    Node.raw(this).tagName = v;
  },
  get() {
    return Node.raw(this).tagName;
  },
  enumerable: true
});

/*
  Document methods:
    adoptNode
    append
    captureEvents
    caretRangeFromPoint
    clear
    close
    createAttribute
    createAttributeNS
    createCDATASection
    createComment
    createDocumentFragment
    createElement
    createElementNS
    createEvent
    createExpression
    createNSResolver
    createNodeIterator
    createProcessingInstruction
    createRange
    createTextNode
    createTreeWalker
    elementFromPoint
    elementsFromPoint
    evaluate
    execCommand
    exitFullscreen
    exitPointerLock
    getElementById
    getElementsByClassName
    getElementsByName
    getElementsByTagName
    getElementsByTagNameNS
    getSelection
    hasFocus
    importNode
    open
    prepend
    queryCommandEnabled
    queryCommandIndeterm
    queryCommandState
    queryCommandSupported
    queryCommandValue
    querySelector
    querySelectorAll
    releaseEvents
    replaceChildren
    webkitCancelFullScreen
    webkitExitFullscreen
    write
    writeln
    constructor
    exitPictureInPicture
    getAnimations

  Document properties:
    implementation
    URL
    documentURI
    compatMode
    characterSet
    charset
    inputEncoding
    contentType
    doctype
    documentElement
    xmlEncoding
    xmlVersion
    xmlStandalone
    domain
    referrer
    cookie
    lastModified
    readyState
    title
    dir
    body
    head
    images
    embeds
    plugins
    links
    forms
    scripts
    currentScript
    defaultView
    designMode
    anchors
    applets
    fgColor
    linkColor
    vlinkColor
    alinkColor
    bgColor
    all
    scrollingElement
    hidden
    visibilityState
    wasDiscarded
    featurePolicy
    webkitVisibilityState
    webkitHidden
    fullscreenEnabled
    fullscreen
    webkitIsFullScreen
    webkitCurrentFullScreenElement
    webkitFullscreenEnabled
    webkitFullscreenElement
    rootElement
    children
    firstElementChild
    lastElementChild
    childElementCount
    activeElement
    styleSheets
    pointerLockElement
    fullscreenElement
    adoptedStyleSheets
    fonts
    fragmentDirective
    addressSpace
    timeline
    pictureInPictureEnabled
    pictureInPictureElement
 */

export class Document extends Element {
  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  constructor(obj, factory) {
    super(obj, null, factory);
  }

  createAttribute(name, value) {
    return new Attr([null, name], null);
  }

  createElement(tagName) {
    return Element.cache({ tagName, attributes: {}, children: [] }, null);
  }

  createTextNode(text) {
    return new Text(text);
  }

  createTreeWalker(root, whatToShow = TreeWalker.TYPE_ALL, filter = { acceptNode: node => TreeWalker.FILTER_ACCEPT }, expandEntityReferences = false) {
    const raw = Node.raw(root);
    let walker;
    walker = new TreeWalker(
      raw,
      (n, k) => typeof k == 'number' && pred(n, k),
      (n, p) => GetNode(n, get(raw, p.slice(0, -1)))
    );
    return walker;
  }

  get body() {
    let element = this.lastElementChild.lastElementChild;
    if(/^body$/i.test(element.tagName)) return element;
  }

  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    return `\x1b[1;31mDocument\x1b[0m ${tagName} attributes: ${attributes[inspectSymbol](depth + 1, opts)} children: ${children.reduce((acc, c) => acc + c[inspectSymbol](depth + 2, opts), '')}>`;
  }
}

define(Document.prototype, { [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE });

export class Attr extends Node {
  constructor(raw, owner) {
    // console.log('Attr', { raw, owner });
    super(raw, owner);

    rawNode.set(this, raw);
    ownerElements.set(this, owner);

    define(this, {});
  }

  get path() {
    let { ownerElement } = this;
    let [obj, name] = Node.raw(this);
    //console.log('path',{owner,parent});
    return Node.path(ownerElement).concat(['attributes', name]);
  }

  get ownerElement() {
    return Node.owner(Node.owner(this));
  }
  get ownerDocument() {
    return Node.document(this);
  }

  get name() {
    const [, name] = Node.raw(this);
    return name;
  }
  set name(v) {
    Node.raw(this)[1] = v;
  }

  get value() {
    const [obj, name] = Node.raw(this);
    return obj[name];
  }
  set value(v) {
    const [obj, name] = Node.raw(this);
    obj[name] = v;
  }

  [inspectSymbol]() {
    const [obj, name] = Node.raw(this);
    const value = obj[name];
    return `\x1b[1;35m${name}\x1b[1;34m=${quote(value, '"')}\x1b[0m`;
  }
}

define(Attr.prototype, {
  nodeType: ATTRIBUTE_NODE,
  [Symbol.toStringTag]: 'Attr'
});

export class Text extends Node {
  static store = gettersetter(rawNode);

  constructor(key, owner) {
    super(owner ? key : null, owner);
    let get, set;
    if(owner) {
      let raw = owner instanceof NodeList ? Node.raw(owner) : owner;
      if(key in raw) {
      } else if(raw.indexOf(key) != -1) {
        key = raw.indexOf(key);
      }
      get = () => raw[key] ?? '';
      set = value => (raw[key] = value);
    } else {
      Text.store(this, key);
      get = () => Text.store(this);
      set = value => Text.store(this, value);
    }
    textValues(this, modifier(get, set));
    //console.log('Text', get());
  }

  get data() {
    return textValues(this)(value => value);
  }

  get nodeValue() {
    return textValues(this)(value => value);
  }

  toString() {
    return this.data;
  }

  [inspectSymbol](depth, opts) {
    const { data } = this;
    return `\x1b[1;31m${this[Symbol.toStringTag]}\x1b[0m \x1b[38;2;192;2550m${quote(data, "'")}\x1b[0m`;
  }

  static cache = MakeCache2((key, owner) => new Text(key, owner));
}

Text.prototype.__proto__ = Node.prototype;
define(Text.prototype, {
  nodeType: TEXT_NODE,
  nodeName: '#text',
  [Symbol.toStringTag]: 'Text'
});

const Tag = gettersetter(new WeakMap());

export class Comment extends Node {
  constructor(raw, owner) {
    super(raw, owner);

    ownerElements.set(this, owner);

    //console.log('Comment.constructor raw', raw);

    let get = () => raw.tagName ?? '!----';
    let set = value => (raw.tagName = value);

    Tag(
      this,
      modifier(
        () => get().replace(/^!--(.*)--$/g, '$1'),
        value => set(`!--${value}--`)
      )
    );
  }

  get data() {
    return Tag(this)(value => '<!--' + value + '-->');
  }

  get nodeValue() {
    return Tag(this)(value => value);
  }

  [inspectSymbol](depth, opts) {
    const { data } = this;
    return `Comment \x1b[38;2;184;0;234m${data}\x1b[0m`;
  }

  static cache = MakeCache2((node, owner) => new Comment(node, owner));
}

Comment.prototype.__proto__ = Node.prototype;
define(Comment.prototype, {
  nodeType: COMMENT_NODE,
  nodeName: '#comment',
  [Symbol.toStringTag]: 'Comment'
});

const Tokens = gettersetter(new WeakMap());

export class TokenList {
  constructor(owner, key = 'class') {
    ownerElements.set(this, owner);

    let { attributes } = Node.raw(owner);

    let get = () => attributes[key] ?? '';
    let set = value => (attributes[key] = value);

    Tokens(
      this,
      modifier(
        () => get().trim().split(/\s+/g),
        value => set(value.join(' '))
      )
    );
  }

  get length() {
    return Tokens(this)(value => value.length);
  }

  get value() {
    return Tokens(this)(value => value.join(' '));
  }

  item(index) {
    return Tokens(this)(value => value[index]);
  }

  contains(token) {
    return Tokens(this)(value => value.indexOf(token) != -1);
  }

  add(...tokens) {
    Tokens(this)((arr, set) => {
      let i;
      for(let token of tokens) {
        if((i = arr.indexOf(token)) == -1) arr.push(token);
      }
      set(arr);
    });
  }

  remove(...tokens) {
    Tokens(this)((arr, set) => {
      let i;
      for(let token of tokens) {
        while((i = arr.indexOf(token)) != -1) arr.splice(i, 1);
      }
      set(arr);
    });
  }

  toggle(token, force) {
    Tokens(this)((arr, set) => {
      let i;
      if((i = arr.indexOf(token)) == -1) arr.push(token);
      else arr.splice(i, 1);
      set(arr);
    });
  }

  supports(token) {
    return true;
  }

  replace(oldToken, newToken) {
    Tokens(this)((arr, set) => {
      let i;
      if((i = arr.indexOf(oldToken)) != -1) arr.splice(i, 1, newToken);
      set(arr);
    });
  }

  [inspectSymbol](depth, opts) {
    return 'TokenList [' + Tokens(this)().join(',') + ']';
  }
}

define(TokenList.prototype, {
  [Symbol.toStringTag]: 'TokenList'
});
arrayFacade(TokenList.prototype, (container, i) => container.item(i));

const cssStyleMap = gettersetter(new WeakMap());

export class CSSStyleDeclaration {
  constructor(attrObj, owner) {
    let cssStyle;
    const factory = Factory.for(owner);

    rawNode.set(this, attrObj);
    ownerElements.set(this, owner);

    let entries = getEntries();

    let internal = {
      entries,
      obj: getObj(entries),
      set(key, value) {
        this.obj[key] = value;
        setEntries((this.entries = Object.entries(this.obj)));
      },
      remove(key) {
        if(key in this.obj) {
          delete this.obj[key];
          setEntries((this.entries = Object.entries(this.obj)));
        }
      }
    };

    cssStyle = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop === 'constructor') return CSSStyleDeclaration;
        if(typeof prop == 'string') {
          if(prop == 'length') return internal.entries.length;
          if(!/-/.test(prop)) {
            let dec = /[a-z][A-Z]/.test(prop) ? decamelize(prop) : prop;
            if(dec in internal.obj) return internal.obj[dec];
          }
        }
        /*  if(prop in CSSStyleDeclaration.prototype) return CSSStyleDeclaration.prototype[prop];*/

        return Reflect.get(target, prop, receiver);
      },
      set: (target, prop, value) => {
        if(typeof prop == 'string') {
          if(prop == 'length') return;
          if(!/-/.test(prop)) {
            let dec = /[a-z][A-Z]/.test(prop) ? decamelize(prop) : prop;
            internal.set(dec, value);
          }
        }
        return Reflect.set(target, prop, value);
      },
      deleteProperty: (target, prop) => {
        if(typeof prop == 'string') {
          if(!/-/.test(prop)) {
            let dec = /[a-z][A-Z]/.test(prop) ? decamelize(prop) : prop;
            internal.remove(dec);
          }
        }
        return Reflect.deleteProperty(target, prop);
      },
      ownKeys: target => internal.entries.map(([k]) => camelize(k))
      /*,
      getOwnPropertyDescriptor: (target, prop) => {
        return Reflect.getOwnPropertyDescriptor(target, prop);
      }*/
    });

    cssStyleMap(cssStyle, internal);

    rawNode.set(cssStyle, attrObj);
    ownerElements.set(cssStyle, owner);

    function setStyle(str) {
      attrObj.style = str;
    }
    function getStyle() {
      return attrObj.style;
    }
    function getEntries() {
      let a = getStyle()
        .split(/\s*;\s*/g)
        .filter(item => /:/.test(item));
      return a.map(item => item.split(/\s*:\s*/));
    }
    function setEntries(ent) {
      let str = ent.map(([k, v]) => `${k}:${v};`).join('');
      setStyle(str);
      return str;
    }

    function getObj(ent = internal.entries) {
      return Object.fromEntries(ent);
    }

    return cssStyle;
  }

  /* get cssText() {
     const internal = cssStyleMap(this);
    return internal.entries.map(([k, v]) => `${k}:${v};`).join('');
  }*/

  setProperty(k, v) {
    const internal = cssStyleMap(this);
    internal.set(k, v);
  }

  item(index) {
    const internal = cssStyleMap(this);
    return internal.entries[index][0];
  }

  getPropertyValue(k) {
    const internal = cssStyleMap(this);
    return internal.obj[k];
  }

  removeProperty(k) {
    const internal = cssStyleMap(this);
    internal.remove(k);
  }
}

define(CSSStyleDeclaration.prototype, {
  constructor: CSSStyleDeclaration,
  [Symbol.toStringTag]: 'CSSStyleDeclaration',
  get cssText() {
    let { style } = Node.raw(this);
    return style;
  },

  [inspectSymbol](depth, opts) {
    let str = inspect(this, depth + 1, { ...opts, customInspect: false });
    str = str.replace(/\[Object.*/g, '');
    return str + this.cssText;
  }
});

export class Serializer {
  serializeToString(node) {
    const raw = Node.raw(node);
    //  console.log('Serializer.serializeToString', { node, raw });
    return writeXML(raw);
  }
}

function Membrane(instance, obj, proto, wrapProp, wrapElement) {
  throw new Error('Membrane');
  return new Proxy(instance, {
    get: (target, prop, receiver) => (wrapProp(prop) ? wrapElement(obj[prop], prop) : Reflect.get(target, prop, receiver)),
    has: (target, prop) => (wrapProp(prop) ? true : Reflect.has(target, prop)),
    getOwnPropertyDescriptor: (target, prop) =>
      wrapProp(prop)
        ? {
            configurable: true,
            enumerable: true,
            writable: true,
            value: wrapElement(obj[prop], prop)
          }
        : Reflect.getOwnPropertyDescriptor(target, prop),
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
        ret instanceof Node ? parentNodes.set(raw, owner) : ownerNode.set(raw, owner);
        return ret;
      }
      return memo(raw, owner, ctor);
    },
    { memo }
  );
}*/

/*let map = new WeakMap();

export function Factory(raw, owner, ctor) {
  let obj;
  if(isObject(raw) && (obj = map.get(raw))) {
    ownerElements.set(obj, owner);
    return obj;
  }

  const isArray = arg => Array.isArray(arg);
  const isAttributes = arg => isObject(arg) && !isArray(arg) && !('tagName' in arg);

  ctor ??= isArray(raw) ? NodeList : isAttributes(raw) ? NamedNodeMap : typeof raw == 'string' ? Text : Element;

  obj = new ctor(raw, owner);
  return obj;
}*/

export function GetType(raw, owner, ctor) {
  const isArray = arg => Array.isArray(arg);
  const isAttributes = arg => isObject(arg) && !isArray(arg) && !('tagName' in arg);

  if(isArray(raw)) return Entities.NodeList;
  if('tagName' in raw) return /^!--.*--$/.test(raw.tagName) ? Entities.Comment : Entities.Element;
  if(typeof raw == 'string') return Entities.Text;
  if(typeof raw == 'object') return Entities.NamedNodeMap;
}
