//import { Pointer } from 'pointer';
import * as deep from 'deep';
import inspect from 'inspect';
import { TreeWalker, TreeIterator } from 'tree_walker';
import { isObject, define, escape, quote, range, assert, memoize, getClassName, getClassID, mapObject, getset, modifier, getter, setter, gettersetter, hasGetSet, lazyProperty, arrayFacade } from './util.js';
import * as xml from 'xml';

//const Path = Pointer;

const inspectSymbol = Symbol.inspect ?? Symbol.for('quickjs.inspect.custom');

const rawNode = new WeakMap();
const parentNode = new Map();
const ownerElement = new Map();
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

const keyOf = (obj, value) => {
  for(let key in obj) if(obj[key] === value) return key;
  return -1;
};

export const nodeTypes = [
  ,
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
  if(!node[Symbol.toStringTag]) throw new TypeError('node does not have [Symbol.toStringTag]');
  if(Object.getPrototypeOf(node) == Object.prototype) throw new TypeError('node is Object');
}

export class Parser {
  parseFromString(str, file, ...rest) {
    let data = xml.read(str, file, ...rest);

    if(Array.isArray(data)) {
      if(data.length > 1)
        data = {
          tagName: '?xml',
          attributes: { version: '1.0', encoding: 'utf-8' },
          children: data
        };
      else data = data[0];
    }
    return new Document(data);
  }
}

function GetNode(obj, owner) {
  let type = { object: Element, string: Text }[typeof obj];

  if(type === Element && /^!--.*--$/.test(obj.tagName)) type = Comment;
  else if(type === Text && typeof obj == 'string') {
    console.log('GetNode', { type, obj, owner: owner.constructor.name });
    obj = owner.indexOf(obj);
  }

  if(!type || !type.cache) throw new Error(`No such node type for ${inspect(obj)}`);

  return type.cache(obj, owner);
}

export class Node {
  constructor(obj, parent, proto = Node.prototype) {
    if(isObject(proto)) assert(Object.getPrototypeOf(this) instanceof Node, true, 'proto');
    assert(this instanceof Node, true, 'instanceof Node');

    //if(!isObject(obj)) console.log('Node.constructor', { obj, parent, proto });
    if(isObject(obj)) rawNode.set(this, obj);

    parentNode.set(this, parent);
    //parentNode.set(obj, parent);
  }

  get path() {
    return Node.path(this);
  }
  f;
  get parentNode() {
    let r = Node.parent(this);
    if(isObject(r) && !(r instanceof Node)) r = Node.owner(r);
    return r;
  }

  get parentElement() {
    let obj = this.parentNode;
    return obj.nodeType == ELEMENT_NODE ? obj : null;
  }

  get ownerDocument() {
    let node = this;
    while(node && node.nodeType != DOCUMENT_NODE) node = node.parentNode;
    return node;
  }

  get childNodes() {
    const raw = Node.raw(this);
    return Factory((raw.children ??= []), this, NodeList);
  }

  get firstChild() {
    const { children } = Node.raw(this);
    if(children?.length) {
      let [first] = children;
      return GetNode(first, this);
    }
  }

  get lastChild() {
    const { children } = Node.raw(this);
    if(children?.length) {
      let last = children[children.length - 1];
      return GetNode(last, this);
    }
  }

  get nextSibling() {
    let { children } = Node.raw(this.parentNode);

    if(children) {
      let raw = Node.raw(this);
      //console.log('children', children);
      let index = children.indexOf(raw);
      if(index != -1) return GetNode(children[index + 1], Node.owner(this));
    }
  }

  get previousSibling() {
    let parent;
    if((parent = Node.parent(this))) {
      let index = parent.indexOf(this);
      if(index != -1) return GetNode(parent[index - 1], parent);
    }
  }

  appendChild(node) {
    const { children } = Node.raw(this);
    let raw = Node.raw(node);
    if(node instanceof Text) {
      let key = children.length;
      textValues(
        this,
        modifier(
          () => children[key] ?? '',
          value => (children[key] = value)
        )
      );
    }
    ownerElement.set(node, this.childNodes);
    parentNode.set(node, this);

    children.push(raw);
    return node;
  }

  insertBefore(node, refNode) {
    const { children } = Node.raw(this);
    let index,
      raw = isObject(node) && node instanceof Node ? Node.raw(node) : node,
      ref = isObject(refNode) && refNode instanceof Node ? Node.raw(refNode) : refNode;
    if((index = children.indexOf(ref)) == -1) index = children.length;
    children.splice(index, 0, raw);
    return node;
  }

  removeChild(node) {
    const { children } = Node.raw(this);
    let index,
      raw = isObject(node) && node instanceof Node ? Node.raw(node) : node;
    if((index = children.indexOf(raw)) == -1) throw new Error(`Node.removeChild no such child!`);
    children.splice(index, 1);
    return node;
  }

  replaceChild(newChild, oldChild) {
    const { children } = Node.raw(this);
    let index,
      old = isObject(oldChild) && oldChild instanceof Node ? Node.raw(oldChild) : oldChild,
      raw = isObject(newChild) && newChild instanceof Node ? Node.raw(newChild) : newChild;
    if((index = children.indexOf(old)) == -1) throw new Error(`Node.replaceChild no such child!`);
    children.splice(index, 1, raw);
    return newChild;
  }

  [inspectSymbol]() {
    return `\x1b[1;31m${this.constructor.name}\x1b[0m`;
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
define(Node, {
  raw(node) {
    checkNode(node);
    return rawNode.get(node);
  },

  children(node) {
    return Node.raw(node)?.children;
  },

  owner(node) {
    checkNode(node);
    let owner, parent;
    if((parent = parentNode.get(node)) && Array.isArray(parent.children)) return parent.children;

    let raw = Node.raw(node);
    if((parent = parentNode.get(raw)) && Array.isArray(parent.children)) return parent.children;

    if((owner = ownerElement.get(node))) return owner;
    return ownerElement.get(raw);
  },

  parent(node) {
    checkNode(node);
    let ret = parentNode.get(node);
    return ret;
  },

  hier(node, pred = node => true) {
    const r = [];
    checkNode(node);
    let next;
    do {
      if(pred(node)) r.unshift(node);
      next = (
        { NodeList: Node.owner, NamedNodeMap: Node.owner, Element: Node.owner, Text: Node.owner }[
          node.constructor.name
        ] ?? Node.parent
      ).call(Node, node);
    } while(next && (node = next));
    return r;
  },

  document(node) {
    const hier = Node.hier(node);
    return hier.find(({ nodeType }) => nodeType == DOCUMENT_NODE);
  },

  path(arg, path = []) {
    if(arg.ownerElement)
      return Node.path(arg.ownerElement).concat(
        { NodeList: ['children'], NamedNodeMap: ['attributes'], Attr: ['attributes', arg.name] }[arg.constructor.name]
      );
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
});

Node.prototype[Symbol.toStringTag] = 'Node';

function MakeCache(ctor, store = new WeakMap()) {
  let [get, set] = getset(store);
  return (k, ...args) => {
    let v;
    if(!(v = get(k))) {
      v = ctor(k, ...args);
      set(k, v);
    }
    ownerElement.set(v, args[0]);
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
    const wrapElement = (value, prop) =>
      typeof value == 'string' ? Text.cache(prop, nodeList) : Element.cache(value, nodeList);

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
        if(isList(prop)) {
          if(prop == 'length' || (prop >= 0 && prop < obj.length))
            return { configurable: true, enumerable: true, value: obj[prop] };
        }
        return Reflect.getOwnPropertyDescriptor(target, prop);
      },
      ownKeys: target =>
        range(0, obj.length - 1)
          .map(prop => prop + '')
          .concat(['length'])
      //    getPrototypeOf: target => NodeList.prototype
    });

    rawNode.set(nodeList, obj);
    ownerElement.set(nodeList, owner);

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
  get path() {
    let owner = Node.owner(this);
    return Node.path(owner).concat(['children']);
  },

  /*  [inspectSymbol](depth, opts) {
    const raw = Node.raw(this);
    return util
      .range(0, raw.length - 1)
      .map(n => inspect(this[n], depth-1,opts)) 
      .join(',\n  ');
  },*/

  *[Symbol.iterator]() {
    const raw = Node.raw(this);
    for(let node of raw) yield Factory(node, this, typeof node == 'string' ? Text : Element);
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

    //lazyProperty(this, 'classList', () => new TokenList('class', this));
    define(this, { classList: new TokenList(this, 'class') });
  }

  get parentElement() {
    let obj = this;
    do {
      obj = obj.parentNode;
    } while(obj.nodeType != ELEMENT_NODE);
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
  set nodeName(v) {
    this.tagName = v;
  }
  get nodeName() {
    return this.tagName;
  }

  /*  setAttribute(name, value) {
    (Node.raw(this).attributes ??= {})[name] = value;
  }
  removeAttribute(name) {
    delete (Node.raw(this).attributes ??= {})[name];
  }
*/
  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    const a = attributes[inspectSymbol](depth + 1, opts)
      .slice(1, -1)
      .trim();
    const c = children[inspectSymbol](depth + 1, opts);
    let s = `<${tagName}`;
    if(a) s += ' ' + a;
    s += '>';
    if(c) s += `${c}</${tagName}>`;
    return s;
  }

  get children() {
    const raw = Node.raw(this);
    // console.log('raw',inspect(raw, {depth: 0}));
    return Factory((raw.children ??= []), this, NodeList);
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
    if(children?.length)
      for(let i = children.length - 1; i >= 0; i--)
        if(isObject(children[i]) && 'tagName' in children[i]) return Element.cache(children[i], this.children);
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
    Element.attributes(this)(attributes => (attributes[name] = value));
  }

  static cache = MakeCache((obj, owner) => new Element(obj, owner));
}

define(Element, {
  attributes(elem) {
    return modifier(Node.raw(elem), 'attributes');
  }
});

//define(Element, { cache: MakeCache(Element) });
define(Element.prototype, { [Symbol.toStringTag]: 'Element', nodeType: ELEMENT_NODE });

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
  constructor(obj) {
    super(obj, null, Document.prototype);
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

  createTreeWalker(root, pred = (n, k) => true) {
    const raw = Node.raw(root);
    let walker;
    walker = new TreeWalker(
      raw,
      (n, k) => typeof k == 'number' && pred(n, k),
      (n, p) => GetNode(n, deep.get(raw, p.slice(0, -1)))
    );
    return walker;
  }

  get body() {
    let element = this.lastElementChild.lastElementChild;
    if(/^body$/i.test(element.tagName)) return element;
  }

  /*  [inspectSymbol](depth, opts) {
    const { tagName, attributes, children } = this;
    console.log('Document.inspect')
    return `\x1b[1;31mDocument\x1b[0m ${tagName} attributes: ${attributes[inspectSymbol](depth + 1, opts)} children: ${inspect(children.map(c => c[inspectSymbol](depth + 2, opts)))}>`;
  }*/
}

define(Document.prototype, { [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE });

export class Attr extends Node {
  constructor(raw, owner) {
    // console.log('Attr', { raw, owner });
    super(raw, owner);

    rawNode.set(this, raw);
    ownerElement.set(this, owner);

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

    ownerElement.set(this, owner);

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
    ownerElement.set(this, owner);

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

function Membrane(instance, obj, proto, wrapProp, wrapElement) {
  throw new Error('Membrane');
  return new Proxy(instance, {
    get: (target, prop, receiver) =>
      wrapProp(prop) ? wrapElement(obj[prop], prop) : Reflect.get(target, prop, receiver),
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
  if(isObject(raw) && (obj = map.get(raw))) {
    ownerElement.set(obj, owner);
    return obj;
  }

  const isArray = arg => Array.isArray(arg);
  const isAttributes = arg => isObject(arg) && !isArray(arg) && !('tagName' in arg);

  ctor ??= isArray(raw) ? NodeList : isAttributes(raw) ? NamedNodeMap : typeof raw == 'string' ? Text : Element;

  obj = new ctor(raw, owner);
  return obj;
}
