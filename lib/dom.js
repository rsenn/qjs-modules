import { Pointer } from 'pointer';
import * as deep from 'deep';
import inspect from 'inspect';
import { isObject, define, escape, quote } from 'util';

const Path = Pointer;

export function Node(obj, parent, pointer, proto) {
  if(!(this instanceof Node)) return new Node(obj, parent, pointer, proto);
  pointer ??= new Path();

  const isElement = prop =>
    prop in
    (typeof obj == 'string' ? String.prototype : obj); /*|| ['tagName', 'attributes', 'children'].indexOf(prop) != -1*/
  const wrapObject = (value, prop) =>
    ((
      {
        children: () => new NodeList(value, this, pointer.concat([prop])),
        attributes: () => new NamedNodeMap(obj.attributes, this, pointer.concat([prop]))
      }[prop] ?? (() => value)
    )());

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });
  define(this, {
    path: pointer,
    get parentNode() {
      return parent;
    },
    get childElementCount() {
      return obj.children?.length ?? 0;
    },
    get firstChild() {
      return obj.children[0];
    },
    get lastChild() {
      return obj.children[obj.children.length - 1];
    },
    get firstElementChild() {
      if(obj.children) return obj.children.find(n => n.nodeType == this.ELEMENT_NODE);
    },
    get lastElementChild() {
      const n = this.childElementCount;
      for(let i = n - 1; i >= 0; i--) {
        if(obj.children[i].nodeType == this.ELEMENT_NODE) return obj.children[i];
      }
    }
  });
  return Membrane(this, obj, proto, isElement, wrapObject);
}

define(Node.prototype, {
  ATTRIBUTE_NODE: 2,
  CDATA_SECTION_NODE: 4,
  COMMENT_NODE: 8,
  DOCUMENT_FRAGMENT_NODE: 11,
  DOCUMENT_NODE: 9,
  DOCUMENT_TYPE_NODE: 10,
  ELEMENT_NODE: 1,
  ENTITY_NODE: 6,
  ENTITY_REFERENCE_NODE: 5,
  NOTATION_NODE: 12,
  PROCESSING_INSTRUCTION_NODE: 7,
  TEXT_NODE: 3
});

Node.prototype[Symbol.toStringTag] = Node.name;

export function NodeList(obj, parent, pointer, proto) {
  if(!(this instanceof NodeList)) return new NodeList(obj, parent, pointer, proto);

  pointer ??= new Path();

  const isElement = prop => typeof prop == 'string' && !isNaN(+prop);
  const isList = prop => isElement(prop) || prop == 'length';
  const wrapElement = (value, prop) =>
    typeof value == 'string'
      ? new Text(value, this, pointer.concat([obj.indexOf(value)]))
      : isObject(value) && 'tagName' in value
      ? new Element(value, this, pointer.concat([obj.indexOf(value)]))
      : value;

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });
  define(this, { path: pointer });

  return Membrane(this, obj, proto, isList, wrapElement);
}

NodeList.prototype.__proto__ = Array.prototype;
NodeList.prototype[Symbol.toStringTag] = NodeList.name;

export function NamedNodeMap(obj, parent, pointer, proto) {
  if(!(this instanceof NamedNodeMap)) return new NamedNodeMap(obj, parent, pointer, proto);

  pointer ??= new Path();

  const isAttr = prop => prop in obj;
  const wrapAttr = (value, prop) => new Attr(obj, prop, this, pointer.concat([prop]));

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });

  define(this, {
    path: pointer,
    setNamedItem(attr) {
      const { name, value } = attr;
      obj[name] = value;
    },
    removeNamedItem(name) {
      delete obj[name];
    }
  });
  return Membrane(this, obj, proto, isAttr, wrapAttr);
}

NamedNodeMap.prototype.__proto__ = Array.prototype;
define(NamedNodeMap.prototype, {
  item(i) {
    let keys = Object.getOwnPropertyNames(this);
    return this[keys[i]];
  },
  get length() {
    let keys = Object.getOwnPropertyNames(this);
    return keys.length;
  },
  getNamedItem(name) {
    return this[name];
  },
  *[Symbol.iterator]() {
    const { length } = this;
    for(let i = 0; i < length; i++) yield this.item(i);
  },
  [Symbol.toStringTag]: NamedNodeMap.name,
  [Symbol.for('quickjs.inspect.custom')]() {
    let s = '';
    for(let attr of this) {
      s += ' \x1b[1;35m' + attr.name + '\x1b[1;36m="' + attr.value + '"\x1b[0m';
    }
    return /*`\0x1b[1;31m${this[Symbol.toStringTag]}\x1b[0m ` +*/ '{' + s + ' }';
    return Object.getOwnPropertyNames(this).reduce((acc, prop) => ({ ...acc, [prop]: this[prop] }), {});
  }
});

export function Document(obj) {
  if(!(this instanceof Document)) return new Document(obj);

  return Node.call(this, obj, null, new Path(), Document.prototype);
}

Document.prototype.__proto__ = Node.prototype;
Document.prototype[Symbol.toStringTag] = Document.name;

define(Document.prototype, {
  createAttribute(name, value) {
    return new Attr(null, name, null);
  }
});

export function Element(obj, parent, pointer) {
  if(!(this instanceof Element)) return new Element(obj, parent, pointer);

  pointer ??= new Path();

  define(this, {
    nodeType: this.ELEMENT_NODE,
    get parentElement() {
      let obj = parent;
      while(isObject(obj)) {
        if(obj instanceof Element || obj.nodeType == this.ELEMENT_NODE) break;
        obj = obj.parent;
      }
      return obj;
    },
    setAttribute(name, value) {
      obj.attributes ??= {};
      obj.attributes[name] = value;
    },
    removeAttribute(name) {
      delete obj.attributes[name];
    }
  });

  return Node.call(this, obj, parent, pointer, Element.prototype);
}

Element.prototype.__proto__ = Node.prototype;
Element.prototype[Symbol.toStringTag] = Element.name;

define(Element.prototype, {
  /* [Symbol.for('quickjs.inspect.custom')](depth) {
    const { tagName, attributes, children, path, parentElement } = this;
    let obj = {};
    if(attributes) obj.attributes = attributes;
    if(children) obj.children = children;
    return inspect(
      {
        ...obj,
        [Symbol.toStringTag]: Element.name
      },
      { colors: true, inspectCustom: false, compact: 1, depth: depth - 1 }
    );
  }*/
});

export function Attr(obj, prop, parent, pointer) {
  if(!(this instanceof Attr)) return new Attr(obj, prop, parent, pointer);

  pointer ??= new Path();

  define(this, {
    nodeType: this.ATTRIBUTE_NODE,
    get name() {
      return prop;
    },
    get value() {
      return obj[prop];
    },
    set value(v) {
      obj[prop] = v;
    }
  });

  return this; //Node.call(this, obj, parent, pointer, Attr.prototype);
}

Attr.prototype.__proto__ = Node.prototype;
define(Attr.prototype, {
  [Symbol.toStringTag]: Attr.name,
  [Symbol.for('quickjs.inspect.custom')]() {
    const { name, value } = this;
    return `\x1b[1;35m${name}\x1b[1;34m=${quote(value, '"')}\x1b[0m`;
  }
});
export function Text(value, parent, pointer) {
  if(!(this instanceof Text)) return new Text(value, parent, pointer);

  pointer ??= new Path();

  define(this, { nodeType: this.TEXT_NODE, value });

  return Node.call(this, value, parent, pointer, Text.prototype);
}

Text.prototype.__proto__ = Node.prototype;
define(Text.prototype, {
  [Symbol.toStringTag]: Text.name,
  [Symbol.for('quickjs.inspect.custom')]() {
    return `\x1b[1;32m'${escape(this.value)}'\x1b[0m`;
  }
});

function Membrane(instance, obj, proto, wrapProp, wrapElement) {
  return new Proxy(instance, {
    get: (target, prop, receiver) =>
      wrapProp(prop) ? wrapElement(obj[prop], prop) : Reflect.get(target, prop, receiver),
    has: (target, prop) => (wrapProp(prop) ? true : Reflect.has(target, prop)),
    getOwnPropertyDescriptor: (target, prop) =>
      wrapProp(prop)
        ? { configurable: true, enumerable: true, writable: true, value: wrapElement(obj[prop], prop) }
        : Reflect.getOwnPropertyDescriptor(target, prop),
    getPrototypeOf: target => proto ?? Object.getPrototypeOf(instance),
    setPrototypeOf: (target, p) => (proto = p),
    ownKeys: target => [/*...Reflect.ownKeys(target), */ ...Object.getOwnPropertyNames(obj)]
  });
}
