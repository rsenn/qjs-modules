import { readFileSync } from 'fs';
import { extend, arrayFacade, assert, camelize, decamelize, define, getset, gettersetter, isBool, isObject, isFunction, isNumber, isString, lazyProperties, memoize, modifier, quote, range, properties, } from 'util';
import { parseSelectors } from './css3-selectors.js';
import { get, iterate, find, RETURN_PATH, RETURN_VALUE, TYPE_STRING, TYPE_OBJECT } from 'deep';
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
  for(let selector of selectors) if((path = find(root, selector, RETURN_PATH, TYPE_OBJECT, ['children']))) return t(path, root);
}

function* queryAll(root, selectors, t = (path, root) => path) {
  for(let selector of selectors) for (let path of iterate(root, selector, RETURN_PATH, TYPE_OBJECT, ['children'])) yield t(path, root);
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
  },
) {
  const prototypes = {};

  for(let key in constructors) prototypes[key] = constructors[key].prototype;

  return prototypes;
}

const factories = gettersetter(new WeakMap());

export function Factory(types = Prototypes()) {
  let result;

  if(new.target) {
    result = function Factory(type) {
      if(isNumber(type)) type = EntityNames[type];

      return result[type].new;
    };

    if(Array.isArray(types)) types = types.reduce((acc, proto, i) => ({ [EntityNames[i]]: proto, ...acc }), {});

    if(!isFunction(types)) {
      const obj = types;

      types = (type, ...args) => {
        if(isNumber(type)) type = EntityNames[type];

        return new obj[type].constructor(...args);
      };

      types.cache = (type, ...args) => {
        if(isNumber(type)) type = EntityNames[type];

        const proto = obj[type];

        return (proto.constructor.cache ?? MakeCache((...a) => new proto.constructor(...a)))(...args);
      };
    }

    for(let i = 0; i < EntityNames.length; i++) {
      const name = EntityNames[i];

      result[name] = {
        new: (...args) => types(name, ...args),
        cache: (...args) => types.cache(name, ...args),
      };
    }

    return Object.setPrototypeOf(result, Factory.prototype);
  }

  return factories(types);
}

Object.setPrototypeOf(Factory.prototype, Function.prototype);

define(Factory, {
  for: node => {
    let parent = node;

    do {
      const factory = factories(Node.raw(parent));

      if(factory) return factory;
    } while((parent = Node.parent(parent) ?? Node.owner(parent)));

    for(let n of Node.hier(node)) {
      const factory = factories(Node.raw(n));

      if(factory) return factory;
    }
  },
  set: (node, factory) => factories(Node.raw(node), factory),
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
          children: data,
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

  if(type == Entities.Text && isString(obj) && isFunction(owner.indexOf)) obj = owner.indexOf(obj);

  factory ??= Factory.for(owner);

  if(!factory) console.log('No factory for', owner, owner.constructor.name);

  const ctor = factory(type);

  if(!ctor) throw new Error(`No such node type for ${obj[Symbol.inspect]()}`);
  if(ctor.cache) return ctor.cache(obj, owner);

  return ctor(obj, owner);
}

export class Interface {
  get parentNode() {
    let result = Node.parent(this);

    if(isObject(result) && !(result instanceof Node)) result = Node.owner(result);

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
    for(let parent, node = Node.parent(this); node; node = parent) {
      if(!(parent = Node.parent(node))) return node;

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
      const owner = Node.owner(this) ?? Node.owner(raw) ?? parentNode;

      if(index != -1 && children[index + 1]) return GetNode(children[index + 1], owner);
    }
  }

  get previousSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const children = Node.children(parentNode);
      const raw = Node.raw(this);

      const index = children.indexOf(raw);
      const owner = Node.owner(this) ?? Node.owner(raw) ?? parentNode;

      if(index != -1 && children[index - 1]) return GetNode(children[index - 1], owner);
    }
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

    const { parentNode, ownerElement } = this;

    ownerElements(node, this.childNodes);
    parentNodes(node, this);

    children.push(Node.raw(node));

    return node;
  }

  insertBefore(node, ref) {
    const { children } = Node.raw(this);

    const old = isObject(node) && node instanceof Node ? Node.raw(node) : node,
      before = isObject(ref) && ref instanceof Node ? Node.raw(ref) : ref;

    const index = children.indexOf(before);

    if(index == -1) index = children.length;

    children.splice(index, 0, old);
    return node;
  }

  removeChild(node) {
    const { children } = Node.raw(this);

    const old = isObject(node) && node instanceof Node ? Node.raw(node) : node;

    const index = children.indexOf(old);

    if(index == -1) throw new Error(`Node.removeChild no such child!`);

    children.splice(index, 1);
    return node;
  }

  replaceChild(newChild, oldChild) {
    const { children } = Node.raw(this);

    const old = isObject(oldChild) && oldChild instanceof Node ? Node.raw(oldChild) : oldChild,
      replacement = isObject(newChild) && newChild instanceof Node ? Node.raw(newChild) : newChild;

    const index = children.indexOf(old);

    if(index == -1) throw new Error(`Node.replaceChild no such child!`);

    children.splice(index, 1, replacement);
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
}

export class Node {
  constructor(obj, parent) {
    if(isObject(obj)) rawNode(this, obj);

    parentNodes(this, parent);
  }

  get path() {
    return Node.path(this);
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

    if(raw !== undefined) {
      rawNode(node, raw);
    } else {
      if(isObject(node) && Object.getPrototypeOf(node) == Object.prototype) return node;

      return rawNode(node) ?? undefined;
    }
  }

  static children(node) {
    return Node.raw(node)?.children;
  }

  static owner(node) {
    this.check(node);
    let owner, parent;

    if((parent = parentNodes(node)) && Array.isArray(parent.children)) return parent.children;

    const raw = Node.raw(node);

    if((parent = parentNodes(raw)) && Array.isArray(parent.children)) return parent.children;

    if((owner = ownerElements(node))) return owner;

    return ownerElements(raw);
  }

  static parent(node) {
    this.check(node);

    return parentNodes(node);
  }

  static document(node) {
    while(node) {
      if(node.nodeType == Node.DOCUMENT_NODE) return node;

      node = Node.parent(node);
    }
  }

  static hier(node, pred = node => true) {
    const result = [];

    this.check(node);

    let parent;

    do {
      if(pred(node)) result.unshift(node);

      parent = Node.owner(node) ?? Node.parent(node);

      if(result.indexOf(parent) != -1) throw new Error(`circular loop`);
    } while(parent && (node = parent));

    return result;
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

      if(!isNaN(+index)) index = +index;

      path.push(index);
      hier.shift();
    }

    return extend(
      path,
      {
        toString() {
          return this.join('.');
        },
      },
      { enumerable: false },
    );
  }

  static from(obj) {}
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
extend(Node.prototype, NODE_TYPES, { enumerable: false });
extend(Node.prototype, { [Symbol.toStringTag]: 'Node' }, { enumerable: false });

function MakeCache(ctor, store = new WeakMap()) {
  const [get, set] = getset(store);

  return (key, ...args) => {
    let value;

    if(!(value = get(key))) {
      value = ctor(key, ...args);
      set(key, value);
    }

    ownerElements(value, args[0]);
    return value;
  };
}

function MakeCache2(ctor, store = new WeakMap()) {
  const cache = memoize(key => [], store);

  return (id, owner) => {
    const textList = cache(owner);

    id = isNaN(+id) ? id : +id;

    textList[id] ??= ctor(id, owner);

    return textList[id];
  };
}

export class NodeList {
  constructor(obj, owner) {
    const isElement = prop => isString(prop) && !isNaN(+prop);
    const isList = prop => isElement(prop) || prop == 'length';

    const factory = Factory.for(owner);

    rawNode(this, obj);
    ownerElements(this, owner);

    const nodeList = new Proxy(this, {
      get: (target, prop, receiver) => {
        if(prop === Symbol.iterator && isFunction(NodeList.prototype[prop])) return NodeList.prototype[prop].bind(this);

        if(isList(prop)) {
          if(prop == 'length') return obj.length;
          if(prop >= 0 && prop < obj.length) return GetNode(obj[prop], nodeList, factory);
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
      getPrototypeOf: target => NodeList.prototype,
    });

    rawNode(nodeList, obj);
    ownerElements(nodeList, owner);

    return nodeList;
  }

  /*[Symbol.inspect](depth, opts) {
    return inspect([...this], depth + 1, { ...opts, customInspect: true }).substring(...(str[0] == '[' ? [str.indexOf('[') + 1, str.lastIndexOf(']')] : [0]));
  }*/
}

extend(
  NodeList.prototype,
  {
    [Symbol.toStringTag]: 'NodeList',
    *[Symbol.iterator]() {
      const factory = Factory.for(this);

      for(let node of Node.raw(this)) yield factory(isString(node) ? Entities.Text : Entities.Element)(node, this);
    },
  },
  { enumerable: false },
);

export function NamedNodeMap(obj, owner) {
  if(!this) return new NamedNodeMap(obj, owner);

  const isAttr = prop => isString(prop) && prop in obj;
  const wrapAttr = (value, prop) => new Attr([obj, prop], nodeMap);

  rawNode(this, obj);
  ownerElements(this, owner);

  const nodeMap = new Proxy(this, {
    get: (target, prop, receiver) => {
      if(prop == 'length') return Object.keys(obj).length;

      if(isString(prop)) {
        if(!isNaN(+prop)) {
          const keys = Object.keys(obj);

          if(prop >= 0 && prop < keys.length) return wrapAttr(obj[keys[+prop]], keys[+prop]);
        } else if(prop in obj) return wrapAttr(obj[prop], prop);
      }

      return Reflect.get(target, prop, receiver);
    },
    ownKeys: target => Object.keys(obj),
  });

  rawNode(nodeMap, obj);
  ownerElements(nodeMap, owner);

  return nodeMap;
}

Object.setPrototypeOf(NamedNodeMap.prototype, Array.prototype);

extend(
  NamedNodeMap.prototype,
  {
    constructor: NamedNodeMap,

    [Symbol.toStringTag]: 'NamedNodeMap',

    get path() {
      return Node.path(Node.owner(this)).concat(['attributes']);
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
      const raw = Node.raw(this);

      return raw[name];
    },

    *[Symbol.iterator]() {
      const { length } = this;

      for(let i = 0; i < length; i++) yield this.item(i);
    },

    [Symbol.inspect]() {
      const raw = Node.raw(this);
      let str = '';
      for(let attr in raw) str += ' \x1b[1;35m' + attr + '\x1b[1;36m="' + raw[attr] + '"\x1b[0m';
      return '{' + str + ' }';
    },
  },
  { enumerable: false },
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
      parent = parent.parentNode;
    } while(parent.nodeType != ELEMENT_NODE);

    return parent;
  }

  get attributes() {
    const factory = Factory.for(this.ownerDocument);

    if(!factory) console.log(this.constructor.name, 'No factory', this);

    return factory.NamedNodeMap.cache((Node.raw(this).attributes ??= {}), this);
  }

  get children() {
    const factory = Factory.for(this.ownerDocument);

    if(!factory) console.log(this.constructor.name, 'No factory', this);

    return factory.NodeList.cache((Node.raw(this).children ??= []), this);
  }

  get style() {
    const factory = Factory.for(this.ownerDocument);

    if(!factory) console.log(this.constructor.name, 'No factory', this);

    return factory.CSSStyleDeclaration.cache((Node.raw(this).attributes ??= {}), this);
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
    const texts = [];

    for(let value of iterate(Node.raw(this), undefined, RETURN_VALUE, TYPE_STRING, ['children'])) texts.push(value.replace(/<s+/g, ' '));

    return texts.join(' ');
  }

  [Symbol.inspect](depth, opts) {
    const { tagName, attributes, children } = this;
    const attrs = attributes[Symbol.inspect](depth + 1, opts)
      .slice(1, -1)
      .trim();
    let str = `<${tagName}`;
    if(attrs) str += ' ' + attrs;
    str += '>';
    try {
      const tmp = children[Symbol.inspect](depth + 1, opts);
      if(tmp) str += tmp + `</${tagName}>`;
    } catch(err) {}
    return str;
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Element.prototype, Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }

  static cache = MakeCache((obj, owner) => new Element(obj, owner));

  static attributes(elem) {
    return modifier(Node.raw(elem), 'attributes');
  }

  static toString(elem) {
    return elem[Symbol.inspect](0, {}).replace(/\x1b\[[^a-z]*[a-z]/g, '');
  }
}

extend(Element.prototype, { [Symbol.toStringTag]: 'Element', nodeType: ELEMENT_NODE }, { enumerable: false });

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

    if(/^body$/i.test(element.tagName)) return element;
  }

  /*[Symbol.inspect](depth, opts) {
    const { tagName, attributes, children } = this;

    return `\x1b[1;31mDocument\x1b[0m ${tagName} attributes: ${attributes[Symbol.inspect](depth + 1, opts)} children: ${children.reduce((acc, c) => acc + c[Symbol.inspect](depth + 2, opts), '')}>`;
  }*/

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }
}

extend(Document.prototype, { [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE }, { enumerable: false });

export class Attr extends Node {
  constructor(raw, owner) {
    super(raw, owner);

    rawNode(this, raw);
    ownerElements(this, owner);

    define(this, {});
  }

  get path() {
    const { ownerElement } = this;
    const [, name] = Node.raw(this);

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

  set name(value) {
    Node.raw(this)[1] = value;
  }

  get value() {
    const [obj, name] = Node.raw(this);

    return obj[name];
  }

  set value(value) {
    const [obj, name] = Node.raw(this);

    obj[name] = value;
  }

  [Symbol.inspect]() {
    const [obj, name] = Node.raw(this);
    return `\x1b[1;35m${name}\x1b[1;34m=${quote(obj[name], '"')}\x1b[0m`;
  }
}

extend(
  Attr.prototype,
  {
    nodeType: ATTRIBUTE_NODE,
    [Symbol.toStringTag]: 'Attr',
  },
  { enumerable: false },
);

export class Text extends Node {
  static store = gettersetter(rawNode);

  constructor(key, owner) {
    super(owner ? key : null, owner);

    let get, set;

    if(owner) {
      const raw = owner instanceof NodeList ? Node.raw(owner) : owner;

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

extend(
  Text.prototype,
  {
    nodeType: TEXT_NODE,
    nodeName: '#text',
    [Symbol.toStringTag]: 'Text',
  },
  { enumerable: false },
);

const Tag = gettersetter(new WeakMap());

export class Comment extends Node {
  constructor(raw, owner) {
    super(raw, owner);

    ownerElements(this, owner);

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

extend(
  Comment.prototype,
  {
    nodeType: COMMENT_NODE,
    nodeName: '#comment',
    [Symbol.toStringTag]: 'Comment',
  },
  { enumerable: false },
);

const Tokens = gettersetter(new WeakMap());

export class TokenList {
  constructor(owner, key = 'class') {
    ownerElements(this, owner);

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

      for(let token of tokens) {
        if((index = arr.indexOf(token)) == -1) arr.push(token);
      }

      set(arr);
    });
  }

  remove(...tokens) {
    Tokens(this)((arr, set) => {
      let index;

      for(let token of tokens) {
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
    return 'TokenList [' + Tokens(this)().join(',') + ']';
  }
}

extend(
  TokenList.prototype,
  {
    [Symbol.toStringTag]: 'TokenList',
  },
  { enumerable: false },
);

const tokenListFacade = arrayFacade({}, (container, i) => container.item(i));

extend(TokenList.prototype, tokenListFacade, { enumerable: false });

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
        for(let k in this.styles) delete this.styles[k];
      },
      *keys() {
        for(let k in this.styles) yield k;
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
      ownKeys(target) {
        return [...impl.keys()].map(k => camelize(k));
      },
    });

    for(let lnk of [this, obj]) {
      styleImpl(lnk, impl);
      rawNode(lnk, style);

      if(isObject(owner)) ownerElements(lnk, owner);
    }

    return obj;
  }

  setProperty(k, v) {
    styleImpl(this).set(k, v);
  }

  item(index) {
    let i = 0;

    for(let k of styleImpl(this).keys()) if(i++ == index) return k;
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

extend(
  CSSStyleDeclaration.prototype,
  {
    constructor: CSSStyleDeclaration,

    [Symbol.toStringTag]: 'CSSStyleDeclaration',

    get parentRule() {
      return null;
    },
    get cssFloat() {
      return '';
    },
  },
  { enumerable: false },
);

export class Serializer {
  serializeToString(node) {
    return writeXML(Node.raw(node));
  }
}

export function GetType(raw, owner, ctor) {
  if(Array.isArray(raw)) return Entities.NodeList;
  if(isObject(raw) && 'tagName' in raw) return /^!--.*--$/.test(raw.tagName) ? Entities.Comment : Entities.Element;

  if(isString(raw)) return Entities.Text;
  if(isObject(raw)) return Entities.NamedNodeMap;
}

export { URLSearchParams, URL } from './url.js';
