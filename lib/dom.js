import { Pointer } from 'pointer';
import * as deep from 'deep';
import { isObject, define } from 'util';

export function Node(obj, parent, pointer, proto) {
  if(!(this instanceof Node)) return new Node(obj, parent, pointer, proto);
  pointer ??= new Pointer();

  const isElement = prop => prop in obj; /*|| ['tagName', 'attributes', 'children'].indexOf(prop) != -1*/
  const wrapObject = (value, prop) => (Array.isArray(value) ? new NodeList(value, this, pointer.concat([prop])) : value);

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });
  Object.assign(this, { path: pointer });
  return Membrane(this, obj, proto, isElement, wrapObject);
}

Node.prototype[Symbol.toStringTag] = Node.name;

export function NodeList(obj, parent, pointer, proto) {
  if(!(this instanceof NodeList)) return new NodeList(obj, parent, pointer, proto);

  pointer ??= new Pointer();

  const isElement = prop => typeof prop == 'string' && !isNaN(+prop);
  const isList = prop => isElement(prop) || prop == 'length';
  const wrapElement = value => (isObject(value) && 'tagName' in value ? new Element(value, this) : value);

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });
  define(this, { path: pointer });

  return Membrane(this, obj, proto, isList, wrapElement);
}

NodeList.prototype.__proto__ = Array.prototype;
NodeList.prototype[Symbol.toStringTag] = NodeList.name;

export function NamedNodeMap(obj, parent, pointer, proto) {
  if(!(this instanceof NamedNodeMap)) return new NamedNodeMap(obj, parent, pointer, proto);

  pointer ??= new Pointer();

  const isElement = prop => typeof prop == 'string' && !isNaN(+prop);
  const isList = prop => isElement(prop) || prop == 'length';
  const wrapElement = (value, prop) => (isObject(value) && 'tagName' in value ? new Element(value, this, pointer.concat([prop])) : value);

  if(isObject(parent)) define(this, { document: parent instanceof Document ? parent : parent.document });

  define(this, { path: pointer });
  return Membrane(this, obj, proto, isList, wrapElement);
}

NamedNodeMap.prototype.__proto__ = Array.prototype;
NamedNodeMap.prototype[Symbol.toStringTag] = NamedNodeMap.name;

export function Document(obj) {
  if(!(this instanceof Document)) return new Document(obj);

  return Node.call(this, obj, null, new Pointer(), Document.prototype);
}

Document.prototype.__proto__ = Node.prototype;
Document.prototype[Symbol.toStringTag] = Document.name;

export function Element(obj, parent, pointer) {
  if(!(this instanceof Element)) return new Element(obj, parent, pointer);

  pointer ??= new Pointer();

  return Node.call(this, obj, parent, pointer, Element.prototype);
}

Element.prototype.__proto__ = Node.prototype;
Element.prototype[Symbol.toStringTag] = Element.name;

function Membrane(instance, obj, proto, wrapProp, wrapElement) {
  return new Proxy(instance, {
    get: (target, prop, receiver) => (wrapProp(prop) ? wrapElement(obj[prop], prop) : Reflect.get(target, prop, receiver)),
    has: (target, prop) => (wrapProp(prop) ? true : Reflect.has(target, prop)),
    getOwnPropertyDescriptor: (target, prop) => (wrapProp(prop) ? { configurable: true, enumerable: true, writable: true, value: wrapElement(obj[prop], prop) } : Reflect.getOwnPropertyDescriptor(target, prop)),
    getPrototypeOf: target => proto ?? Object.getPrototypeOf(instance),
    setPrototypeOf: (target, p) => (proto = p),
    ownKeys: target => [...Reflect.ownKeys(target), ...Object.getOwnPropertyNames(obj)]
  });
}
