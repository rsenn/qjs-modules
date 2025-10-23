import { readFileSync } from 'fs';
import { className, nonenumerable, extend, arrayFacade, assert, camelize, decamelize, define, mapObject, getset, getter, gettersetter, isBool, isObject, isFunction, isNumeric, isNumber, isString, lazyProperties, memoize, modifier, quote, range, properties, isPrototypeOf, } from 'util';
import { parseSelectors } from './css3-selectors.js';
import { get, iterate, find, clone, RETURN_VALUE_PATH, RETURN_PATH, RETURN_VALUE, TYPE_STRING, TYPE_OBJECT } from 'deep';
import { TreeWalker } from 'tree_walker';
import { read as readXML, write as writeXML } from 'xml';

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

const rawNode = gettersetter(new WeakMap());
const parentNodes = gettersetter(new WeakMap());
const ownerElements = gettersetter(new WeakMap());
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
  'createNotation',
];

const EntityNames = ['Document', 'Node', 'NodeList', 'Element', 'NamedNodeMap', 'Attr', 'Text', 'Comment', 'TokenList', 'CSSStyleDeclaration', 'HTMLCollection'];
const EntityType = name => EntityNames.indexOf(name);
const TypeName = n => (isNumber(n) ? EntityNames[n] : n);

export const Entities = EntityNames.reduce((obj, name, id) => ({ [name]: id, ...obj }), {});

class DereferenceError extends Error {
  constructor(obj, i, path, error) {
    if(typeof i == 'symbol') i = '<symbol>';
    super(`dereference error of <${obj}> at ${i} '${path.slice(0, i)}': ${error.message}`);
  }
}

function applyPath(path, obj) {
  const { length } = path;
  let raw = rawNode(obj);

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
        rawNode(obj, raw);
      } catch(error) {
        raw = undefined;
      }
  }

  return obj;
}

function query(root, selectors, t = (path, root) => path) {
  let path;
  for(const selector of selectors) if((path = find(root, selector, RETURN_PATH, TYPE_OBJECT, ['children']))) return t(path, root);
}

function* queryAll(root, selectors, t = (path, root) => path) {
  for(const selector of selectors) for (const path of iterate(root, selector, RETURN_PATH, TYPE_OBJECT, ['children'])) yield t(path, root);
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
  'NOTATION_NODE',
];

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
    CSSStyleDeclaration,
    HTMLCollection,
  },
) {
  const prototypes = {};
  for(const key in constructors) prototypes[key] = constructors[key].prototype;
  return prototypes;
}

const factories = gettersetter(new WeakMap());

export function Factory(obj = Prototypes()) {
  if(Array.isArray(obj)) obj = obj.reduce((acc, proto, i) => ({ [EntityNames[i]]: proto, ...acc }), {});

  const GetProto = type => ((type = TypeName(type)), obj[type] ?? Prototypes()[type]);
  const fn = type => fn[TypeName(type)].new;
  fn.entities = gettersetter(new WeakMap());

  let create = obj;

  if(!isFunction(create)) create = (type, ...args) => new (GetProto(type).constructor)(...args);

  let cr = create;

  create = (type, ...args) => {
    const obj = cr(type, ...args);
    fn.entities(obj, { type, args });
    return obj;
  };

  for(let i = 0; i < EntityNames.length; i++) {
    const name = EntityNames[i];
    const proto = GetProto(name);

    fn[name] = {
      new: (...args) => create(name, ...args),
      cache(...args) {
        proto.constructor.cache ??= MakeCache((...a) => this.new(...a));
        return proto.constructor.cache(...args);
      },
    };

    (fn.classes ??= Object.create(null))[name] = proto.constructor;
  }

  return Object.setPrototypeOf(fn, Factory.prototype);
}

Object.setPrototypeOf(Factory.prototype, Function.prototype);

define(Factory, {
  type(node) {
    const fn = this.get(node);
    return fn.entities(node)?.type;
  },
  for(node) {
    const fn = this.get(node);
    if(!fn) throw new Error(`No fn for <${className(node)}> [[ ${Element.toString(node)} ]]`);
    return fn;
  },
  get: node => factories(Node.document(node)),
  set: (node, factory) => factories(Node.document(node), factory),
});

const parsers = gettersetter(new WeakMap());

export class Parser {
  constructor(factory = new Factory()) {
    define(this, nonenumerable({ factory }));
  }

  parseFromString(str, file) {
    let data = readXML(str, file);

    if(Array.isArray(data)) {
      if(data[0].tagName != '?xml')
        data = {
          tagName: '?xml',
          attributes: { version: '1.0', encoding: 'utf-8' },
          children: data,
        };
      else if(data.length == 1) data = data[0];
    }

    const { factory } = this;
    const doc = factory['Document'].new(data, factory);

    Factory.set(doc, factory);

    parsers(doc, this);

    return doc;
  }

  parseFromFile(file) {
    const { factory } = this;
    return this.parseFromString(readFileSync(file), file, factory);
  }

  static for(node) {
    return parsers(Node.document(node));
  }
}

define(Parser.prototype, nonenumerable({ [Symbol.toStringTag]: 'Parser' }));

function GetNode(obj, owner, factory) {
  const type = GetType(obj, owner);
  if(type == Entities.Text && isString(obj) && isFunction(owner.indexOf)) obj = owner.indexOf(obj);
  factory ??= Factory.for(owner);
  const ctor = factory(type);
  if(!ctor) throw new Error(`No such node type for ${obj[Symbol.inspect]()}`);
  if(ctor.cache) return ctor.cache(obj, owner);
  return ctor(obj, owner);
}

export class Interface {
  get textContent() {
    const texts = [];
    for(const value of iterate(Node.raw(this), undefined, RETURN_VALUE, TYPE_STRING, ['children'])) {
      texts.push(value.replace(/<s+/g, ' '));
    }
    return texts.join(' ');
  }

  get isConnected() {
    return isObject(Node.ownerOrParent(this));
  }

  get nodeName() {
    switch (this.nodeType) {
      case DOCUMENT_NODE:
        return '#document';
      case TEXT_NODE:
        return '#text';
      case ELEMENT_NODE:
        return this.tagName.toUpperCase();
    }
  }

  get nodeValue() {
    switch (this.nodeType) {
      case TEXT_NODE:
        return this.textContent;
      default:
        return null;
    }
  }

  get parentNode() {
    let result = Node.ownerOrParent(this);
    if(!isNode(result)) result = Node.ownerOrParent(result);
    return result;
  }

  get parentElement() {
    const { parentNode } = this;
    return parentNode.nodeType == ELEMENT_NODE ? parentNode : null;
  }

  isSameNode(other) {
    if(Node.raw(other) == Node.raw(this)) return true;
    if(Node.document(other) == Node.document(this)) {
      if(Node.path(other) + '' == Node.path(this) + '') return true;
    }
  }

  hasChildNodes() {
    const children = Node.children(this);
    return children && children.length > 0;
  }

  getRootNode() {
    for(let parent, node = Node.ownerOrParent(this); node; node = parent) {
      if(!(parent = Node.ownerOrParent(node))) return node;
      node = parent;
    }
  }

  get ownerDocument() {
    return Node.document(this);
  }

  get childNodes() {
    const raw = Node.raw(this);
    return Factory.for(this).NodeList.cache((raw.children ??= []), this, NodeList);
  }

  get firstChild() {
    if(this.hasChildNodes()) {
      const [first] = Node.children(this);
      return GetNode(first, this);
    }
  }

  get lastChild() {
    if(this.hasChildNodes()) {
      const children = Node.children(this);
      const last = children[children.length - 1];
      return GetNode(last, this);
    }
  }

  get nextSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const children = Node.children(parentNode);
      const raw = Node.raw(this);
      const index = children.indexOf(raw);
      const owner = Node.ownerOrParent(this) ?? parentNode;

      if(index != -1 && children[index + 1]) return GetNode(children[index + 1], owner);
    }
  }

  get previousSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const children = Node.children(parentNode);
      const raw = Node.raw(this);
      const index = children.indexOf(raw);
      const owner = Node.ownerOrParent(this) ?? Node.ownerOrParent(raw) ?? parentNode;

      if(index != -1 && children[index - 1]) return GetNode(children[index - 1], owner);
    }
  }

  cloneNode(deep = true) {
    const obj = clone(Node.raw(this));
    const factory = Factory.for(this);
    return factory(Factory.type(this))(obj, null);
  }

  appendChild(node) {
    const { children } = Node.raw(this);

    if(node instanceof Text) {
      const k = children.length;

      textValues(
        this,
        modifier(
          () => children[k] ?? '',
          v => (children[k] = v),
        ),
      );
    }

    children.push(Node.raw(node));

    parentNodes(node, this.childNodes);
    ownerElements(node, this);

    return node;
  }

  insertBefore(node, ref) {
    const { children } = Node.raw(this);
    const old = isNode(node) ? Node.raw(node) : node,
      before = isNode(ref) ? Node.raw(ref) : ref;
    const index = children.indexOf(before);

    if(index == -1) index = children.length;

    children.splice(index, 0, old);

    parentNodes(node, this.childNodes);
    ownerElements(node, this);

    return node;
  }

  removeChild(node) {
    const { children } = Node.raw(this);
    const old = isNode(node) ? Node.raw(node) : node;
    const index = children.indexOf(old);

    if(index == -1) throw new Error(`Node.removeChild no such child!`);

    children.splice(index, 1);

    setParentOwner(node, null);

    return node;
  }

  replaceChild(newChild, oldChild) {
    const { children } = Node.raw(this);
    const old = isNode(oldChild) ? Node.raw(oldChild) : oldChild,
      replacement = isNode(newChild) ? Node.raw(newChild) : newChild;
    const index = children.indexOf(old);

    if(index == -1) throw new Error(`Node.replaceChild no such child!`);

    children.splice(index, 1, replacement);

    setParentOwner(old, null);
    parentNodes(replacement, this.childNodes);
    ownerElements(replacement, this);

    return newChild;
  }

  querySelector(...selectors) {
    if(isString(selectors[0])) selectors = [...parseSelectors(...selectors)];

    let path;
    if((path = query(Node.raw(this), selectors))) return applyPath(path, this);
  }

  querySelectorAll(...selectors) {
    if(isString(selectors[0])) selectors = [...parseSelectors(...selectors)];

    return queryAll((globalThis.raw = Node.raw(this)), (globalThis.selectors = selectors), p => applyPath(p, this));
  }

  *getElementsByTagName(name) {
    for(const [value, path] of iterate(Node.raw(this), name == '*' ? e => true : e => e.tagName == name, RETURN_VALUE_PATH, TYPE_OBJECT, ['children'])) yield applyPath(path, this);
  }
}

export class Node {
  constructor(obj, parent) {
    if(isObject(obj)) rawNode(this, obj);

    setParentOwner(this, parent);
  }

  [Symbol.inspect]() {
    return `\x1b[1;31m${this.constructor.name}\x1b[0m`;
  }

  static check(node) {
    if(!isObject(node)) throw new TypeError('node is not an object');
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Node.prototype, Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  static raw(node, raw) {
    this.check(node);

    if(raw === undefined) {
      if(isObject(node) && Object.getPrototypeOf(node) == Object.prototype) return node;
      return rawNode(node) ?? undefined;
    }

    return rawNode(node, raw);
  }

  static children(node) {
    return Node.raw(node)?.children;
  }

  static ownerOrParent(node) {
    this.check(node);

    let owner;
    if((owner = ownerElements(node))) return owner;
    if((owner = parentNodes(node))) return owner;
  }

  static document(node) {
    while(node) {
      if(node.nodeType == Node.DOCUMENT_NODE) return node;
      node = Node.ownerOrParent(node);
    }
  }

  static *up(node) {
    this.check(node);

    let next;
    do {
      yield node;
      next = Node.ownerOrParent(node);
    } while(next && (node = next));
  }

  static hier(node, pred = node => true, forward = false) {
    const ret = [],
      method = ret[forward ? 'push' : 'unshift'];

    for(const n of this.up(node)) {
      if(!pred(n)) continue;
      if(ret.indexOf(n) != -1) throw new Error(`circular loop`);
      method.call(ret, n);
    }

    return ret;
  }

  static document(node) {
    const hier = Node.hier(node);
    return hier.find(({ nodeType }) => nodeType == DOCUMENT_NODE);
  }

  static path(arg, path) {
    if(arg.ownerElement) {
      const child = {
        NodeList: ['children'],
        NamedNodeMap: ['attributes'],
        Attr: ['attributes', arg.name],
      }[arg.constructor.name];

      if(child.reduce((acc, key) => acc[key], arg.ownerElement)) return Node.path(arg.ownerElement).concat(child);
    }

    const hier = Node.hier(arg);

    if(!Array.isArray(path)) path = [];

    while(hier.length >= 2) {
      let index = keyOf(Node.raw(hier[0]), Node.raw(hier[1]));

      if(index == -1) index = keyOf(hier[0], hier[1]);
      if(isNumeric(index)) index = +index;

      path.push(index);
      hier.shift();
    }

    return define(
      path,
      nonenumerable({
        toString() {
          return this.join('.');
        },
      }),
    );
  }
}

Node.types = nodeTypes;

export const NODE_TYPES = {
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
  TEXT_NODE,
};

define(Node.prototype, Interface.prototype);
define(Node.prototype, nonenumerable(NODE_TYPES));
define(Node.prototype, nonenumerable({ [Symbol.toStringTag]: 'Node' }));

export class NodeList {
  constructor(obj, owner) {
    const isIndex = prop => isString(prop) && isNumeric(prop);

    rawNode(this, obj);
    setParentOwner(this, owner);

    const nodeList = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop === Symbol.iterator && isFunction(NodeList.prototype[prop])) return NodeList.prototype[prop].bind(this);

        if(prop == 'length') return obj.length;
        if(isIndex(prop) && prop >= 0 && prop < obj.length) return GetNode(obj[prop], nodeList, Factory.for(owner));

        return Reflect.get(target, prop, receiver);
      },
      getOwnPropertyDescriptor: (target, prop) => {
        if(prop == 'length' || (prop >= 0 && prop < obj.length)) return { value: obj[prop], configurable: true, enumerable: true };

        return Reflect.getOwnPropertyDescriptor(target, prop);
      },
      ownKeys: () =>
        range(0, obj.length - 1)
          .map(prop => prop + '')
          .concat(['length']),
      //getPrototypeOf: () => NodeList.prototype,
    });

    rawNode(nodeList, obj);
    setParentOwner(nodeList, owner);

    return nodeList;
  }
}

define(
  NodeList.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'NodeList',
    *[Symbol.iterator]() {
      const factory = Factory.for(this);

      for(const node of Node.raw(this)) yield factory(isString(node) ? Entities.Text : Entities.Element)(node, this);
    },
  }),
);

export class HTMLCollection {
  constructor(obj, owner, pred = e => true) {
    const isIndex = prop => isString(prop) && isNumeric(prop);

    const arr = () => obj.filter(pred);

    rawNode(this, obj);
    setParentOwner(this, owner);

    const coll = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop === Symbol.iterator && isFunction(HTMLCollection.prototype[prop])) return HTMLCollection.prototype[prop].bind(this);

        if(prop == 'length' || isIndex(prop)) return GetNode(arr()[prop], coll, Factory.for(owner));

        return Reflect.get(target, prop, receiver);
      },
      getOwnPropertyDescriptor: (target, prop) => {
        if(prop == 'length' || isIndex(prop)) return { value: arr()[prop], configurable: true, enumerable: true };

        return Reflect.getOwnPropertyDescriptor(target, prop);
      },
      ownKeys: () =>
        range(0, arr().length - 1)
          .map(prop => prop + '')
          .concat(['length']),
      getPrototypeOf: () => HTMLCollection.prototype,
    });

    rawNode(coll, obj);
    setParentOwner(coll, owner);

    return coll;
  }
}

define(HTMLCollection.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLCollection' }));

export function NamedNodeMap(delegate, owner) {
  if(!new.target) return new NamedNodeMap(delegate, owner);

  const getset = isFunction(delegate) ? delegate : 'set' in delegate ? gettersetter(delegate) : getter(delegate);
  const adapter = mapObject(getset);

  rawNode(this, delegate);
  setParentOwner(this, owner);

  const obj = new Proxy(this, {
    get: (target, prop, receiver) => {
      if(prop == 'length') return adapter.keys().length;
      if(isString(prop)) {
        if(isNumeric(prop)) {
          const keys = adapter.keys();
          if(prop >= 0 && prop < keys.length) prop = keys[+prop];
        }

        if(adapter.has(prop)) return adapter.get(prop);
      }
      return Reflect.get(target, prop, receiver);
    },
    ownKeys: target => adapter.keys(),
  });

  rawNode(obj, delegate);
  setParentOwner(obj, owner);

  return obj;
}

Object.setPrototypeOf(NamedNodeMap.prototype, Array.prototype);

define(
  NamedNodeMap,
  nonenumerable({
    toString(obj) {
      let s = '';
      for(const { name, value } of [...obj]) {
        if(s) s += ' ';
        s += `${name}="${value}"`;
      }
      return s;
    },
    inspect(obj) {
      const a = [],
        parts = [...obj];
      for(const part of parts) {
        let s = part[Symbol.inspect]?.();

        if(!s) s = `\x1b[1;35m${name}\x1b[0m=\x1b[1;36m"${value}"\x1b[0m`;
        else {
          let i = s.indexOf('Attr');
          if(i != -1) s = s.slice(i + 4);

          s = s.replaceAll(/{\s*(.*)\s*}/g, '$1');
          s = s.replaceAll(/\x1b\[0m\s+\x1b/g, '\x1b');
        }

        a.push(s);
      }

      return isPrototypeOf(Element.prototype, parts[0]) ? a.join(',\n') : ' ' + a.join('');
    },
  }),
);

define(
  NamedNodeMap.prototype,
  nonenumerable({
    constructor: NamedNodeMap,
    [Symbol.toStringTag]: 'NamedNodeMap',
    get path() {
      return Node.path(Node.ownerOrParent(this)).concat(['attributes']);
    },
    item(key) {
      return this[key];
    },
    setNamedItem(attr) {
      const { name, value } = attr;
      Node.raw(this)[name] = value;
    },
    removeNamedItem(name) {
      delete Node.raw(this)[name];
    },
    getNamedItem(name) {
      return Node.raw(this)[name];
    },
    *[Symbol.iterator]() {
      const { length } = this;
      for(let i = 0; i < length; i++) yield this.item(i);
    },
    [Symbol.inspect]() {
      return NamedNodeMap.inspect(this);
    },
  }),
);

/* Element methods:
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

    lazyProperties(this, { classList: () => new TokenList(this, 'class') });
  }

  get tagName() {
    return Node.raw(this).tagName;
  }

  set tagName(value) {
    Node.raw(this).tagName = value;
  }

  set nodeName(value) {
    this.tagName = value;
  }

  get nodeName() {
    return this.tagName;
  }

  get parentElement() {
    let parent = this;

    do {
      parent = Node.ownerOrParent(parent);
    } while(isObject(parent) && parent.nodeType != ELEMENT_NODE);

    return parent;
  }

  get attributes() {
    const raw = (Node.raw(this).attributes ??= {});

    return Factory.for(this).NamedNodeMap.cache(
      {
        get: k => new Attr([() => raw[k], k], this),
        has: k => k in raw,
        keys: () => Reflect.ownKeys(raw),
      },
      this,
    );
  }

  get children() {
    return Factory.for(this).NodeList.cache((Node.raw(this).children ??= []), this /*, e=> e.nodeType == ELEMENT_NODE*/);
  }

  get style() {
    return Factory.for(this).CSSStyleDeclaration.cache((Node.raw(this).attributes ??= {}), this);
  }

  get childElementCount() {
    return Node.children(this)?.length ?? 0;
  }

  get firstElementChild() {
    const element = Node.children(this).find(n => isObject(n) && 'tagName' in n);

    if(element) return Element.cache(element, this.children);

    return null;
  }

  get lastElementChild() {
    const children = Node.children(this);

    if(!children?.length) return null;

    for(let i = children.length - 1; i >= 0; i--) {
      if(isObject(children[i]) && 'tagName' in children[i]) return Element.cache(children[i], this.children);
    }

    return null;
  }

  get id() {
    if(this.hasAttribute('id')) return this.getAttribute('id');
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
    if(!(isString(value) || (value !== null && value !== undefined && value.toString))) throw new TypeError(`Element.setAttribute(): value not of type 'string': ${value}`);

    value = value + '';

    Element.attributes(this)(attributes => (attributes[name] = value));
  }

  get innerText() {
    return this.textContent;
  }

  get outerText() {
    return this.textContent;
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  static cache = MakeCache((obj, owner) => new Element(obj, owner));

  static attributes(elem) {
    return modifier(Node.raw(elem), 'attributes');
  }

  get innerHTML() {
    return [...this.children].map(e => e.outerHTML).join('\n');
  }

  get outerHTML() {
    return Element.toString(this);
  }

  static toString(elem) {
    return new Serializer().serializeToString(elem);
  }

  static xpath(elem, attr = 'name') {
    let r = [],
      prev;

    for(const e of Node.hier(elem, n => n.nodeType == ELEMENT_NODE, false)) {
      let s = e.tagName,
        sameName = [...(prev?.children ?? [])].filter(e2 => e2.tagName == e.tagName);

      if(sameName.length > 1) {
        if(sameName.every(e => attr in e.attributes)) s += '[' + attr + '="' + e.getAttribute(attr) + '"]';
        else s += '[' + (sameName.indexOf(e) + 1) + ']';
      }

      r.push(s);
      prev = Node.raw(e);
    }

    return r.join('/');
  }
}

define(
  Element.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'Element',
    nodeType: ELEMENT_NODE,
    namespaceURI: 'http://www.w3.org/1999/xhtml',
  }),
);

define(Element.prototype, {
  [Symbol.inspect](depth, opts) {
    const { tagName, attributes, children } = this;
    const { length } = children;
    const attrs = attributes[Symbol.inspect](depth + 1, opts)
      .slice(1, -1)
      .trim();
    let str = `<${tagName}`;
    if(attrs) str += ' ' + attrs;
    if(length == 0) str += ' /';
    str += '>';
    if(length) {
      for(const child of children) str += ('\n' + child[Symbol.inspect](depth + 1, opts)).replaceAll('\n', '\n  ');
      str += `\n</${tagName}>`;
    }
    if('breakLength' in opts) {
      const oneline = str.replaceAll(new RegExp('[\\t ]*\\n[ \\t]*', 'g'), '');
      if(oneline.length <= opts.breakLength) return oneline;
    }
    return str;
  },
});

Object.defineProperty(Element.prototype, 'attributes', { configurable: false });

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

    return new TreeWalker(
      raw,
      (node, key) => isNumber(key) && pred(node, key),
      (node, ptr) => GetNode(node, get(raw, ptr.slice(0, -1))),
    );
  }

  get body() {
    const element = this.lastElementChild.lastElementChild;

    try {
      if(/^body$/i.test(element.tagName)) return element;
    } catch(e) {}

    return this.querySelector('frameset') ?? this.querySelector('body');
  }

  [Symbol.inspect](depth, opts) {
    return `\x1b[1;31m${className(this)}\x1b[0m`;
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }
}

define(Document.prototype, nonenumerable({ [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE }));

export class Attr extends Node {
  constructor(raw, owner) {
    super(raw, owner);

    if(raw) {
      rawNode(this, raw);
      setParentOwner(this, owner);

      if(!isFunction(raw[0])) {
        const [obj] = raw;
        const fn = gettersetter(obj);
        raw[0] = (...args) => fn(raw[1], ...args);
      }
    }
  }

  get path() {
    const { ownerElement } = this;
    const [, name] = Node.raw(this);

    return Node.path(ownerElement).concat(['attributes', name]);
  }

  get ownerElement() {
    return Node.ownerOrParent(Node.ownerOrParent(this));
  }

  get ownerDocument() {
    return Node.document(this);
  }

  get name() {
    const [, name] = Node.raw(this);

    return name;
  }

  get value() {
    const [fn, name] = Node.raw(this);

    return fn();
  }

  set value(value) {
    const [fn, name] = Node.raw(this);

    fn(value);
  }

  [Symbol.inspect]() {
    const [fn, name] = Node.raw(this);
    return `\x1b[1;31m${className(this)}\x1b[0m { \x1b[1;35m${name}\x1b[1;34m=${quote(fn(), '"')}\x1b[0m }`;
  }
}

define(
  Attr.prototype,
  nonenumerable({
    nodeType: ATTRIBUTE_NODE,
    [Symbol.toStringTag]: 'Attr',
  }),
);

export class Text extends Node {
  static store = gettersetter(rawNode);

  constructor(key, owner) {
    super(owner ? key : null, owner);

    let get, set;

    if(owner) {
      const raw = owner instanceof NodeList ? Node.raw(owner) : owner;

      if(!(key in raw) && raw.indexOf(key) != -1) key = raw.indexOf(key);

      get = () => raw[key] ?? '';
      set = value => (raw[key] = value);
    } else {
      Text.store(this, key);

      get = () => Text.store(this);
      set = value => Text.store(this, value);
    }

    textValues(this, modifier(get, set));
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

  [Symbol.inspect](depth, opts) {
    return `\x1b[1;31m${this[Symbol.toStringTag]}\x1b[0m \x1b[38;2;192;2550m${quote(this.data, "'")}\x1b[0m`;
  }

  static cache = MakeCache2((key, owner) => new Text(key, owner));
}

Text.prototype.__proto__ = Node.prototype;

define(
  Text.prototype,
  nonenumerable({
    nodeType: TEXT_NODE,
    nodeName: '#text',
    [Symbol.toStringTag]: 'Text',
  }),
);

const Tag = gettersetter(new WeakMap());

export class Comment extends Node {
  constructor(raw, owner) {
    super(raw, owner);

    rawNode(this, raw);
    setParentOwner(this, owner);

    const get = () => raw.tagName ?? '!----';
    const set = value => (raw.tagName = value);

    Tag(
      this,
      modifier(
        () => get().replace(/^!--(.*)--$/g, '$1'),
        value => set(`!--${value}--`),
      ),
    );
  }

  get data() {
    return Tag(this)(value => '<!--' + value + '-->');
  }

  get nodeValue() {
    return Tag(this)(value => value);
  }

  [Symbol.inspect](depth, opts) {
    return `Comment \x1b[38;2;184;0;234m${tthis.data}\x1b[0m`;
  }

  static cache = MakeCache2((node, owner) => new Comment(node, owner));
}

Comment.prototype.__proto__ = Node.prototype;

define(
  Comment.prototype,
  nonenumerable({
    nodeType: COMMENT_NODE,
    nodeName: '#comment',
    [Symbol.toStringTag]: 'Comment',
  }),
);

const Tokens = gettersetter(new WeakMap());

export class TokenList {
  constructor(owner, key = 'class') {
    setParentOwner(this, owner);

    const { attributes } = Node.raw(owner);

    const get = () => attributes[key] ?? '';
    const set = value => (attributes[key] = value);

    Tokens(
      this,
      modifier(
        () => get().trim().split(/\s+/g),
        value => set(value.join(' ')),
      ),
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
      let index;

      for(const token of tokens) {
        if((index = arr.indexOf(token)) == -1) arr.push(token);
      }

      set(arr);
    });
  }

  remove(...tokens) {
    Tokens(this)((arr, set) => {
      let index;

      for(const token of tokens) {
        while((index = arr.indexOf(token)) != -1) arr.splice(index, 1);
      }

      set(arr);
    });
  }

  toggle(token, force) {
    Tokens(this)((arr, set) => {
      let index;

      if((index = arr.indexOf(token)) == -1) {
        arr.push(token);
      } else {
        arr.splice(index, 1);
      }

      set(arr);
    });
  }

  supports(token) {
    throw new TypeError(`TokenList has no supported tokens.`);
  }

  replace(oldToken, newToken) {
    Tokens(this)((arr, set) => {
      let index;

      if((index = arr.indexOf(oldToken)) != -1) {
        arr.splice(index, 1, newToken);
      }

      set(arr);
    });
  }

  [Symbol.inspect](depth, opts) {
    return 'TokenList [' + [...this].join(',') + ']';
  }

  [Symbol.iterator]() {
    return this.values();
  }

  static tokens = Tokens;
}

define(
  TokenList.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'TokenList',
  }),
);

const tokenListFacade = arrayFacade({}, (container, i) => container.item(i));

define(TokenList.prototype, nonenumerable(tokenListFacade));
define(TokenList.prototype, { [Symbol.toStringTag]: 'TokenList' });

const styleImpl = gettersetter(new WeakMap());

export class CSSStyleDeclaration {
  constructor(style, owner) {
    if(isObject(style) && isFunction(style.getAttribute) && isFunction(style.setAttribute)) {
      owner = style;
      style = value => (value === undefined ? owner.getAttribute('style') : owner.setAttribute('style', value));
    } else if(!isFunction(style)) {
      style = gettersetter(style, 'style');
    }

    const impl = {
      styles: parseStyle(style() ?? ''),
      get(key) {
        return key in this.styles ? this.styles[key] : '';
      },
      set(key, value) {
        this.styles[key] = value;
        style(formatStyle(this.styles));
      },
      remove(key) {
        const value = this.styles[key];
        delete this.styles[key];
        style(formatStyle(this.styles));
        return value;
      },
      clear() {
        for(const k in this.styles) delete this.styles[k];
      },
      *keys() {
        for(const k in this.styles) yield k;
      },
    };

    const obj = new Proxy(this, {
      get(target, prop, receiver) {
        if(prop == 'constructor') return CSSStyleDeclaration;
        if(prop == 'length') return Object.keys(impl.styles).length;
        if(prop in target) return Reflect.get(target, prop, receiver);
        if(isString(prop) && prop != 'cssText') {
          const key = decamelize(prop);
          if(key in impl.styles) return impl.styles[key];
        }
      },
      set(target, prop, value) {
        if(prop == 'length') throw new TypeError(`length property is read-only`);
        if(prop in target) return Reflect.set(target, prop, value);
        if(isString(prop) && prop != 'cssText') {
          const key = decamelize(prop);
          impl.set(key, value);
          return;
        }
      },
      deleteProperty(target, prop) {
        if(prop == 'length') throw new TypeError(`length property is read-only`);

        if(isString(prop) && prop != 'cssText') {
          const key = decamelize(prop);
          if(key in impl.styles) {
            impl.remove(key);
            return;
          }
        }

        if(prop in target) return Reflect.deleteProperty(target, prop);
      },
      ownKeys: target => [...impl.keys()].map(k => camelize(k)),
    });

    for(const lnk of [this, obj]) {
      styleImpl(lnk, impl);
      rawNode(lnk, style);

      if(isObject(owner)) setParentOwner(lnk, owner);
    }

    return obj;
  }

  setProperty(k, v) {
    styleImpl(this).set(k, v);
  }

  item(index) {
    let i = 0;
    for(const k of styleImpl(this).keys()) if(i++ == index) return k;
  }

  getPropertyValue(key) {
    return styleImpl(this).get(key);
  }

  getPropertyPriority(key) {
    return '';
  }

  removeProperty(key) {
    return styleImpl(this).remove(key);
  }

  get cssText() {
    return Node.raw(this)();
  }

  set cssText(value) {
    Node.raw(this)(formatStyle((styleImpl(this).styles = parseStyle(value))));
  }

  [Symbol.inspect](depth, opts) {
    const { compact } = opts;
    const multiline = compact !== true && (compact === false || (!isBool(compact) && depth - 1 > compact));
    const spacing = multiline ? '\n' : ' ',
      indent = multiline ? '  ' : '';

    return `\x1b[1;31mCSSStyleDeclaration\x1b[0m {${spacing}${formatStyle(styleImpl(this).styles, ';', spacing, indent)}${spacing}}`;
  }
}

function parseStyle(str) {
  return str
    .split(/\s*;\s*/g)
    .filter(item => /:/.test(item))
    .map(item => item.split(/\s*:\s*/))
    .reduce((acc, [k, v]) => ((acc[k] = v), acc), {});
}

function formatStyle(styles, eol = ';', spc = ' ', ind = '') {
  return Object.entries(styles)
    .map(([k, v]) => `${ind}${k}: ${v}${eol}`)
    .join(spc);
}

define(
  CSSStyleDeclaration.prototype,
  nonenumerable({
    constructor: CSSStyleDeclaration,
    [Symbol.toStringTag]: 'CSSStyleDeclaration',
    get parentRule() {
      return null;
    },
    get cssFloat() {
      return '';
    },
  }),
);

export class Serializer {
  serializeToString(node) {
    return writeXML(Node.raw(node));
  }
}

define(Serializer.prototype, nonenumerable({ [Symbol.toStringTag]: 'Serializer' }));

function keyOf(obj, value) {
  for(const key in obj) if(obj[key] === value) return key;
  return -1;
}

function isNode(obj) {
  return isObject(obj) && 'nodeType' in obj;
}

function setParentOwner(obj, parentOrOwner) {
  const is = isNode(obj);

  if(!is || parentOrOwner == null) parentNodes(obj, parentOrOwner);
  if(is || parentOrOwner == null) ownerElements(obj, parentOrOwner);
}

export function GetType(raw, owner, ctor) {
  if(Array.isArray(raw)) return Entities.NodeList;
  if(isObject(raw) && 'tagName' in raw) return /^!--.*--$/.test(raw.tagName) ? Entities.Comment : Entities.Element;

  if(isString(raw)) return Entities.Text;
  if(isObject(raw)) return Entities.NamedNodeMap;
}

export function* MapItems(list, t) {
  const { length } = list;

  for(let i = 0; i < length; i++) yield t(list[i], i, list);
}

export function FindItemIndex(list, pred) {
  const { length } = list;

  for(let i = 0; i < length; i++) if(pred(list[i], i, list)) return i;

  return -1;
}

export function FindItem(list, pred) {
  return list[FindItemIndex(list, pred)];
}

export function ListAdapter(list, key = 'name') {
  if(!isFunction(key)) {
    const attr = key;
    key = item => Node.raw(item).attributes[attr];
  }

  return Object.setPrototypeOf(
    {
      get: id => FindItem(list, item => key(item) == id),
      keys: () => [...MapItems(list, key)],
      has(id) {
        return this.keys().indexOf(id) != -1;
      },
    },
    ListAdapter.prototype,
  );
}

define(ListAdapter.prototype, nonenumerable({ [Symbol.toStringTag]: 'ListAdapter' }));

function MakeCache(ctor, store = new WeakMap()) {
  const [get, set] = getset(store);

  return (key, ...args) => {
    let value;

    if(!(value = get(key))) {
      value = ctor(key, ...args);
      set(key, value);
    }

    setParentOwner(value, args[0]);
    return value;
  };
}

function MakeCache2(ctor, store = new WeakMap()) {
  const cache = memoize(key => [], store);

  return (id, owner) => {
    const textList = cache(owner);
    if(isNumeric(id)) id = +id;
    textList[id] ??= ctor(id, owner);
    return textList[id];
  };
}

export { URLSearchParams, URL } from './url.js';
