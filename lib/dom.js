import { Pointer } from 'pointer';
import * as deep from 'deep';

export function Node(obj,parent, proto) {
  if(!(this instanceof Node)) return new Node(obj);

  const isElement = prop => prop in obj || ['tagName', 'attributes', 'children'].indexOf(prop) != -1;

  return new Proxy(this, {
    get: (target, prop, receiver) => (isElement(prop) ? obj[prop] : Reflect.get(target, prop, receiver)),
    has: (target, prop) => (isElement(prop) ? true : Reflect.has(target, prop)),
    getOwnPropertyDescriptor: (target, prop) => (isElement(prop) ? { configurable: true, enumerable: true, writable: true, value: obj[prop] } : Reflect.getOwnPropertyDescriptor(target, prop)),
    getPrototypeOf: target => proto ?? Object.getPrototypeOf(this),
    setPrototypeOf: (target, p) => (proto = p),
    ownKeys: target => [...Reflect.ownKeys(target), 'tagName', 'attributes', 'children']
  });
}
Node.prototype[Symbol.toStringTag] = Node.name;

export function Document(obj) {
  if(!(this instanceof Document)) return new Document(obj);

  return Node.call(this, obj, null, Document.prototype);
}

Document.prototype.__proto__ = Node.prototype;
Document.prototype[Symbol.toStringTag] = Document.name;

export function Element(obj, parent) {
  if(!(this instanceof Element)) return new Element(obj);

  return Node.call(this, obj, parent,Element.prototype);
}

Element.prototype.__proto__ = Node.prototype;
Element.prototype[Symbol.toStringTag] = Element.name;
