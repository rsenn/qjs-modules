import { readFileSync } from 'fs';
import { wrapFunction, isInstanceOf, types, className, nonenumerable, extend, arrayFacade, assert, camelize, decamelize, define, mapObject, getset, getter, gettersetter, isBool, isObject, isFunction, isNumeric, isNumber, isString, isSymbol, isPropertyKey, lazyProperties, memoize, modifier, quote, range, properties, isPrototypeOf, weakMapper, decodeHTMLEntities, } from 'util';
import { parseSelectors } from './css3-selectors.js';
import { get, iterate, find, clone, select, RECURSE, YIELD_NO_RECURSE, PATH_AS_POINTER, FILTER_KEY_OF, FILTER_HAS_KEY, FILTER_NEGATE, RETURN_VALUE_PATH, RETURN_PATH, RETURN_VALUE, TYPE_STRING, TYPE_OBJECT, } from 'deep';
import { TreeWalker } from 'tree_walker';
import { read as readXML, write as writeXML } from 'xml';
import { DereferenceError, Pointer } from 'pointer';

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

const DEBUG = (env => {
  const { length } = [...env.matchAll(/\bdom\b/gi)];
  return length > 0 ? (...args) => console.log('\x1b[1;33mDOM\x1b[0m', console.config({ depth: 1, compact: true }), ...args) : () => {};
})(process.env.DEBUG ?? '');

const proxyOf = gettersetter(new WeakMap());
const proxyFor = gettersetter(new WeakMap());

const proxy = (proxy, obj) => (proxyOf(obj, proxy), proxyFor(proxy, obj));

const rawNode = gettersetter(new WeakMap());
const parentNodes = gettersetter(new WeakMap());
const ownerElements = gettersetter(new WeakMap());
const ownerDocument = gettersetter(new WeakMap());
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

export class DOMException extends Error {
  constructor(message, name) {
    super(message ?? 'DOMException');

    if(name) define(this, nonenumerable({ name }));
  }
}

define(DOMException.prototype, nonenumerable({ name: 'DOMException' }));

function applyPath(path, obj) {
  const { length } = path;

  let raw = Node.raw(obj) ?? rawNode(obj);

  for(let i = 0; i < length; i++) {
    const k = path[i];
    try {
      obj = obj[k];
    } catch(error) {
      throw new DereferenceError(obj, i, path);
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

  for(const selector of selectors) {
    if((path = find(root, selector, RETURN_PATH | FILTER_HAS_KEY, TYPE_OBJECT, ['tagName']))) {
      return t(path, root);
    }
  }
}

function* queryAll(root, selectors, t = (path, root) => path) {
  for(const selector of selectors) {
    for(const path of iterate(root, selector, RETURN_PATH | FILTER_HAS_KEY, TYPE_OBJECT, ['tagName'])) {
      yield t(path, root);
    }
  }
}

function* walk(root) {
  const raw = Node.raw(root) ?? rawNode(root);
  for(let path of iterate(raw, () => true, RETURN_PATH, TYPE_OBJECT | TYPE_STRING, ['children'])) {
    yield path.reduce((o, k) => o[k], root);
  }
}

const nodeTypes = [
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

export function Classes() {
  return {
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
  };
}

export function Prototypes(constructors = Classes()) {
  const prototypes = {};
  for(const key in constructors) {
    prototypes[key] = constructors[key].prototype;

    if(constructors[key].name && constructors[key].name != prototypes[key][Symbol.toStringTag]) define(prototypes[key], { [Symbol.toStringTag]: constructors[key].name });
  }

  return prototypes;
}

const factories = gettersetter(new WeakMap());

export class Factory {
  constructor(obj = Prototypes()) {
    if(Array.isArray(obj)) obj = obj.reduce((acc, proto, i) => ({ [EntityNames[i]]: proto, ...acc }), {});

    const GetProto = type => ((type = TypeName(type)), obj[type] ?? Prototypes()[type]);
    const GetConstructor = type => GetProto(type).constructor; //((type = TypeName(type)), Classes()[type]);
    const fn = type => fn[TypeName(type)]?.new;

    const entities = gettersetter(new WeakMap()),
      classes = Object.create(null);

    let create = obj;

    if(!isFunction(create))
      create = (type, ...args) => {
        let proto = GetProto(type);
        let ctor = GetConstructor(type);

        if(type == 'Element') {
          const ctor2 = ctor.elements[args[0].tagName];
          //DEBUG('Factory', { type, raw: args[0], ctor2 });
          if(ctor2?.constructor) ctor = ctor2.constructor;
          else if(ctor2) ctor = ctor2;
        }
        return new ctor(...args);
      };

    const cr = create;

    create = (type, ...args) => {
      const obj = cr(type, ...args);
      entities(obj, { type, args });
      factories(obj, fn);
      return obj;
    };

    for(let i = 0; i < EntityNames.length; i++) {
      const name = EntityNames[i];
      const ctor = GetProto(name).constructor;

      fn[name] = {
        new: (...args) => create(name, ...args),
        cache(...args) {
          return (ctor.cache ??= MakeCache((...a) => this.new(...a)))(...args);
        },
      };

      classes[name] = ctor;
    }

    try {
      delete fn.name;
    } catch(e) {}

    const proto = new.target ? new.target.prototype : Factory.prototype;

    define(fn, nonenumerable({ name: proto[Symbol.toStringTag], entities, classes }));

    return Object.setPrototypeOf(fn, proto);
  }

  static type(node) {
    const factory = this.get(node);
    let cl;
    return factory.entities(node)?.type ?? ((cl = className(node)) in factory && cl);
  }

  static for(node) {
    const factory = this.get(node);

    if(!factory) throw new Error(`No factory for <${className(node)}> [[ ${Element.toString(node)} ]]`);

    return factory;
  }

  static get(node) {
    let tmp;
    if((tmp = proxyFor(node))) node = tmp;
    if((tmp = ownerElements(node))) node = tmp;

    return factories(node) ?? factories(Node.document(node));
  }

  static set(node, factory) {
    let tmp;
    if((tmp = proxyFor(node))) node = tmp;
    if((tmp = ownerElements(node))) node = tmp;

    factories(node, factory);

    if(Node.document(node) !== node) this.set(Node.document(node), factory);
  }
}

Object.setPrototypeOf(Factory.prototype, function Factory(type) {});

define(
  Factory.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'Factory',
  }),
);

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

function GetType(raw) {
  if(Array.isArray(raw)) return Entities.NodeList;
  if(isComment(raw)) return Entities.Comment;
  if(isElement(raw)) return Entities.Element;
  if(isString(raw)) return Entities.Text;
  if(isObject(raw)) return Entities.NamedNodeMap;
}

function GetNode(raw, owner, factory = Factory.for(owner)) {
  const type = GetType(raw);

  if(type == Entities.Text) {
    const rawOwner = Node.raw(owner);
    raw = rawOwner.indexOf(raw);
  }

  const ctor = factory(type);

  if(!ctor) throw new Error(`No such node type for ${raw[Symbol.inspect]()}`);

  const node = ctor.cache ? ctor.cache(raw, owner) : ctor(raw, owner);

  DEBUG('GetNode', { node, raw, owner });

  return node;
}

export class Interface {
  static [Symbol.hasInstance](instance) {
    return isObject(instance) && 'nodeType' in instance;
  }

  get textContent() {
    if(this.nodeType == TEXT_NODE) return this.nodeValue;

    const texts = [];

    for(const value of iterate(Node.raw(this), (v, k) => isNumber(k), RETURN_VALUE, TYPE_STRING)) texts.push(value.replace(/<s+/g, ' '));

    return decodeHTMLEntities(texts.join(' '));
  }

  get isConnected() {
    return isObject(ownerElements(this));
  }

  get nodeName() {
    return { [DOCUMENT_NODE]: '#document', [TEXT_NODE]: '#text', [ELEMENT_NODE]: this.tagName.toUpperCase() }[this.nodeType];
  }

  get nodeValue() {
    return { [TEXT_NODE]: this.textContent }[this.nodeType] ?? null;
  }

  get parentNode() {
    return Node.parent(this);
    /*let result = ownerElements(this);
    if(!isNode(result)) result = ownerElements(result);
    return result;*/
  }

  get parentElement() {
    const { parentNode } = this;
    return parentNode.nodeType == ELEMENT_NODE ? parentNode : null;
  }

  contains(other) {
    for(const node of walk(this)) {
      if(node == other) return true;
      if(isInstanceOf(Node, node) && node.isSameNode(other)) return true;
    }
    return false;
  }

  isSameNode(other) {
    return Node.raw(other) == Node.raw(this);
  }

  isEqualNode(other) {
    const s = new Serializer();
    const [a, b] = [this, other].map(n => s.serializeToString(n));
    return a == b;
  }

  hasChildNodes() {
    const { children } = Node.raw(this);
    return children && children.length > 0;
  }

  getRootNode() {
    for(let parent, node = ownerElements(this); node; node = parent) if(!(parent = ownerElements(node))) return node;
  }

  get ownerDocument() {
    return Node.document(this) || ownerDocument(this);
  }

  get childNodes() {
    const raw = Node.raw(this);
    const list = Factory.for(this).NodeList.cache((raw.children ??= []), this, NodeList);
    ownerElements(list, this);
    return list;
  }

  get firstChild() {
    if(this.hasChildNodes()) return GetNode(Node.raw(this).children[0], this);
  }

  get lastChild() {
    if(this.hasChildNodes()) {
      const { children } = Node.raw(this);
      return GetNode(children[children.length - 1], this);
    }
  }

  get nextSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const { children } = Node.raw(parentNode);
      const raw = Node.raw(this);
      const index = children.indexOf(raw);
      const owner = ownerElements(this) ?? parentNode;

      console.log('get nextSibling', { children, raw, index, owner });

      if(index != -1 && children[index + 1]) return GetNode(children[index + 1], owner);
    }
  }
  get previousSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const { children } = Node.raw(parentNode);
      const raw = Node.raw(this);
      const index = children.indexOf(raw);
      const owner = ownerElements(this) ?? parentNode;

      console.log('get previousSibling', { children, raw, index, owner });

      if(index != -1 && children[index - 1]) return GetNode(children[index - 1], owner);
    }
  }

  cloneNode(deep = true) {
    const obj = clone(Node.raw(this));
    const factory = Factory.for(this);
    const el = factory(Factory.type(this))?.(obj, null);

    ownerDocument(el, this.ownerDocument);

    return el;
  }

  appendChild(node) {
    ownerElements(node)?.removeChild(node);

    const raw = Node.raw(node);
    const { children } = Node.raw(this);

    if(isInstanceOf(Text, node)) textValues(this, Text.own(this, children.length));

    children.push(raw);

    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);

    parentNodes(node, this);

    return node;
  }

  insertBefore(node, ref) {
    ownerElements(node)?.removeChild(node);

    const { children } = Node.raw(this);
    const old = isNode(node) ? Node.raw(node) : node,
      before = isNode(ref) ? Node.raw(ref) : ref;
    let index = children.indexOf(before);

    if(index == -1) index = children.length;

    children.splice(index, 0, old);

    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);
    parentNodes(node, this);

    return node;
  }

  removeChild(node) {
    const { children } = Node.raw(this);
    let index = children.indexOf(isNode(node) ? Node.raw(node) : node);
    if(index == -1) throw new Error(`Node.removeChild no such child!`);
    children.splice(index, 1);
    setParentOwner(node, null);
    return node;
  }

  replaceChild(newChild, oldChild) {
    ownerElements(newChild)?.removeChild(newChild);

    const { children } = Node.raw(this);
    const old = Node.raw(oldChild),
      node = Node.raw(newChild);
    const idx = children.indexOf(old);

    if(idx == -1) throw new Error(`Node.replaceChild no such child!`);

    children.splice(idx, 1, node);

    setParentOwner(old, null);
    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);
    parentNodes(node, this);

    return oldChild;
  }

  querySelector(s) {
    const raw = Node.raw(this);
    try {
      for(let sel of parseSelectors(s)) {
        const values = sel && sel.values();

        let path = find(
          raw,
          sel
            ? (node, path) => {
                return values.every(v =>
                  path
                    .hier()
                    .filter(p => p[p.length - 1] != 'children')
                    .reverse()
                    .some(p => v(p.deref(raw))),
                )
                  ? YIELD_NO_RECURSE
                  : RECURSE;
              }
            : e => !/^[!?]/.test(e.tagName),
          RETURN_PATH | PATH_AS_POINTER | FILTER_KEY_OF,
          TYPE_OBJECT | TYPE_STRING,
          ['children'],
        );

        if(path) return applyPath(path, this);
      }
    } catch(e) {
      const { message, pointer, root, pos, stack } = e;
      DEBUG('querySelector', console.config({ compact: 1 }), { message, pointer, root, pos, stack });
      throw new Error(message);
    }
  }

  *querySelectorAll(s) {
    const raw = Node.raw(this);
    try {
      for(let sel of parseSelectors(s)) {
        const values = sel && sel.values();

        for(const path of iterate(
          raw,
          sel
            ? (node, path) => {
                return values.every(v =>
                  path
                    .hier()
                    .filter(p => p[p.length - 1] != 'children')
                    .reverse()
                    .some(p => v(p.deref(raw))),
                )
                  ? YIELD_NO_RECURSE
                  : RECURSE;
              }
            : e => !/^[!?]/.test(e.tagName),
          RETURN_PATH | PATH_AS_POINTER | FILTER_KEY_OF,
          TYPE_OBJECT | TYPE_STRING,
          ['children'],
        ))
          yield applyPath(path, this);
      }
    } catch(e) {
      const { message, pointer, root, pos, stack } = e;
      DEBUG('querySelectorAll', console.config({ compact: 1 }), { message, pointer, root, pos, stack });
      throw new Error(message);
    }
  }

  *getElementsByTagName(name) {
    for(const [value, path] of iterate(Node.raw(this), name == '*' ? e => true : e => e.tagName == name, RETURN_VALUE_PATH, TYPE_OBJECT, ['children'])) yield applyPath(path, this);
  }
}

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

define(Interface.prototype, nonenumerable(NODE_TYPES));

export class Node extends Interface {
  constructor(obj, parent) {
    super();
    rawNode(this, obj);
    setParentOwner(this, parent);
  }

  static [Symbol.hasInstance](instance) {
    return isObject(instance) && 'nodeType' in instance;
  }

  [Symbol.inspect]() {
    return `\x1b[1;31m${className(this) || 'Node'}\x1b[0m`;
  }

  static check(node) {
    if(!isObject(node)) throw new TypeError('node is not an object');
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Node.prototype, Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  static raw(node) {
    let tmp;
    if((tmp = proxyFor(node))) node = tmp;

    return rawNode(node);
  }

  static proxyFor(proxy) {
    return proxyFor(proxy);
  }

  static proxyOf(node) {
    return proxyOf(node);
  }

  /*static parentOrOwner(node) {
    this.check(node);
    return parentNodes(node) ?? ownerElements(node);
  }*/

  static document(node) {
    let doc = node;
    while(doc) {
      if(doc.nodeType == Node.DOCUMENT_NODE) break;
      doc = ownerElements(doc);
    }
    if(doc) ownerDocument(node, doc);
    else doc = ownerDocument(node);
    return doc;
  }

  static *up(node) {
    this.check(node);
    let next;
    do {
      yield node;
      next = parentNodes(node) ?? ownerElements(node);
    } while(next && (node = next));
  }

  static depth(node, pred = (node, path) => true) {
    return this.hier(node, pred).length;
  }

  static parent(node) {
    let tmp;
    if((tmp = parentNodes(node))) return tmp;
    if((tmp = ownerElements(node))) {
      const n2 = tmp;
      if((tmp = ownerElements(n2) || parentNodes(n2))) {
        node = tmp;
        return node;
      }
    }
  }

  static hier(node, pred = (node, path) => true, forward = false, t) {
    const r = [],
      p = [],
      method = r[forward ? 'push' : 'unshift'];
    let prev;
    for(const n of this.up(node)) {
      const raw = rawNode(n) ?? Node.raw(n);
      if(raw && prev) {
        const entries = Object.entries(raw);
        let [k] = entries.find(([k, v]) => v == prev) ?? [];
        //if(k === undefined) DEBUG('hier', { k, raw, prev });
        if(isNumeric(k)) k = +k;
        p.unshift(k);
      }
      if(!pred || pred(n, p)) {
        if(r.indexOf(n) != -1) throw new Error(`circular loop`);
        method.call(r, isFunction(t) ? t(n, p) : n);
      }
      prev = raw;
    }
    return r;
  }

  static document(node) {
    const hier = Node.hier(node);
    return hier.find(({ nodeType }) => nodeType == DOCUMENT_NODE);
  }

  static path(node, path) {
    const [tmp] = Node.hier(
      node,
      n => isInstanceOf(Document, n),
      false,
      (n, p) => p.slice(),
    );
    if(!tmp) return undefined;
    (path ??= []).push(...tmp);
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

//Object.setPrototypeOf(Node.prototype, Object.create(Interface.prototype));

define(Node.prototype, nonenumerable({ [Symbol.toStringTag]: 'Node' }));

export class NodeList {
  constructor(obj, owner) {
    // DEBUG('NodeList.constructor', { obj, owner });

    const isIndex = prop => isString(prop) && isNumeric(prop);
    const inRange = index => index >= 0 && index < obj.length;

    rawNode(this, obj);
    setParentOwner(this, owner);

    const nodeList = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop == 'length') return obj.length;

        if(isIndex(prop)) {
          let node;
          if(prop in obj) {
            node = GetNode(obj[prop], nodeList, Factory.for(owner));
            ownerElements(node, this);
          }
          return node;
        }

        if(isFunction(NodeList.prototype[prop])) return NodeList.prototype[prop];

        return Reflect.get(target, prop, receiver);
      },
      deleteProperty: (target, prop) => {
        if(isIndex(prop)) {
          if(+prop + 1 == obj.length) obj.pop();
          else delete obj[prop];
          return true;
        }

        return Reflect.deleteProperty(target, prop);
      },
      set: (target, prop, value, receiver) => {
        if(isIndex(prop)) {
          obj[prop] = Node.raw(value);
          return;
        }
        return Reflect.set(target, prop, value, receiver);
      } /*,
      getOwnPropertyDescriptor: (target, prop) => {
        if(prop == 'length') return { value: obj.length, configurable: false, enumerable: true, writable: false };
        if(isIndex(prop)) return { value: inRange(+prop) ? GetNode(obj[prop], nodeList, Factory.for(owner)) : undefined, configurable: true, enumerable: true, writable: true };
        return Reflect.getOwnPropertyDescriptor(target, prop);
      }*/,
      ownKeys: () =>
        range(0, obj.length - 1)
          .map(prop => prop + '')
          .concat(['length']),
      //getPrototypeOf: () => NodeList.prototype,
    });

    /*rawNode(nodeList, obj);
    setParentOwner(nodeList, owner);*/

    ownerElements(this, owner);
    proxy(nodeList, this);

    return nodeList;
  }
}

define(
  NodeList.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'NodeList',
    *[Symbol.iterator]() {
      const factory = Factory.for(this);

      for(const node of Node.raw(this)) {
        const type = isString(node) ? Entities.Text : isElement(node) ? (isComment(node) ? Entities.Comment : Entities.Element) : undefined;

        yield factory(type)(node, this);
      }
    },
  }),
);

export function Collection(obj, get, len, proto) {
  if(!get) {
    const owner = ownerElements(obj);
    const factory = Factory.for(obj);
    const arr = Node.raw(obj).children;

    get = k => GetNode(arr[k], owner, factory);
    len = () => arr.length;
  }

  const coll = new Proxy(obj, {
    get: (target, prop, receiver) => {
      if(prop == 'length') return len();

      if(isNumeric(prop)) return get(prop);

      if(proto && isFunction(proto[prop])) return proto[prop];

      return Reflect.get(target, prop, receiver);
    },
    getOwnPropertyDescriptor: (target, prop) => {
      if(prop == 'length') return { value: len(), configurable: true, enumerable: false };

      if(isNumeric(prop)) return { value: get(prop), configurable: true, enumerable: true };

      return Reflect.getOwnPropertyDescriptor(target, prop);
    },
    ownKeys: () =>
      range(0, len() - 1)
        .map(prop => prop + '')
        .concat(['length']),
    ...(proto ? { getPrototypeOf: () => proto } : {}),
  });

  proxy(coll, obj);

  return coll;
}

export class HTMLCollection {
  constructor(obj, owner, pred = e => true) {
    const arr = () => obj.filter(pred);

    rawNode(this, obj);
    setParentOwner(this, owner);

    const coll = Collection(
      this,
      (
        factory => k =>
          GetNode(arr()[k], owner, factory)
      )(Factory.for(owner)),
      () => arr().length,
      HTMLCollection.prototype,
    );

    rawNode(coll, obj);
    setParentOwner(coll, owner);
    proxy(coll, this);

    return coll;
  }

  *[Symbol.iterator]() {
    const { length } = this;

    for(let i = 0; i < length; i++) yield this[i];
  }
}

define(HTMLCollection.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLCollection' }));

export function NamedMap(node, get, keys) {
  if(isString(get)) {
    const key = get;
    get = n => [...node.children].find(e => e.getAttribute(key) == n);
    keys ??= () => [...node.children].map(e => e.getAttribute(key));
  }

  const obj = new Proxy(node, {
    get: keys
      ? (target, prop, receiver) => {
          if(prop == 'length') return keys().filter(isPropertyKey).length;
          if(isNumeric(prop)) {
            const a = keys();
            if(prop >= 0 && prop < a.length) prop = a[+prop];
          }

          if(isString(prop)) {
            let tmp = get(prop);
            if(tmp) return tmp;
          }
          //if(keys().includes(prop)) return get(prop);

          return Reflect.get(target, prop, receiver);
        }
      : (target, prop, receiver) => {
          let tmp = get(prop);
          if(tmp) return tmp;
          return Reflect.get(target, prop, receiver);
        },
    ...(keys ? { ownKeys: target => keys().filter(isPropertyKey) } : {}),
  });

  proxy(obj, node);

  return obj;
}

export function NamedNodeMap(delegate, owner) {
  if(!this) return new NamedNodeMap(delegate, owner);

  //const getset = isFunction(delegate) ? delegate : 'set' in delegate ? gettersetter(delegate) : getter(delegate);
  const adapter = isFunction(delegate) ? mapObject(delegate) : delegate;

  rawNode(this, delegate);
  setParentOwner(this, owner);

  const obj = NamedMap(this, adapter.get, adapter.keys);

  rawNode(obj, delegate);
  setParentOwner(obj, owner);

  return obj;
}

//Object.setPrototypeOf(NamedNodeMap.prototype, Array.prototype);

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
        keys = Reflect.ownKeys(obj); /*,
        parts = types.isIterable(obj) ? [...obj] : []*/

      for(const key of keys) {
        const part = obj[key];
        let s = part[Symbol.inspect]?.(0, {}) ?? '';

        let i = s.indexOf('Attr');
        if(i != -1) s = s.slice(i + 4);
        else s = (isNumeric(key) ? `[${key}]` : key) + ': ' + s;

        s = s.replaceAll(/{\s*(.*)\s*}/g, '$1');
        s = s.replaceAll(/\x1b\[0m\s+\x1b/g, '\x1b');

        a.push(s);
      }

      if(isPrototypeOf(Element.prototype, obj[keys[0]])) return `\x1b[1;31m${className(obj)}\x1b[0m {\n  ` + a.join(',\n  ') + `\n}`;

      return ' ' + a.join('').trim();
    },
  }),
);

define(
  NamedNodeMap.prototype,
  nonenumerable({
    constructor: NamedNodeMap,
    [Symbol.toStringTag]: 'NamedNodeMap',
    get path() {
      return Node.path(ownerElements(this)).concat(['attributes']);
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
    //DEBUG('Element.constructor', { obj, parent });
    super(obj, parent);

    lazyProperties(this, { classList: () => new TokenList(this, 'class') });
  }

  static [Symbol.hasInstance](instance) {
    return isObject(instance) && instance.nodeType == ELEMENT_NODE;
  }

  get tagName() {
    const raw = Node.raw(this);
    if(!raw) console.log('get tagName', { thisObj: this, raw });
    return raw.tagName;
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

  get attributes() {
    const raw = (Node.raw(this).attributes ??= {});
    const gs = gettersetter(raw);
    const factory = Factory.for(this);

    return factory.NamedNodeMap.cache(
      {
        get: k => new Attr([(...args) => gs(k, ...args), k], this),
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
    return Node.raw(this).children?.length ?? 0;
  }

  get firstElementChild() {
    const element = Node.raw(this).children.find(n => isObject(n) && 'tagName' in n);

    if(element) return Element.cache(element, this.children);

    return null;
  }

  get lastElementChild() {
    const { children } = Node.raw(this);

    if(!children?.length) return null;

    for(let i = children.length - 1; i >= 0; i--) {
      if(isObject(children[i]) && 'tagName' in children[i]) return Element.cache(children[i], this.children);
    }

    return null;
  }

  get nextElementSibling() {
    let node = this;
    while((node = node.nextSibling)) if(node.nodeType == node.ELEMENT_NODE) break;
    return node;
  }

  get previousElementSibling() {
    let node = this;
    while((node = node.previousSibling)) if(node.nodeType == node.ELEMENT_NODE) break;
    return node;
  }

  get id() {
    if(this.hasAttribute('id')) return this.getAttribute('id');
  }

  getAttribute(name) {
    return Node.raw(this).attributes[name];
    //return Element.attributes(this)(attributes => attributes[name]);
  }

  getAttributeNames() {
    return Object.keys(Node.raw(this).attributes);
    //return Element.attributes(this)(attributes => Object.keys(attributes));
  }

  hasAttribute(name) {
    return name in Node.raw(this).attributes;
    //return Element.attributes(this)(attributes => name in attributes);
  }

  hasAttributes() {
    return this.getAttributeNames().length > 0;
  }

  removeAttribute(name) {
    return delete Node.raw(this).attributes[name];
    //return Element.attributes(this)(attributes => delete attributes[name]);
  }

  getAttributeNode(name) {
    return this.attributes[name];
  }

  setAttribute(name, value) {
    if(!(isString(value) || (value !== null && value !== undefined && value.toString))) throw new TypeError(`Element.setAttribute(): value not of type 'string': ${value}`);

    value = value + '';

    const raw = Node.raw(this);

    const oldValue = raw.attributes[name];

    raw.attributes[name] = value;

    try {
      MutationObserver.eventFor(this, MutationRecord.attribute(name, null, this, oldValue));
    } catch(e) {}
  }

  get innerText() {
    return this.textContent;
  }

  set innerText(s) {
    const { children } = Node.raw(this);
    children.splice(0, children.length);
    this.appendChild(this.ownerDocument.createTextNode(s));
  }

  get outerText() {
    return this.textContent;
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  static cache = MakeCache((obj, owner) => new Element(obj, owner));

  /*static attributes(elem) {
    return modifier(Node.raw(elem), 'attributes');
  }*/

  get innerHTML() {
    return [...this.children].map(e => (e.nodeType == e.TEXT_NODE ? e.data : 'outerHTML' in e ? e.outerHTML : e.toString?.())).join('\n');
  }

  get outerHTML() {
    return new Serializer().serializeToString(this);
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

define(
  Element.prototype,
  nonenumerable({
    [Symbol.inspect](depth, opts) {
      const { tagName, attributes, children } = this;
      const { length } = children ?? [];
      let str = `<${tagName}`;
      if(attributes) str += ' ' + NamedNodeMap.inspect(attributes, depth + 1, opts).trim();
      if(length == 0) str += ' /';
      str = str.trimEnd() + '>';

      if(length) {
        if(depth <= opts.depth) {
          let i = 0;
          for(const child of children) {
            if(i++ == opts.maxArrayLength) {
              str += `\n... ${length - opts.maxArrayLength} more children ...`;
              break;
            }
            str += ('\n' + child[Symbol.inspect](depth + 1, opts)).replaceAll('\n', '\n  ');
          }
          str += '\n';
        } else {
          str += `[... ${length} children ...]`;
        }
        str += `</${tagName}>`;
      }
      return `\x1b[1;31m${className(this) || 'Element'}\x1b[0m ${str}`;
    },
  }),
);

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
    //DEBUG('Document.constructor', { obj, factory });

    super(obj, null, factory);
  }

  createAttribute(name, value) {
    const a = new Attr([null, name], null);
    ownerDocument(a, this);
    return a;
  }

  createElement(tagName) {
    const e = Element.cache({ tagName, attributes: {}, children: [] }, null);
    ownerDocument(e, this);
    return e;
  }

  createTextNode(text) {
    const n = new Text(text);
    ownerDocument(n, this);
    return n;
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
    return `\x1b[1;31m${className(this) || 'Document'}\x1b[0m`;
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
    return ownerElements(ownerElements(this));
  }

  get ownerDocument() {
    let doc;
    if((doc = Node.document(this))) ownerDocument(this, doc);
    return ownerDocument(this);
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
    return `\x1b[1;31m${className(this) || 'Attr'}\x1b[0m { \x1b[1;35m${name}\x1b[1;34m=${quote(fn(), '"')}\x1b[0m }`;
  }
}

define(
  Attr.prototype,
  nonenumerable({
    nodeType: ATTRIBUTE_NODE,
    [Symbol.toStringTag]: 'Attr',
  }),
);

//const charData = gettersetter(new WeakMap());

export class CharacterData extends Node {
  constructor(gs, owner) {
    super(null, owner);

    if(isFunction(gs)) textValues(this, gs);
  }

  get data() {
    return textValues(this)();
  }

  set data(v) {
    textValues(this)(v);
  }

  appendData(data) {
    const s = textValues(this)() + data;
    textValues(this)(s);
    return s;
  }

  deleteData(offset, count) {
    const s = textValues(this)();
    textValues(this)(s.slice(0, offset) + s.slice(offset + count));
  }

  insertData(offset, data) {
    const s = textValues(this)();
    textValues(this)(s.slice(0, offset) + data + s.slice(offset));
  }

  replaceData(offset, count, data) {
    const s = textValues(this)();
    textValues(this)(s.slice(0, offset) + data + s.slice(offset + count));
  }
}

export class Text extends CharacterData {
  static store = gettersetter(rawNode);

  static [Symbol.hasInstance](instance) {
    return instance.nodeType == TEXT_NODE;
  }

  constructor(key, owner) {
    super(owner ? Text.own(owner, key) : (...args) => Text.store(this, ...args));
    if(!owner && typeof key == 'string') Text.store(this, key);
    //textValues(this, owner ? Text.own(owner, key) : (...args) => Text.store(this, ...args));
  }

  static own(owner, key) {
    const raw = Node.raw(owner) ?? owner;
    const idx = raw.indexOf?.(key);
    if(!(key in raw) && idx != -1) key = idx;
    return gettersetter([() => raw[key] ?? '', value => (raw[key] = value)]);
  }

  toString() {
    return this.data;
  }

  [Symbol.inspect](depth, opts) {
    return `\x1b[1;31m${className(this) || 'Text'}\x1b[0m \x1b[38;2;192;2550m${quote(this.data, "'")}\x1b[0m`;
  }

  static cache = MakeCache2((key, owner) => new Text(key, owner));
}

//Object.setPrototypeOf(Text.prototype, Node.prototype);

define(
  Text.prototype,
  nonenumerable({
    nodeType: TEXT_NODE,
    nodeName: '#text',
    [Symbol.toStringTag]: 'Text',
    get data() {
      return textValues(this)?.();
    },
    get nodeValue() {
      return textValues(this)?.();
    },
  }),
);

//define(Text.prototype, Interface.prototype);

const Tag = gettersetter(new WeakMap());

export class Comment extends CharacterData {
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
    return `\x1b[38;5;236m${className(this) || 'Comment'} \x1b[38;2;184;0;234m${this.data}\x1b[0m`;
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
    return `\x1b[1;31m${className(this) || 'TokenList'}\x1b[0m [` + [...this].join(',') + ']';
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

    proxy(obj, this);

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

    return `\x1b[1;31m${className(this) || 'CSSStyleDeclaration'}\x1b[0m {${spacing}${formatStyle(styleImpl(this).styles, ';', spacing, indent)}${spacing}}`;
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

function isElement(node) {
  return isObject(node) && 'tagName' in node;
}

function isComment(node) {
  return isElement(node) && node.tagName[0] == '!';
}

function isCollection(node) {
  return isInstanceOf([NodeList, HTMLCollection], node);
}

function setParentOwner(node, ancestor) {
  const is = isNode(node);

  let ok = [node, ancestor].reduce((i, e) => i ^ isCollection(e), 0);

  //DEBUG('setParentOwner', console.config({ compact: true }), { ok, node, ancestor });

  if(!is || ancestor == null) parentNodes(node, ancestor);
  if(is || ancestor == null) ownerElements(node, ancestor);
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

export class MutationRecord {
  addedNodes = new NodeList();
  removedNodes = new NodeList();
  attributeName = null;
  attributeNamespace = null;
  nextSibling = null;
  previousSibling = null;

  constructor(opts = {}) {
    define(this, opts);
  }

  static attribute(name, ns = null, target, oldValue) {
    return new MutationRecord({ type: 'attribute', attributeName: name, attributeNamespace: ns, target, oldValue });
  }

  static characterData(target, oldValue) {
    return new MutationRecord({ type: 'characterData', target, oldValue });
  }

  static childList(target, nextSibling, previousSibling) {
    return new MutationRecord({ type: 'childList', target, nextSibling, previousSibling });
  }
}

MutationRecord.prototype[Symbol.toStringTag] = 'MutationRecord';

export class MutationObserver {
  #callback;
  static #observe = weakMapper(() => new Array(), new WeakMap());
  #targets = new Set();

  static observationsFor(target) {
    return MutationObserver.#observe.get(target);
  }

  static eventFor(target, ...records) {
    const recordsFor = weakMapper(() => new Array(), new Map());

    for(let { observer, ...options } of this.observationsFor(target)) {
      for(let record of records) {
        if(options[{ attribute: 'attributes' }[record.type] ?? record.type]) recordsFor(observer).push(record);
      }
    }

    for(let [observer, records] of recordsFor.map) {
      observer.event(...records);
    }
  }

  constructor(callback) {
    this.#callback = callback;
  }

  observe(target, options = {}) {
    let { subtree = false, childList = false, attributes, attributeFilter, attributeOldValue, characterData, characterDataOldValue = false } = options;

    if(attributes === undefined) attributes = isObject(attributeFilter) || attributeOldValue !== undefined;
    if(characterData === undefined) characterData = characterDataOldValue !== undefined;

    MutationObserver.#observe(target).push({ observer: this, subtree, childList, attributes, attributeFilter, attributeOldValue, characterData, characterDataOldValue });
    this.#targets.add(target);
  }

  disconnect() {
    for(const target of this.#targets) {
      const observers = this.#observe(target);
    }
  }

  event(...args) {
    return this.#callback(args, this);
  }
}

MutationObserver.prototype[Symbol.toStringTag] = 'MutationObserver';

export { URLSearchParams, URL } from './url.js';
