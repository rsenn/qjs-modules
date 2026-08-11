import { readFileSync } from 'fs';
import { arrayFacade, camelize, className, decamelize, decodeHTMLEntities, define, extend, getset, getter, gettersetter, isBool, isFunction, isInstanceOf, isNumber, isNumeric, isObject, isPropertyKey, isPrototypeOf, isString, mapObject, memoize, modifier, nonenumerable, properties, queueMicrotask, quote, range, types, weakMapper, } from 'util';
import { setTimeout as _setTimeout, clearTimeout as _clearTimeout, setInterval as _setInterval, clearInterval as _clearInterval } from './timers.js';
import { parseSelectors } from './css3-selectors.js';
import { clone, FILTER_KEY_OF, FILTER_NEGATE, find, get, iterate, PATH_AS_POINTER, RECURSE, RETURN_PATH, RETURN_VALUE, TYPE_OBJECT, TYPE_STRING, YIELD_NO_RECURSE } from 'deep';
import { DereferenceError, Pointer } from 'pointer';
import { TreeWalker } from 'tree_walker';
import { read as readXML, write as writeXML } from 'xml';
import { URL } from 'url';
import { File } from './file.js';
export { File } from './file.js';

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

const DEBUG = (() => {
  // Safely access process.env.DEBUG if available
  let debugEnv = '';
  try {
    if(typeof process !== 'undefined' && process.env) {
      debugEnv = process.env.DEBUG ?? '';
    }
  } catch(e) {
    // process not available, use empty string
  }
  const { length } = [...debugEnv.matchAll(/\bdom\b/gi)];
  return length > 0 ? (...args) => console.log('\x1b[1;33mDOM\x1b[0m', console.config({ depth: 1, compact: true }), ...args) : () => {};
})();

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

const EntityNames = [
  'Document',
  'Node',
  'NodeList',
  'Element',
  'NamedNodeMap',
  'Attr',
  'Text',
  'Comment',
  'TokenList',
  'CSSStyleDeclaration',
  'HTMLCollection',
  'Event',
  'EventTarget',
  'DocumentFragment',
  'HTMLElement',
];
const EntityType = name => EntityNames.indexOf(name);
const TypeName = n => (isNumber(n) ? EntityNames[n] : n);

export const Entities = EntityNames.reduce((obj, name, id) => ({ [name]: id, ...obj }), {});

export class DOMException extends Error {
  constructor(message, name) {
    super(message ?? 'DOMException');

    if(name) define(this, nonenumerable({ name }));
  }
}

extend(DOMException.prototype, nonenumerable({ name: 'DOMException' }));

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

function* walk(root) {
  const raw = Node.raw(root) ?? rawNode(root);
  const it = iterate(raw, undefined, RETURN_PATH | FILTER_KEY_OF | FILTER_NEGATE, TYPE_OBJECT | TYPE_STRING, ['attributes', 'tagName']);

  for(let path of it) {
    yield path.reduce((o, k) => o[k], root);
  }
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
    Event,
    EventTarget,
    DocumentFragment,
    HTMLElement,
    File,
  };
}

export function Prototypes(constructors = Classes()) {
  const prototypes = {};
  for(const key in constructors) {
    prototypes[key] = constructors[key].prototype;

    if(constructors[key].name && constructors[key].name != prototypes[key][Symbol.toStringTag]) Object.assign(prototypes[key], { [Symbol.toStringTag]: constructors[key].name });
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

        if(type == 'Element' && ctor.elements) {
          const ctor2 = ctor.elements[args[0].tagName];
          //DEBUG('Factory', { type, raw: args[0], ctor2 });
          if(isFunction(ctor2)) ctor = ctor2;
          else if(ctor2?.constructor) ctor = ctor2.constructor;
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

    if(!factory) {
      throw new Error(`No factory for <${className(node)}> [[ ${node + ''} ]]`);
    }

    return factory;
  }

  static get(node) {
    let tmp;
    if((tmp = proxyFor(node))) node = tmp;

    if(!('nodeType' in node) && (tmp = ownerElements(node))) node = tmp;

    if(node.nodeType != DOCUMENT_NODE && (tmp = Node.document(node))) node = tmp;

    return factories(node) ?? factories(ownerDocument(node));
  }

  static set(node, factory) {
    let tmp;

    if((tmp = proxyFor(node))) node = tmp;

    if(!('nodeType' in node) && (tmp = ownerElements(node))) node = tmp;

    factories(node, factory);

    if(Node.document(node) !== node) this.set(Node.document(node), factory);
  }
}

Object.setPrototypeOf(Factory.prototype, function Factory(type) {});

extend(
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
    let data = readXML(str, file, { tolerant: true });

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

extend(Parser.prototype, nonenumerable({ [Symbol.toStringTag]: 'Parser' }));

export function GetType(raw) {
  if(Array.isArray(raw)) return Entities.NodeList;
  if(isComment(raw)) return Entities.Comment;
  if(isElement(raw)) return Entities.Element;
  if(isString(raw)) return Entities.Text;
  if(isObject(raw)) return Entities.NamedNodeMap;
}

export function GetNode(raw, owner, factory = Factory.for(owner)) {
  const type = GetType(raw);

  if(type == Entities.Text) {
    const rawOwner = Node.raw(owner);
    raw = rawOwner.indexOf(raw);
  }

  const ctor = factory[TypeName(type)];

  if(!ctor) throw new Error(`No such node type for ${raw}`);

  const node = ctor.cache ? ctor.cache(raw, owner) : ctor.new(raw, owner);

  //DEBUG('GetNode', { node, raw, owner });

  rawNode(node, raw);
  ownerElements(node, owner);

  return node;
}

/* ========== DOM Events ========== */

export class Event {
  #type;
  #bubbles;
  #cancelable;
  #composed;
  #defaultPrevented = false;
  #propagationStopped = false;
  #immediatePropagationStopped = false;
  #target = null;
  #currentTarget = null;
  #eventPhase = 0;
  #timeStamp;

  static NONE = 0;
  static CAPTURING_PHASE = 1;
  static AT_TARGET = 2;
  static BUBBLING_PHASE = 3;

  constructor(type, eventInitDict = {}) {
    this.#type = type;
    this.#bubbles = !!eventInitDict.bubbles;
    this.#cancelable = !!eventInitDict.cancelable;
    this.#composed = !!eventInitDict.composed;
    this.#timeStamp = Date.now();
  }

  get type() {
    return this.#type;
  }

  get bubbles() {
    return this.#bubbles;
  }

  get cancelable() {
    return this.#cancelable;
  }

  get composed() {
    return this.#composed;
  }

  get defaultPrevented() {
    return this.#defaultPrevented;
  }

  get target() {
    return this.#target;
  }

  get currentTarget() {
    return this.#currentTarget;
  }

  get eventPhase() {
    return this.#eventPhase;
  }

  get timeStamp() {
    return this.#timeStamp;
  }

  preventDefault() {
    if(this.#cancelable) this.#defaultPrevented = true;
  }

  stopPropagation() {
    this.#propagationStopped = true;
  }

  stopImmediatePropagation() {
    this.#propagationStopped = true;
    this.#immediatePropagationStopped = true;
  }

  /* internal: set by EventTarget.dispatchEvent */
  setTarget(target) {
    this.#target = target;
  }

  setCurrentTarget(currentTarget) {
    this.#currentTarget = currentTarget;
  }

  setEventPhase(phase) {
    this.#eventPhase = phase;
  }

  isPropagationStopped() {
    return this.#propagationStopped;
  }

  isImmediatePropagationStopped() {
    return this.#immediatePropagationStopped;
  }
}

extend(Event.prototype, nonenumerable({ [Symbol.toStringTag]: 'Event' }));

export class CustomEvent extends Event {
  #detail;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#detail = eventInitDict.detail !== undefined ? eventInitDict.detail : null;
  }

  get detail() {
    return this.#detail;
  }
}

extend(CustomEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'CustomEvent' }));

export class UIEvent extends Event {
  #view;
  #detail;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#view = eventInitDict.view ?? null;
    this.#detail = eventInitDict.detail ?? 0;
  }

  get view() {
    return this.#view;
  }

  get detail() {
    return this.#detail;
  }
}

extend(UIEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'UIEvent' }));

export class MouseEvent extends UIEvent {
  #screenX;
  #screenY;
  #clientX;
  #clientY;
  #ctrlKey;
  #shiftKey;
  #altKey;
  #metaKey;
  #button;
  #buttons;
  #relatedTarget;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#screenX = eventInitDict.screenX ?? 0;
    this.#screenY = eventInitDict.screenY ?? 0;
    this.#clientX = eventInitDict.clientX ?? 0;
    this.#clientY = eventInitDict.clientY ?? 0;
    this.#ctrlKey = !!eventInitDict.ctrlKey;
    this.#shiftKey = !!eventInitDict.shiftKey;
    this.#altKey = !!eventInitDict.altKey;
    this.#metaKey = !!eventInitDict.metaKey;
    this.#button = eventInitDict.button ?? 0;
    this.#buttons = eventInitDict.buttons ?? 0;
    this.#relatedTarget = eventInitDict.relatedTarget ?? null;
  }

  get screenX() {
    return this.#screenX;
  }

  get screenY() {
    return this.#screenY;
  }

  get clientX() {
    return this.#clientX;
  }

  get clientY() {
    return this.#clientY;
  }

  get pageX() {
    return this.#clientX;
  }

  get pageY() {
    return this.#clientY;
  }

  get offsetX() {
    return this.#clientX;
  }

  get offsetY() {
    return this.#clientY;
  }

  get ctrlKey() {
    return this.#ctrlKey;
  }

  get shiftKey() {
    return this.#shiftKey;
  }

  get altKey() {
    return this.#altKey;
  }

  get metaKey() {
    return this.#metaKey;
  }

  get button() {
    return this.#button;
  }

  get buttons() {
    return this.#buttons;
  }

  get relatedTarget() {
    return this.#relatedTarget;
  }

  getModifierState(key) {
    switch(key) {
      case 'Control':
        return this.#ctrlKey;
      case 'Shift':
        return this.#shiftKey;
      case 'Alt':
        return this.#altKey;
      case 'Meta':
        return this.#metaKey;
      default:
        return false;
    }
  }
}

extend(MouseEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'MouseEvent' }));

export class KeyboardEvent extends UIEvent {
  #key;
  #code;
  #location;
  #ctrlKey;
  #shiftKey;
  #altKey;
  #metaKey;
  #repeat;
  #isComposing;

  static DOM_KEY_LOCATION_STANDARD = 0;
  static DOM_KEY_LOCATION_LEFT = 1;
  static DOM_KEY_LOCATION_RIGHT = 2;
  static DOM_KEY_LOCATION_NUMPAD = 3;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#key = eventInitDict.key ?? '';
    this.#code = eventInitDict.code ?? '';
    this.#location = eventInitDict.location ?? 0;
    this.#ctrlKey = !!eventInitDict.ctrlKey;
    this.#shiftKey = !!eventInitDict.shiftKey;
    this.#altKey = !!eventInitDict.altKey;
    this.#metaKey = !!eventInitDict.metaKey;
    this.#repeat = !!eventInitDict.repeat;
    this.#isComposing = !!eventInitDict.isComposing;
  }

  get key() {
    return this.#key;
  }

  get code() {
    return this.#code;
  }

  get location() {
    return this.#location;
  }

  get ctrlKey() {
    return this.#ctrlKey;
  }

  get shiftKey() {
    return this.#shiftKey;
  }

  get altKey() {
    return this.#altKey;
  }

  get metaKey() {
    return this.#metaKey;
  }

  get repeat() {
    return this.#repeat;
  }

  get isComposing() {
    return this.#isComposing;
  }

  getModifierState(key) {
    switch(key) {
      case 'Control':
        return this.#ctrlKey;
      case 'Shift':
        return this.#shiftKey;
      case 'Alt':
        return this.#altKey;
      case 'Meta':
        return this.#metaKey;
      default:
        return false;
    }
  }
}

extend(KeyboardEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'KeyboardEvent' }));

export class FocusEvent extends UIEvent {
  #relatedTarget;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#relatedTarget = eventInitDict.relatedTarget ?? null;
  }

  get relatedTarget() {
    return this.#relatedTarget;
  }
}

extend(FocusEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'FocusEvent' }));

export class InputEvent extends UIEvent {
  #data;
  #inputType;
  #isComposing;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#data = eventInitDict.data ?? null;
    this.#inputType = eventInitDict.inputType ?? '';
    this.#isComposing = !!eventInitDict.isComposing;
  }

  get data() {
    return this.#data;
  }

  get inputType() {
    return this.#inputType;
  }

  get isComposing() {
    return this.#isComposing;
  }
}

extend(InputEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'InputEvent' }));

export class WheelEvent extends MouseEvent {
  #deltaX;
  #deltaY;
  #deltaZ;
  #deltaMode;

  static DOM_DELTA_PIXEL = 0;
  static DOM_DELTA_LINE = 1;
  static DOM_DELTA_PAGE = 2;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#deltaX = eventInitDict.deltaX ?? 0;
    this.#deltaY = eventInitDict.deltaY ?? 0;
    this.#deltaZ = eventInitDict.deltaZ ?? 0;
    this.#deltaMode = eventInitDict.deltaMode ?? 0;
  }

  get deltaX() {
    return this.#deltaX;
  }

  get deltaY() {
    return this.#deltaY;
  }

  get deltaZ() {
    return this.#deltaZ;
  }

  get deltaMode() {
    return this.#deltaMode;
  }
}

extend(WheelEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'WheelEvent' }));

export class Touch {
  #identifier;
  #target;
  #screenX;
  #screenY;
  #clientX;
  #clientY;
  #pageX;
  #pageY;

  constructor(touchInitDict = {}) {
    this.#identifier = touchInitDict.identifier ?? 0;
    this.#target = touchInitDict.target ?? null;
    this.#screenX = touchInitDict.screenX ?? 0;
    this.#screenY = touchInitDict.screenY ?? 0;
    this.#clientX = touchInitDict.clientX ?? 0;
    this.#clientY = touchInitDict.clientY ?? 0;
    this.#pageX = touchInitDict.pageX ?? 0;
    this.#pageY = touchInitDict.pageY ?? 0;
  }

  get identifier() {
    return this.#identifier;
  }

  get target() {
    return this.#target;
  }

  get screenX() {
    return this.#screenX;
  }

  get screenY() {
    return this.#screenY;
  }

  get clientX() {
    return this.#clientX;
  }

  get clientY() {
    return this.#clientY;
  }

  get pageX() {
    return this.#pageX;
  }

  get pageY() {
    return this.#pageY;
  }
}

extend(Touch.prototype, nonenumerable({ [Symbol.toStringTag]: 'Touch' }));

export class TouchList {
  #touches;

  constructor(touches = []) {
    this.#touches = [...touches];
  }

  get length() {
    return this.#touches.length;
  }

  item(index) {
    return this.#touches[index] ?? null;
  }

  [Symbol.iterator]() {
    return this.#touches[Symbol.iterator]();
  }
}

extend(TouchList.prototype, nonenumerable({ [Symbol.toStringTag]: 'TouchList' }));

export class TouchEvent extends UIEvent {
  #touches;
  #targetTouches;
  #changedTouches;
  #ctrlKey;
  #shiftKey;
  #altKey;
  #metaKey;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#touches = eventInitDict.touches ?? new TouchList();
    this.#targetTouches = eventInitDict.targetTouches ?? new TouchList();
    this.#changedTouches = eventInitDict.changedTouches ?? new TouchList();
    this.#ctrlKey = !!eventInitDict.ctrlKey;
    this.#shiftKey = !!eventInitDict.shiftKey;
    this.#altKey = !!eventInitDict.altKey;
    this.#metaKey = !!eventInitDict.metaKey;
  }

  get touches() {
    return this.#touches;
  }

  get targetTouches() {
    return this.#targetTouches;
  }

  get changedTouches() {
    return this.#changedTouches;
  }

  get ctrlKey() {
    return this.#ctrlKey;
  }

  get shiftKey() {
    return this.#shiftKey;
  }

  get altKey() {
    return this.#altKey;
  }

  get metaKey() {
    return this.#metaKey;
  }
}

extend(TouchEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'TouchEvent' }));

export class PointerEvent extends MouseEvent {
  #pointerId;
  #width;
  #height;
  #pressure;
  #tiltX;
  #tiltY;
  #pointerType;
  #isPrimary;

  constructor(type, eventInitDict = {}) {
    super(type, eventInitDict);
    this.#pointerId = eventInitDict.pointerId ?? 0;
    this.#width = eventInitDict.width ?? 1;
    this.#height = eventInitDict.height ?? 1;
    this.#pressure = eventInitDict.pressure ?? 0;
    this.#tiltX = eventInitDict.tiltX ?? 0;
    this.#tiltY = eventInitDict.tiltY ?? 0;
    this.#pointerType = eventInitDict.pointerType ?? '';
    this.#isPrimary = !!eventInitDict.isPrimary;
  }

  get pointerId() {
    return this.#pointerId;
  }

  get width() {
    return this.#width;
  }

  get height() {
    return this.#height;
  }

  get pressure() {
    return this.#pressure;
  }

  get tiltX() {
    return this.#tiltX;
  }

  get tiltY() {
    return this.#tiltY;
  }

  get pointerType() {
    return this.#pointerType;
  }

  get isPrimary() {
    return this.#isPrimary;
  }
}

extend(PointerEvent.prototype, nonenumerable({ [Symbol.toStringTag]: 'PointerEvent' }));

const listenerMap = weakMapper(() => new Map());

export class EventTarget {
  addEventListener(type, listener, options = {}) {
    if(!isFunction(listener)) return;

    const capture = isObject(options) ? !!options.capture : !!options;
    const once = isObject(options) ? !!options.once : false;
    const passive = isObject(options) ? !!options.passive : false;

    const listeners = listenerMap(this);
    if(!listeners.has(type)) listeners.set(type, []);

    const list = listeners.get(type);

    /* prevent duplicate registration */
    const exists = list.some(l => l.listener === listener && l.capture === capture);
    if(exists) return;

    list.push({ listener, capture, once, passive });
  }

  removeEventListener(type, listener, options = {}) {
    const capture = isObject(options) ? !!options.capture : !!options;
    const listeners = listenerMap(this);

    if(!listeners.has(type)) return;

    const list = listeners.get(type);
    const index = list.findIndex(l => l.listener === listener && l.capture === capture);

    if(index !== -1) list.splice(index, 1);
  }

  dispatchEvent(event) {
    if(!(event instanceof Event)) throw new TypeError('Argument 1 of EventTarget.dispatchEvent is not an Event');

    // Build event path (from root to target)
    const path = [];
    let node = this;
    while(node) {
      path.unshift(node);
      node = node.parentNode;
    }

    event.setTarget(this);

    // Capturing phase (from root to target's parent)
    for(let i = 0; i < path.length - 1; i++) {
      if(event.isPropagationStopped()) break;

      const target = path[i];
      event.setCurrentTarget(target);
      event.setEventPhase(Event.CAPTURING_PHASE);

      this.invokeListeners(target, event, true);
    }

    // At target phase
    if(!event.isPropagationStopped()) {
      const target = path[path.length - 1];
      event.setCurrentTarget(target);
      event.setEventPhase(Event.AT_TARGET);

      // At target, both capturing and bubbling listeners fire
      this.invokeListeners(target, event, true);
      if(!event.isPropagationStopped()) {
        this.invokeListeners(target, event, false);
      }
    }

    // Bubbling phase (from target's parent back to root)
    if(event.bubbles) {
      for(let i = path.length - 2; i >= 0; i--) {
        if(event.isPropagationStopped()) break;

        const target = path[i];
        event.setCurrentTarget(target);
        event.setEventPhase(Event.BUBBLING_PHASE);

        this.invokeListeners(target, event, false);
      }
    }

    event.setEventPhase(Event.NONE);
    event.setCurrentTarget(null);

    return !event.defaultPrevented;
  }

  invokeListeners(target, event, useCapture) {
    const listeners = listenerMap(target);
    if(!listeners.has(event.type)) return;

    const list = listeners.get(event.type);

    /* snapshot the listener list so removeEventListener during dispatch is safe */
    const snapshot = [...list];

    for(const entry of snapshot) {
      if(event.isImmediatePropagationStopped()) break;

      // Skip if capture/bubble phase doesn't match (except at target where both fire)
      if(useCapture !== entry.capture && event.eventPhase !== Event.AT_TARGET) continue;

      try {
        entry.listener.call(target, event);
      } catch(e) {
        console.error('Error in event listener:', e);
      }

      if(entry.once) {
        const idx = list.indexOf(entry);
        if(idx !== -1) list.splice(idx, 1);
      }
    }
  }
}

extend(EventTarget.prototype, nonenumerable({ [Symbol.toStringTag]: 'EventTarget' }));

export class Interface extends EventTarget {
  static [Symbol.hasInstance](instance) {
    return isObject(instance) && 'nodeType' in instance;
  }

  get textContent() {
    if(this.nodeType == TEXT_NODE) return this.nodeValue;

    const texts = [];

    for(const value of iterate(Node.raw(this), undefined, RETURN_VALUE | FILTER_KEY_OF | FILTER_NEGATE, TYPE_STRING, ['attributes', 'tagName'])) texts.push(value.replace(/\s+/g, ' '));

    return decodeHTMLEntities(texts.join(' '));
  }

  set textContent(value) {
    if(this.nodeType == TEXT_NODE) {
      this.nodeValue = value;
      return;
    }

    const raw = Node.raw(this);
    const children = (raw.children ??= []);
    children.splice(0, children.length);

    if(value != null && value !== '') {
      const text = new Text(String(value));
      children.push(Node.raw(text));
      parentNodes(text, this);
    }
  }

  get isConnected() {
    return isObject(ownerElements(this));
  }

  get nodeName() {
    switch (this.nodeType) {
      case DOCUMENT_NODE:
        return '#document';
      case TEXT_NODE:
        return '#text';
      case ELEMENT_NODE:
        return this.tagName.toUpperCase();
      case ATTRIBUTE_NODE:
        return this.name;
    }
    return null;
  }

  get nodeValue() {
    switch (this.nodeType) {
      case TEXT_NODE:
        return this.textContent;
      case ATTRIBUTE_NODE:
        return this.value;
    }
    return null;
  }

  get parentNode() {
    return Node.parent(this);
    /*let result = ownerElements(this);
    if(!isNode(result)) result = ownerElements(result);
    return result;*/
  }

  get parentElement() {
    const { parentNode } = this;
    return parentNode?.nodeType == ELEMENT_NODE ? parentNode : null;
  }

  contains(other) {
    if(this == other || (isInstanceOf(Node, other) && this.isSameNode(other))) return true;

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
    if(this.hasChildNodes()) return GetNode(Node.raw(this).children[0], this.childNodes);
  }

  get lastChild() {
    if(this.hasChildNodes()) {
      const { children } = Node.raw(this);
      return GetNode(children[children.length - 1], this.childNodes);
    }
  }

  get nextSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const { children } = Node.raw(parentNode);
      const index = children.indexOf(Node.raw(this));

      if(index != -1 && children[index + 1]) return GetNode(children[index + 1], ownerElements(this));
    }
  }

  get previousSibling() {
    const { parentNode } = this;

    if(parentNode.hasChildNodes()) {
      const { children } = Node.raw(parentNode);
      const index = children.indexOf(Node.raw(this));

      if(index != -1 && children[index - 1]) return GetNode(children[index - 1], ownerElements(this));
    }
  }

  cloneNode(deep = true) {
    const obj = clone(Node.raw(this));

    if(!deep && isObject(obj) && 'children' in obj) obj.children = [];

    const factory = Factory.for(this);
    const type = Factory.type(this);
    const el = factory(type)?.(obj, null);
    
    ownerDocument(el, this.ownerDocument);
    
    return el;
  }

  appendChild(node) {
    if(node.parentElement) node.parentElement.removeChild(node);
    //ownerElements(node)?.removeChild(node);

    const raw = Node.raw(node);
    const self = Node.raw(this);
    const children = (self.children ??= []);

    if(isInstanceOf(Text, node)) textValues(this, Text.own(this, children.length));

    const previousSibling = children.length > 0 ? GetNode(children[children.length - 1], this.childNodes) : null;

    children.push(raw);

    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);

    parentNodes(node, this);

    MutationObserver.eventFor(this, MutationRecord.childList(this, { addedNodes: [node], previousSibling }));

    return node;
  }

  insertBefore(node, ref) {
    ownerElements(node)?.removeChild(node);

    const children = (Node.raw(this).children ??= []);
    const old = isNode(node) ? Node.raw(node) : node,
      before = isNode(ref) ? Node.raw(ref) : ref;
    let index = children.indexOf(before);

    if(index == -1) index = children.length;

    children.splice(index, 0, old);

    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);
    parentNodes(node, this);

    MutationObserver.eventFor(
      this,
      MutationRecord.childList(this, { addedNodes: [node], nextSibling: ref ?? null, previousSibling: index > 0 ? GetNode(children[index - 1], this.childNodes) : null }),
    );

    return node;
  }

  removeChild(node) {
    const children = (Node.raw(this).children ??= []);
    let index = children.indexOf(isNode(node) ? Node.raw(node) : node);
    if(index == -1) throw new Error(`Node.removeChild no such child!`);
    const previousSibling = index > 0 ? GetNode(children[index - 1], this.childNodes) : null;
    const nextSibling = index + 1 < children.length ? GetNode(children[index + 1], this.childNodes) : null;
    children.splice(index, 1);
    setParentOwner(node, null);

    MutationObserver.eventFor(this, MutationRecord.childList(this, { removedNodes: [node], nextSibling, previousSibling }));

    return node;
  }

  replaceChild(newChild, oldChild) {
    ownerElements(newChild)?.removeChild(newChild);

    const children = (Node.raw(this).children ??= []);
    const old = Node.raw(oldChild),
      node = Node.raw(newChild);
    const idx = children.indexOf(old);

    if(idx == -1) throw new Error(`Node.replaceChild no such child!`);

    children.splice(idx, 1, node);

    setParentOwner(old, null);
    ownerElements(node, this.childNodes);
    ownerElements(this.childNodes, this);
    parentNodes(node, this);

    MutationObserver.eventFor(this, MutationRecord.childList(this, { addedNodes: [newChild], removedNodes: [oldChild] }));

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
            ? (node, path) =>
                values.every(v =>
                  path
                    .hier()
                    .filter(p => p[p.length - 1] != 'children')
                    .reverse()
                    .some(p => v(p.deref(raw))),
                )
                  ? YIELD_NO_RECURSE
                  : RECURSE
            : e => !/^[!?]/.test(e.tagName),
          RETURN_PATH | PATH_AS_POINTER | FILTER_KEY_OF | FILTER_NEGATE,
          TYPE_OBJECT | TYPE_STRING,
          ['attributes', 'tagName'],
        );

        if(path) return path.deref(this); //applyPath(path, this);
      }
    } catch(e) {
      DEBUG('querySelector', e);
      const { message, pointer, root, pos, stack } = e;
      DEBUG('querySelector', console.config({ compact: 1 }), { s, message, pointer, root, pos, stack });
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
          RETURN_PATH | PATH_AS_POINTER | FILTER_KEY_OF | FILTER_NEGATE,
          TYPE_OBJECT | TYPE_STRING,
          ['attributes', 'tagName'],
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
    const it = iterate(Node.raw(this), name == '*' ? undefined : e => e.tagName == name, RETURN_PATH | FILTER_KEY_OF | FILTER_NEGATE, TYPE_OBJECT, ['attributes', 'tagName']);

    for(const path of it) yield applyPath(path, this);
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

extend(Interface.prototype, nonenumerable(NODE_TYPES));

export class Node extends Interface {
  constructor(obj, parent) {
    super();
    rawNode(this, obj);
    setParentOwner(this, parent);
  }

  static [Symbol.hasInstance](instance) {
    return isObject(instance) && 'nodeType' in instance;
  }

  [inspectSymbol]() {
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

  static owner = ownerElements;

  static parent(node) {
    let tmp;
    if((tmp = parentNodes(node))) return tmp;
    if((tmp = ownerElements(node))) if ((tmp = ownerElements(tmp))) return tmp;
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

extend(Node.prototype, nonenumerable({ [Symbol.toStringTag]: 'Node' }));

export class NodeList {
  constructor(obj, owner) {
    // DEBUG('NodeList.constructor', { obj, owner });

    const isIndex = prop => isString(prop) && isNumeric(prop);
    const inRange = index => index >= 0 && index < obj.length;

    rawNode(this, obj);
    ownerElements(this, owner);

    //setParentOwner(this, owner);

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

    ownerElements(nodeList, owner);
    proxy(nodeList, this);

    return nodeList;
  }
}

extend(
  NodeList.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'NodeList',
    *[Symbol.iterator]() {
      const { length } = this;

      for(let i = 0; i < length; i++) yield this[i];
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

extend(HTMLCollection.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLCollection' }));

export function NamedMap(node, get, keys) {
  const raw = Node.raw(node);

  if(isString(get)) {
    const key = get;
    get = n => ((n = raw.children.find(e => e.attributes[key] == n)), n ? GetNode(n, node.children) : n);
    keys ??= () => [...raw.children].map(e => e.attributes[key]);
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

          return Reflect.get(target, prop, receiver);
        }
      : (target, prop, receiver) => {
          if(isString(prop)) {
            let tmp = get(prop);
            if(tmp) return tmp;
          }

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

extend(
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
        keys = Reflect.ownKeys(obj);

      for(const key of keys) {
        const part = obj[key];

        let s = part[inspectSymbol]?.(0, {}) ?? '';

        const pos = s.indexOf('Attr');
        if(pos != -1) s = s.slice(pos + 4);
        else s = (isNumeric(key) ? `[${key}]` : key) + ': ' + s;

        s = s.replaceAll(/{\s*([^}]+)\s*}/g, '$1');
        s = s.replaceAll(/\x1b\[0m\s+\x1b/g, '\x1b');

        a.push(s);
      }

      if(isPrototypeOf(Element.prototype, obj[keys[0]])) return `\x1b[1;31m${className(obj)}\x1b[0m {\n  ` + a.join(',\n  ') + `\n}`;

      return ' ' + a.join('').trim();
    },
  }),
);

extend(
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
      for(let i = 0; this[i]; i++) yield this[i];
    },
    [inspectSymbol]() {
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

    //lazyProperties(this, { classList: () => new TokenList(this, 'class') });
  }

  static [Symbol.hasInstance](instance) {
    return isObject(instance) && instance.nodeType == ELEMENT_NODE;
  }

  get tagName() {
    const raw = Node.raw(this);
    if(!raw) DEBUG('get tagName', { thisObj: this, raw });
    return raw?.tagName;
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
    const raw = Node.raw(this);

    const attributes = (raw.attributes ??= {});
    const gs = gettersetter(attributes);

    const factory = Factory.for(this);

    return factory.NamedNodeMap.cache(
      {
        get: k => new Attr([(...args) => gs(k, ...args), k], this),
        has: k => k in attributes,
        keys: () => Reflect.ownKeys(attributes),
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
    return Node.raw(this).attributes?.[name] ?? null;
    //return Element.attributes(this)(attributes => attributes[name]);
  }

  getAttributeNames() {
    return Object.keys(Node.raw(this)?.attributes ?? {});
    //return Element.attributes(this)(attributes => Object.keys(attributes));
  }

  hasAttribute(name) {
    return name in (Node.raw(this)?.attributes ?? {});
    //return Element.attributes(this)(attributes => name in attributes);
  }

  hasAttributes() {
    return this.getAttributeNames().length > 0;
  }

  removeAttribute(name) {
    const raw = Node.raw(this);
    const oldValue = raw.attributes[name];
    const ret = delete raw.attributes[name];
    //return Element.attributes(this)(attributes => delete attributes[name]);

    MutationObserver.eventFor(this, MutationRecord.attribute(name, null, this, oldValue));

    return ret;
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

    MutationObserver.eventFor(this, MutationRecord.attribute(name, null, this, oldValue));
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
    if(!isObject(obj)) return false;
    let proto = Object.getPrototypeOf(obj);
    while(proto) {
      if(proto === Element.prototype) return true;
      proto = Object.getPrototypeOf(proto);
    }
    return false;
  }

  static cache = MakeCache((obj, owner) => {
    let ctor = Element;

    if(Element.elements) {
      const ctor2 = Element.elements[obj.tagName];
      if(isFunction(ctor2)) ctor = ctor2;
      else if(ctor2?.constructor) ctor = ctor2.constructor;
    }

    return new ctor(obj, owner);
  });

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

extend(
  Element.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'Element',
    nodeType: ELEMENT_NODE,
    namespaceURI: 'http://www.w3.org/1999/xhtml',
  }),
);

extend(
  Element.prototype,
  nonenumerable({
    [inspectSymbol](depth, opts) {
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
            str += ('\n' + child[inspectSymbol](depth + 1, opts)).replaceAll('\n', '\n  ');
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

/* ========== DOMStringMap (dataset) ========== */

const datasetElements = gettersetter(new WeakMap());

export class DOMStringMap {
  constructor(element) {
    datasetElements(this, element);
  }

  [Symbol.iterator]() {
    const element = datasetElements(this);
    return Object.keys(Node.raw(element).attributes ?? {})
      .filter(k => k.startsWith('data-'))
      .map(k => [camelize(k.slice(5)), element.getAttribute(k)])
      [Symbol.iterator]();
  }

  get [Symbol.toStringTag]() {
    return 'DOMStringMap';
  }
}

extend(DOMStringMap.prototype, {
  get(name) {
    return this[name];
  },
  set(name, value) {
    this[name] = value;
  },
  delete(name) {
    delete this[name];
  },
});

const datasetHandler = {
  get(map, name) {
    const element = datasetElements(map);
    return element?.getAttribute?.('data-' + decamelize(name)) ?? undefined;
  },
  set(map, name, value) {
    const element = datasetElements(map);
    element?.setAttribute?.('data-' + decamelize(name), String(value));
    return true;
  },
  has(map, name) {
    const element = datasetElements(map);
    return element?.hasAttribute?.('data-' + decamelize(name)) ?? false;
  },
  deleteProperty(map, name) {
    const element = datasetElements(map);
    element?.removeAttribute?.('data-' + decamelize(name));
    return true;
  },
  ownKeys(map) {
    const element = datasetElements(map);
    return Object.keys(Node.raw(element).attributes ?? {})
      .filter(k => k.startsWith('data-'))
      .map(k => camelize(k.slice(5)));
  },
  getOwnPropertyDescriptor(map, name) {
    if(datasetHandler.has(map, name)) return { configurable: true, enumerable: true, value: datasetHandler.get(map, name) };
  },
};

/* ========== HTMLElement (9.2) ========== */

export class HTMLElement extends Element {
  #dataset;

  get dataset() {
    return (this.#dataset ??= new Proxy(new DOMStringMap(this), datasetHandler));
  }

  get hidden() {
    return this.hasAttribute('hidden');
  }

  set hidden(v) {
    if(v) this.setAttribute('hidden', '');
    else this.removeAttribute('hidden');
  }

  get tabIndex() {
    const v = this.getAttribute('tabindex');
    return v !== null ? parseInt(v, 10) || 0 : -1;
  }

  set tabIndex(v) {
    this.setAttribute('tabindex', String(v));
  }

  get title() {
    return this.getAttribute('title') ?? '';
  }

  set title(v) {
    this.setAttribute('title', v);
  }

  get lang() {
    return this.getAttribute('lang') ?? '';
  }

  set lang(v) {
    this.setAttribute('lang', v);
  }

  get dir() {
    return this.getAttribute('dir') ?? '';
  }

  set dir(v) {
    this.setAttribute('dir', v);
  }

  get draggable() {
    return this.getAttribute('draggable') === 'true';
  }

  set draggable(v) {
    this.setAttribute('draggable', v ? 'true' : 'false');
  }

  get contentEditable() {
    const v = this.getAttribute('contenteditable');
    if(v === '' || v === 'true') return 'true';
    if(v === 'false') return 'false';
    return this.parentElement?.contentEditable ?? 'inherit';
  }

  set contentEditable(v) {
    this.setAttribute('contenteditable', v ? 'true' : 'false');
  }

  click() {
    this.dispatchEvent(new Event('click', { bubbles: true, cancelable: true }));
  }

  focus() {
    const doc = this.ownerDocument;
    if(doc) doc.activeElement = this;
    this.dispatchEvent(new Event('focus', { bubbles: false }));
  }

  blur() {
    const doc = this.ownerDocument;
    if(doc?.activeElement === this) doc.activeElement = null;
    this.dispatchEvent(new Event('blur', { bubbles: false }));
  }

  closest(selector) {
    for(const sel of parseSelectors(selector)) {
      const values = sel?.values();
      if(!values) continue;

      for(let node = this; node && node.nodeType === ELEMENT_NODE; node = node.parentElement) {
        const raw = Node.raw(node);
        if(values.every(v => v(raw))) return node;
      }
    }
    return null;
  }

  insertAdjacentElement(position, element) {
    switch ((position || '').toLowerCase()) {
      case 'beforebegin':
        this.parentNode?.insertBefore(element, this);
        return element;
      case 'afterend':
        this.parentNode?.insertBefore(element, this.nextSibling);
        return element;
      case 'afterbegin':
        this.insertBefore(element, this.firstChild);
        return element;
      case 'beforeend':
        this.appendChild(element);
        return element;
    }
    return null;
  }
}

extend(
  HTMLElement.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'HTMLElement',
  }),
);

/* ========== HTMLElement subclasses (9.5) ========== */

export class HTMLInputElement extends HTMLElement {
  #customValue;
  #checked;

  get value() {
    if(this.#customValue !== undefined) return this.#customValue;
    const type = (this.getAttribute('type') || 'text').toLowerCase();
    if(type === 'checkbox' || type === 'radio') return this.getAttribute('value') ?? 'on';
    return this.getAttribute('value') ?? '';
  }

  set value(v) {
    this.#customValue = String(v);
  }

  get checked() {
    return this.#checked ?? this.hasAttribute('checked');
  }

  set checked(v) {
    this.#checked = !!v;
  }

  get type() {
    return this.getAttribute('type') ?? 'text';
  }

  set type(v) {
    this.setAttribute('type', v);
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get placeholder() {
    return this.getAttribute('placeholder') ?? '';
  }

  set placeholder(v) {
    this.setAttribute('placeholder', v);
  }

  get disabled() {
    return this.hasAttribute('disabled');
  }

  set disabled(v) {
    if(v) this.setAttribute('disabled', '');
    else this.removeAttribute('disabled');
  }

  get readOnly() {
    return this.hasAttribute('readonly');
  }

  set readOnly(v) {
    if(v) this.setAttribute('readonly', '');
    else this.removeAttribute('readonly');
  }

  get required() {
    return this.hasAttribute('required');
  }

  set required(v) {
    if(v) this.setAttribute('required', '');
    else this.removeAttribute('required');
  }

  get multiple() {
    return this.hasAttribute('multiple');
  }

  set multiple(v) {
    if(v) this.setAttribute('multiple', '');
    else this.removeAttribute('multiple');
  }

  get min() {
    return this.getAttribute('min') ?? '';
  }

  set min(v) {
    this.setAttribute('min', v);
  }

  get max() {
    return this.getAttribute('max') ?? '';
  }

  set max(v) {
    this.setAttribute('max', v);
  }

  get step() {
    return this.getAttribute('step') ?? '';
  }

  set step(v) {
    this.setAttribute('step', v);
  }

  get pattern() {
    return this.getAttribute('pattern') ?? '';
  }

  set pattern(v) {
    this.setAttribute('pattern', v);
  }

  get maxLength() {
    const v = this.getAttribute('maxlength');
    return v !== null ? parseInt(v, 10) : -1;
  }

  set maxLength(v) {
    this.setAttribute('maxlength', String(v));
  }

  get minLength() {
    const v = this.getAttribute('minlength');
    return v !== null ? parseInt(v, 10) : -1;
  }

  set minLength(v) {
    this.setAttribute('minlength', String(v));
  }

  get size() {
    const v = this.getAttribute('size');
    return v !== null ? parseInt(v, 10) : 20;
  }

  set size(v) {
    this.setAttribute('size', String(v));
  }

  get autocomplete() {
    return this.getAttribute('autocomplete') ?? '';
  }

  set autocomplete(v) {
    this.setAttribute('autocomplete', v);
  }

  get form() {
    for(let node = this.parentElement; node; node = node.parentElement) if(node.tagName === 'form') return node;
    return null;
  }

  select() {}

  setSelectionRange() {}

  setCustomValidity() {}

  checkValidity() {
    return true;
  }

  reportValidity() {
    return true;
  }

  get validity() {
    return { valid: true };
  }

  get validationMessage() {
    return '';
  }

  get willValidate() {
    return !this.disabled && this.type !== 'hidden';
  }

  stepUp(n = 1) {
    const val = parseFloat(this.value) || 0;
    const s = parseFloat(this.step) || 1;
    this.value = String(val + s * n);
  }

  stepDown(n = 1) {
    this.stepUp(-n);
  }
}

extend(HTMLInputElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLInputElement' }));

export class HTMLButtonElement extends HTMLElement {
  get type() {
    return this.getAttribute('type') ?? 'submit';
  }

  set type(v) {
    this.setAttribute('type', v);
  }

  get disabled() {
    return this.hasAttribute('disabled');
  }

  set disabled(v) {
    if(v) this.setAttribute('disabled', '');
    else this.removeAttribute('disabled');
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get value() {
    return this.getAttribute('value') ?? '';
  }

  set value(v) {
    this.setAttribute('value', v);
  }

  get form() {
    for(let node = this.parentElement; node; node = node.parentElement) if(node.tagName === 'form') return node;
    return null;
  }
}

extend(HTMLButtonElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLButtonElement' }));

export class HTMLFormElement extends HTMLElement {
  get action() {
    return this.getAttribute('action') ?? '';
  }

  set action(v) {
    this.setAttribute('action', v);
  }

  get method() {
    return this.getAttribute('method') ?? 'get';
  }

  set method(v) {
    this.setAttribute('method', v);
  }

  get enctype() {
    return this.getAttribute('enctype') ?? 'application/x-www-form-urlencoded';
  }

  set enctype(v) {
    this.setAttribute('enctype', v);
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get target() {
    return this.getAttribute('target') ?? '';
  }

  set target(v) {
    this.setAttribute('target', v);
  }

  get noValidate() {
    return this.hasAttribute('novalidate');
  }

  set noValidate(v) {
    if(v) this.setAttribute('novalidate', '');
    else this.removeAttribute('novalidate');
  }

  get elements() {
    const controls = [];
    for(const child of this.children) {
      if(child.nodeType === ELEMENT_NODE && ['input', 'select', 'textarea', 'button'].includes(child.tagName)) controls.push(child);
    }
    return controls;
  }

  get length() {
    return this.elements.length;
  }

  submit() {
    this.dispatchEvent(new Event('submit', { bubbles: true, cancelable: true }));
  }

  reset() {
    for(const el of this.elements) {
      if(el.tagName === 'input') {
        const type = (el.getAttribute('type') || 'text').toLowerCase();
        if(type === 'checkbox' || type === 'radio') el.checked = el.hasAttribute('checked');
        else el.value = el.getAttribute('value') ?? '';
      } else if(el.tagName === 'select') {
        for(const opt of el.options) opt.selected = opt.hasAttribute('selected');
      } else if(el.tagName === 'textarea') {
        el.value = el.textContent;
      }
    }
    this.dispatchEvent(new Event('reset', { bubbles: true }));
  }

  checkValidity() {
    return true;
  }

  reportValidity() {
    return true;
  }
}

extend(HTMLFormElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLFormElement' }));

export class HTMLAnchorElement extends HTMLElement {
  get href() {
    return this.getAttribute('href') ?? '';
  }

  set href(v) {
    this.setAttribute('href', v);
  }

  get target() {
    return this.getAttribute('target') ?? '';
  }

  set target(v) {
    this.setAttribute('target', v);
  }

  get rel() {
    return this.getAttribute('rel') ?? '';
  }

  set rel(v) {
    this.setAttribute('rel', v);
  }

  get download() {
    return this.getAttribute('download') ?? '';
  }

  set download(v) {
    this.setAttribute('download', v);
  }

  get hash() {
    try {
      return new URL(this.href).hash;
    } catch(e) {
      return '';
    }
  }

  get host() {
    try {
      return new URL(this.href).host;
    } catch(e) {
      return '';
    }
  }

  get hostname() {
    try {
      return new URL(this.href).hostname;
    } catch(e) {
      return '';
    }
  }

  get pathname() {
    try {
      return new URL(this.href).pathname;
    } catch(e) {
      return '';
    }
  }

  get port() {
    try {
      return new URL(this.href).port;
    } catch(e) {
      return '';
    }
  }

  get protocol() {
    try {
      return new URL(this.href).protocol;
    } catch(e) {
      return '';
    }
  }

  get search() {
    try {
      return new URL(this.href).search;
    } catch(e) {
      return '';
    }
  }

  get origin() {
    try {
      return new URL(this.href).origin;
    } catch(e) {
      return '';
    }
  }
}

extend(HTMLAnchorElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLAnchorElement' }));

export class HTMLImageElement extends HTMLElement {
  get src() {
    return this.getAttribute('src') ?? '';
  }

  set src(v) {
    this.setAttribute('src', v);
  }

  get alt() {
    return this.getAttribute('alt') ?? '';
  }

  set alt(v) {
    this.setAttribute('alt', v);
  }

  get width() {
    const v = this.getAttribute('width');
    return v !== null ? parseInt(v, 10) || 0 : 0;
  }

  set width(v) {
    this.setAttribute('width', String(v));
  }

  get height() {
    const v = this.getAttribute('height');
    return v !== null ? parseInt(v, 10) || 0 : 0;
  }

  set height(v) {
    this.setAttribute('height', String(v));
  }

  get crossOrigin() {
    return this.getAttribute('crossorigin');
  }

  set crossOrigin(v) {
    if(v === null) this.removeAttribute('crossorigin');
    else this.setAttribute('crossorigin', v);
  }
}

extend(HTMLImageElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLImageElement' }));

export class HTMLTextAreaElement extends HTMLElement {
  #customValue;

  get value() {
    return this.#customValue ?? this.textContent;
  }

  set value(v) {
    this.#customValue = String(v);
  }

  get rows() {
    const v = this.getAttribute('rows');
    return v !== null ? parseInt(v, 10) || 2 : 2;
  }

  set rows(v) {
    this.setAttribute('rows', String(v));
  }

  get cols() {
    const v = this.getAttribute('cols');
    return v !== null ? parseInt(v, 10) || 20 : 20;
  }

  set cols(v) {
    this.setAttribute('cols', String(v));
  }

  get placeholder() {
    return this.getAttribute('placeholder') ?? '';
  }

  set placeholder(v) {
    this.setAttribute('placeholder', v);
  }

  get disabled() {
    return this.hasAttribute('disabled');
  }

  set disabled(v) {
    if(v) this.setAttribute('disabled', '');
    else this.removeAttribute('disabled');
  }

  get readOnly() {
    return this.hasAttribute('readonly');
  }

  set readOnly(v) {
    if(v) this.setAttribute('readonly', '');
    else this.removeAttribute('readonly');
  }

  get required() {
    return this.hasAttribute('required');
  }

  set required(v) {
    if(v) this.setAttribute('required', '');
    else this.removeAttribute('required');
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get maxLength() {
    const v = this.getAttribute('maxlength');
    return v !== null ? parseInt(v, 10) : -1;
  }

  set maxLength(v) {
    this.setAttribute('maxlength', String(v));
  }

  get form() {
    for(let node = this.parentElement; node; node = node.parentElement) if(node.tagName === 'form') return node;
    return null;
  }

  select() {}

  setSelectionRange() {}

  checkValidity() {
    return true;
  }
}

extend(HTMLTextAreaElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTextAreaElement' }));

export class HTMLSelectElement extends HTMLElement {
  get value() {
    for(const opt of this.options) if(opt.selected) return opt.value;
    return '';
  }

  set value(v) {
    for(const opt of this.options) opt.selected = opt.value === v;
  }

  get selectedIndex() {
    const opts = this.options;
    for(let i = 0; i < opts.length; i++) if(opts[i].selected) return i;
    return -1;
  }

  set selectedIndex(idx) {
    const opts = this.options;
    for(let i = 0; i < opts.length; i++) opts[i].selected = i === idx;
  }

  get options() {
    const opts = [];
    const collect = node => {
      for(const child of node.children) {
        if(child.nodeType === ELEMENT_NODE) {
          if(child.tagName === 'option') opts.push(child);
          else if(child.tagName === 'optgroup') collect(child);
        }
      }
    };
    collect(this);
    return opts;
  }

  get multiple() {
    return this.hasAttribute('multiple');
  }

  set multiple(v) {
    if(v) this.setAttribute('multiple', '');
    else this.removeAttribute('multiple');
  }

  get size() {
    const v = this.getAttribute('size');
    return v !== null ? parseInt(v, 10) : this.multiple ? 4 : 1;
  }

  set size(v) {
    this.setAttribute('size', String(v));
  }

  get disabled() {
    return this.hasAttribute('disabled');
  }

  set disabled(v) {
    if(v) this.setAttribute('disabled', '');
    else this.removeAttribute('disabled');
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get required() {
    return this.hasAttribute('required');
  }

  set required(v) {
    if(v) this.setAttribute('required', '');
    else this.removeAttribute('required');
  }

  get form() {
    for(let node = this.parentElement; node; node = node.parentElement) if(node.tagName === 'form') return node;
    return null;
  }

  get length() {
    return this.options.length;
  }

  add(option) {
    this.appendChild(option);
  }

  remove(index) {
    const opts = this.options;
    if(index >= 0 && index < opts.length) opts[index].remove();
  }

  checkValidity() {
    return true;
  }
}

extend(HTMLSelectElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLSelectElement' }));

export class HTMLOptionElement extends HTMLElement {
  get value() {
    return this.getAttribute('value') ?? this.textContent.trim();
  }

  set value(v) {
    this.setAttribute('value', v);
  }

  get text() {
    return this.textContent.trim();
  }

  set text(v) {
    this.textContent = v;
  }

  get selected() {
    return this.#selected ?? this.hasAttribute('selected');
  }

  set selected(v) {
    this.#selected = !!v;
  }

  #selected;

  get disabled() {
    return this.hasAttribute('disabled');
  }

  set disabled(v) {
    if(v) this.setAttribute('disabled', '');
    else this.removeAttribute('disabled');
  }

  get label() {
    return this.getAttribute('label') ?? this.text;
  }

  set label(v) {
    this.setAttribute('label', v);
  }

  get defaultSelected() {
    return this.hasAttribute('selected');
  }

  get index() {
    const select = this.parentElement;
    if(select?.options) {
      const opts = select.options;
      for(let i = 0; i < opts.length; i++) if(opts[i] === this) return i;
    }
    return 0;
  }
}

extend(HTMLOptionElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLOptionElement' }));

export class HTMLScriptElement extends HTMLElement {
  get src() {
    return this.getAttribute('src') ?? '';
  }

  set src(v) {
    this.setAttribute('src', v);
  }

  get type() {
    return this.getAttribute('type') ?? '';
  }

  set type(v) {
    this.setAttribute('type', v);
  }

  get async() {
    return this.hasAttribute('async');
  }

  set async(v) {
    if(v) this.setAttribute('async', '');
    else this.removeAttribute('async');
  }

  get defer() {
    return this.hasAttribute('defer');
  }

  set defer(v) {
    if(v) this.setAttribute('defer', '');
    else this.removeAttribute('defer');
  }

  get text() {
    return this.textContent;
  }

  set text(v) {
    this.textContent = v;
  }

  get crossOrigin() {
    return this.getAttribute('crossorigin');
  }

  set crossOrigin(v) {
    if(v === null) this.removeAttribute('crossorigin');
    else this.setAttribute('crossorigin', v);
  }
}

extend(HTMLScriptElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLScriptElement' }));

export class HTMLStyleElement extends HTMLElement {
  get type() {
    return this.getAttribute('type') ?? 'text/css';
  }

  set type(v) {
    this.setAttribute('type', v);
  }

  get media() {
    return this.getAttribute('media') ?? '';
  }

  set media(v) {
    this.setAttribute('media', v);
  }
}

extend(HTMLStyleElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLStyleElement' }));

export class HTMLLinkElement extends HTMLElement {
  get href() {
    return this.getAttribute('href') ?? '';
  }

  set href(v) {
    this.setAttribute('href', v);
  }

  get rel() {
    return this.getAttribute('rel') ?? '';
  }

  set rel(v) {
    this.setAttribute('rel', v);
  }

  get type() {
    return this.getAttribute('type') ?? '';
  }

  set type(v) {
    this.setAttribute('type', v);
  }

  get media() {
    return this.getAttribute('media') ?? '';
  }

  set media(v) {
    this.setAttribute('media', v);
  }

  get crossOrigin() {
    return this.getAttribute('crossorigin');
  }

  set crossOrigin(v) {
    if(v === null) this.removeAttribute('crossorigin');
    else this.setAttribute('crossorigin', v);
  }
}

extend(HTMLLinkElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLLinkElement' }));

export class HTMLMediaElement extends HTMLElement {
  get src() {
    return this.getAttribute('src') ?? '';
  }

  set src(v) {
    this.setAttribute('src', v);
  }

  get autoplay() {
    return this.hasAttribute('autoplay');
  }

  set autoplay(v) {
    if(v) this.setAttribute('autoplay', '');
    else this.removeAttribute('autoplay');
  }

  get controls() {
    return this.hasAttribute('controls');
  }

  set controls(v) {
    if(v) this.setAttribute('controls', '');
    else this.removeAttribute('controls');
  }

  get loop() {
    return this.hasAttribute('loop');
  }

  set loop(v) {
    if(v) this.setAttribute('loop', '');
    else this.removeAttribute('loop');
  }

  get muted() {
    return this.hasAttribute('muted');
  }

  set muted(v) {
    if(v) this.setAttribute('muted', '');
    else this.removeAttribute('muted');
  }

  get preload() {
    return this.getAttribute('preload') ?? '';
  }

  set preload(v) {
    this.setAttribute('preload', v);
  }

  get crossOrigin() {
    return this.getAttribute('crossorigin');
  }

  set crossOrigin(v) {
    if(v === null) this.removeAttribute('crossorigin');
    else this.setAttribute('crossorigin', v);
  }
}

extend(HTMLMediaElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLMediaElement' }));

export class HTMLVideoElement extends HTMLMediaElement {
  get width() {
    const v = this.getAttribute('width');
    return v !== null ? parseInt(v, 10) || 0 : 0;
  }

  set width(v) {
    this.setAttribute('width', String(v));
  }

  get height() {
    const v = this.getAttribute('height');
    return v !== null ? parseInt(v, 10) || 0 : 0;
  }

  set height(v) {
    this.setAttribute('height', String(v));
  }

  get poster() {
    return this.getAttribute('poster') ?? '';
  }

  set poster(v) {
    this.setAttribute('poster', v);
  }
}

extend(HTMLVideoElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLVideoElement' }));

export class HTMLAudioElement extends HTMLMediaElement {}

extend(HTMLAudioElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLAudioElement' }));

export class HTMLTableElement extends HTMLElement {
  get caption() {
    return this.querySelector('caption');
  }

  get tHead() {
    return this.querySelector('thead');
  }

  get tBody() {
    return this.querySelector('tbody');
  }

  get tFoot() {
    return this.querySelector('tfoot');
  }

  get rows() {
    return [...this.querySelectorAll('tr')];
  }

  get tBodies() {
    return [...this.querySelectorAll('tbody')];
  }

  createTHead() {
    let thead = this.querySelector('thead');
    if(!thead) {
      thead = this.ownerDocument.createElement('thead');
      this.insertBefore(thead, this.firstChild);
    }
    return thead;
  }

  createTBody() {
    const tbody = this.ownerDocument.createElement('tbody');
    this.appendChild(tbody);
    return tbody;
  }

  createTFoot() {
    let tfoot = this.querySelector('tfoot');
    if(!tfoot) {
      tfoot = this.ownerDocument.createElement('tfoot');
      this.appendChild(tfoot);
    }
    return tfoot;
  }

  createCaption() {
    let caption = this.querySelector('caption');
    if(!caption) {
      caption = this.ownerDocument.createElement('caption');
      this.insertBefore(caption, this.firstChild);
    }
    return caption;
  }

  insertRow(index = -1) {
    const tbody = this.querySelector('tbody') ?? this.createTBody();
    const tr = this.ownerDocument.createElement('tr');
    const rows = tbody.children;
    if(index === -1 || index >= rows.length) {
      tbody.appendChild(tr);
    } else {
      tbody.insertBefore(tr, rows[index]);
    }
    return tr;
  }

  deleteRow(index) {
    const rows = this.rows;
    if(index >= 0 && index < rows.length) rows[index].remove();
  }
}

extend(HTMLTableElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTableElement' }));

export class HTMLTableRowElement extends HTMLElement {
  get cells() {
    return [...this.children].filter(c => c.nodeType === ELEMENT_NODE && (c.tagName === 'td' || c.tagName === 'th'));
  }

  get rowIndex() {
    const table = this.closest('table');
    if(!table) return -1;
    const rows = table.rows;
    return rows.indexOf(this);
  }

  get sectionRowIndex() {
    const parent = this.parentElement;
    if(!parent) return -1;
    const rows = [...parent.children].filter(c => c.nodeType === ELEMENT_NODE && c.tagName === 'tr');
    return rows.indexOf(this);
  }

  insertCell(index = -1) {
    const td = this.ownerDocument.createElement('td');
    const cells = this.cells;
    if(index === -1 || index >= cells.length) {
      this.appendChild(td);
    } else {
      this.insertBefore(td, cells[index]);
    }
    return td;
  }

  deleteCell(index) {
    const cells = this.cells;
    if(index >= 0 && index < cells.length) cells[index].remove();
  }
}

extend(HTMLTableRowElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTableRowElement' }));

export class HTMLTableCellElement extends HTMLElement {
  get colSpan() {
    const v = this.getAttribute('colspan');
    return v !== null ? parseInt(v, 10) || 1 : 1;
  }

  set colSpan(v) {
    this.setAttribute('colspan', String(v));
  }

  get rowSpan() {
    const v = this.getAttribute('rowspan');
    return v !== null ? parseInt(v, 10) || 1 : 1;
  }

  set rowSpan(v) {
    this.setAttribute('rowspan', String(v));
  }

  get headers() {
    return this.getAttribute('headers') ?? '';
  }

  set headers(v) {
    this.setAttribute('headers', v);
  }

  get cellIndex() {
    const row = this.parentElement;
    if(!row) return -1;
    return row.cells.indexOf(this);
  }

  get scope() {
    return this.getAttribute('scope') ?? '';
  }

  set scope(v) {
    this.setAttribute('scope', v);
  }
}

extend(HTMLTableCellElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTableCellElement' }));

export class HTMLLabelElement extends HTMLElement {
  get htmlFor() {
    return this.getAttribute('for') ?? '';
  }

  set htmlFor(v) {
    this.setAttribute('for', v);
  }

  get control() {
    const id = this.htmlFor;
    if(id) return this.ownerDocument?.getElementById(id) ?? null;
    return this.querySelector('input, select, textarea, button') ?? null;
  }

  get form() {
    const ctrl = this.control;
    return ctrl?.form ?? null;
  }
}

extend(HTMLLabelElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLLabelElement' }));

export class HTMLLIElement extends HTMLElement {
  get value() {
    const v = this.getAttribute('value');
    return v !== null ? parseInt(v, 10) : 0;
  }

  set value(v) {
    this.setAttribute('value', String(v));
  }
}

extend(HTMLLIElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLLIElement' }));

export class HTMLOListElement extends HTMLElement {
  get start() {
    const v = this.getAttribute('start');
    return v !== null ? parseInt(v, 10) : 1;
  }

  set start(v) {
    this.setAttribute('start', String(v));
  }

  get reversed() {
    return this.hasAttribute('reversed');
  }

  set reversed(v) {
    if(v) this.setAttribute('reversed', '');
    else this.removeAttribute('reversed');
  }

  get type() {
    return this.getAttribute('type') ?? '';
  }

  set type(v) {
    this.setAttribute('type', v);
  }
}

extend(HTMLOListElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLOListElement' }));

export class HTMLIFrameElement extends HTMLElement {
  get src() {
    return this.getAttribute('src') ?? '';
  }

  set src(v) {
    this.setAttribute('src', v);
  }

  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get width() {
    return this.getAttribute('width') ?? '';
  }

  set width(v) {
    this.setAttribute('width', v);
  }

  get height() {
    return this.getAttribute('height') ?? '';
  }

  set height(v) {
    this.setAttribute('height', v);
  }

  get sandbox() {
    return this.getAttribute('sandbox') ?? '';
  }

  set sandbox(v) {
    this.setAttribute('sandbox', v);
  }

  get contentDocument() {
    return null;
  }

  get contentWindow() {
    return null;
  }
}

extend(HTMLIFrameElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLIFrameElement' }));

export class HTMLMetaElement extends HTMLElement {
  get name() {
    return this.getAttribute('name') ?? '';
  }

  set name(v) {
    this.setAttribute('name', v);
  }

  get content() {
    return this.getAttribute('content') ?? '';
  }

  set content(v) {
    this.setAttribute('content', v);
  }

  get httpEquiv() {
    return this.getAttribute('http-equiv') ?? '';
  }

  set httpEquiv(v) {
    this.setAttribute('http-equiv', v);
  }
}

extend(HTMLMetaElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLMetaElement' }));

export class HTMLCanvasElement extends HTMLElement {
  get width() {
    const v = this.getAttribute('width');
    return v !== null ? parseInt(v, 10) : 300;
  }

  set width(v) {
    this.setAttribute('width', String(v));
  }

  get height() {
    const v = this.getAttribute('height');
    return v !== null ? parseInt(v, 10) : 150;
  }

  set height(v) {
    this.setAttribute('height', String(v));
  }

  getContext() {
    return null;
  }

  toDataURL() {
    return '';
  }

  toBlob() {
    return null;
  }
}

extend(HTMLCanvasElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLCanvasElement' }));

export class HTMLDialogElement extends HTMLElement {
  get open() {
    return this.hasAttribute('open');
  }

  set open(v) {
    if(v) this.setAttribute('open', '');
    else this.removeAttribute('open');
  }

  get returnValue() {
    return this.getAttribute('returnvalue') ?? '';
  }

  set returnValue(v) {
    this.setAttribute('returnvalue', v);
  }

  show() {
    this.open = true;
  }

  showModal() {
    this.open = true;
  }

  close(returnValue) {
    if(returnValue !== undefined) this.returnValue = returnValue;
    this.open = false;
    this.dispatchEvent(new Event('close'));
  }
}

extend(HTMLDialogElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLDialogElement' }));

export class HTMLHeadElement extends HTMLElement {}
export class HTMLBodyElement extends HTMLElement {}
export class HTMLHtmlElement extends HTMLElement {}
export class HTMLParagraphElement extends HTMLElement {}
export class HTMLHeadingElement extends HTMLElement {}
export class HTMLPreElement extends HTMLElement {}
export class HTMLQuoteElement extends HTMLElement {}
export class HTMLHRElement extends HTMLElement {}
export class HTMLBRElement extends HTMLElement {}
export class HTMLDataListElement extends HTMLElement {}
export class HTMLFieldSetElement extends HTMLElement {}
export class HTMLOptGroupElement extends HTMLElement {}
export class HTMLLegendElement extends HTMLElement {}
export class HTMLSpanElement extends HTMLElement {}
export class HTMLDivElement extends HTMLElement {}
export class HTMLProgressElement extends HTMLElement {}
export class HTMLMeterElement extends HTMLElement {}
export class HTMLDetailsElement extends HTMLElement {}
export class HTMLSummaryElement extends HTMLElement {}
export class HTMLTemplateElement extends HTMLElement {}
export class HTMLSlotElement extends HTMLElement {}
export class HTMLDataElement extends HTMLElement {}
export class HTMLTimeElement extends HTMLElement {}
export class HTMLPictureElement extends HTMLElement {}
export class HTMLSourceElement extends HTMLElement {}
export class HTMLTrackElement extends HTMLElement {}
export class HTMLEmbedElement extends HTMLElement {}
export class HTMLObjectElement extends HTMLElement {}
export class HTMLParamElement extends HTMLElement {}
export class HTMLMapElement extends HTMLElement {}
export class HTMLAreaElement extends HTMLElement {}
export class HTMLBaseElement extends HTMLElement {}
export class HTMLTitleElement extends HTMLElement {}
export class HTMLTableSectionElement extends HTMLElement {}
export class HTMLTableCaptionElement extends HTMLElement {}
export class HTMLColElement extends HTMLElement {}
export class HTMLColGroupElement extends HTMLElement {}
export class HTMLUnknownElement extends HTMLElement {}

extend(HTMLHeadElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLHeadElement' }));
extend(HTMLBodyElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLBodyElement' }));
extend(HTMLHtmlElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLHtmlElement' }));
extend(HTMLParagraphElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLParagraphElement' }));
extend(HTMLHeadingElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLHeadingElement' }));
extend(HTMLPreElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLPreElement' }));
extend(HTMLQuoteElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLQuoteElement' }));
extend(HTMLHRElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLHRElement' }));
extend(HTMLBRElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLBRElement' }));
extend(HTMLDataListElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLDataListElement' }));
extend(HTMLFieldSetElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLFieldSetElement' }));
extend(HTMLOptGroupElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLOptGroupElement' }));
extend(HTMLLegendElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLLegendElement' }));
extend(HTMLSpanElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLSpanElement' }));
extend(HTMLDivElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLDivElement' }));
extend(HTMLProgressElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLProgressElement' }));
extend(HTMLMeterElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLMeterElement' }));
extend(HTMLDetailsElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLDetailsElement' }));
extend(HTMLSummaryElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLSummaryElement' }));
extend(HTMLTemplateElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTemplateElement' }));
extend(HTMLSlotElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLSlotElement' }));
extend(HTMLDataElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLDataElement' }));
extend(HTMLTimeElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTimeElement' }));
extend(HTMLPictureElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLPictureElement' }));
extend(HTMLSourceElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLSourceElement' }));
extend(HTMLTrackElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTrackElement' }));
extend(HTMLEmbedElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLEmbedElement' }));
extend(HTMLObjectElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLObjectElement' }));
extend(HTMLParamElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLParamElement' }));
extend(HTMLMapElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLMapElement' }));
extend(HTMLAreaElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLAreaElement' }));
extend(HTMLBaseElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLBaseElement' }));
extend(HTMLTitleElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTitleElement' }));
extend(HTMLTableSectionElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTableSectionElement' }));
extend(HTMLTableCaptionElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLTableCaptionElement' }));
extend(HTMLColElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLColElement' }));
extend(HTMLColGroupElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLColGroupElement' }));
extend(HTMLUnknownElement.prototype, nonenumerable({ [Symbol.toStringTag]: 'HTMLUnknownElement' }));

/* Register tag-name → class mapping for Element element creation dispatch */
Element.elements = Object.assign(Object.create(null), {
  input: HTMLInputElement,
  button: HTMLButtonElement,
  form: HTMLFormElement,
  a: HTMLAnchorElement,
  img: HTMLImageElement,
  image: HTMLImageElement,
  textarea: HTMLTextAreaElement,
  select: HTMLSelectElement,
  option: HTMLOptionElement,
  script: HTMLScriptElement,
  style: HTMLStyleElement,
  link: HTMLLinkElement,
  video: HTMLVideoElement,
  audio: HTMLAudioElement,
  table: HTMLTableElement,
  tr: HTMLTableRowElement,
  th: HTMLTableCellElement,
  td: HTMLTableCellElement,
  label: HTMLLabelElement,
  li: HTMLLIElement,
  ol: HTMLOListElement,
  iframe: HTMLIFrameElement,
  meta: HTMLMetaElement,
  canvas: HTMLCanvasElement,
  dialog: HTMLDialogElement,
  head: HTMLHeadElement,
  body: HTMLBodyElement,
  html: HTMLHtmlElement,
  p: HTMLParagraphElement,
  h1: HTMLHeadingElement,
  h2: HTMLHeadingElement,
  h3: HTMLHeadingElement,
  h4: HTMLHeadingElement,
  h5: HTMLHeadingElement,
  h6: HTMLHeadingElement,
  pre: HTMLPreElement,
  blockquote: HTMLQuoteElement,
  q: HTMLQuoteElement,
  hr: HTMLHRElement,
  br: HTMLBRElement,
  datalist: HTMLDataListElement,
  fieldset: HTMLFieldSetElement,
  optgroup: HTMLOptGroupElement,
  legend: HTMLLegendElement,
  span: HTMLSpanElement,
  div: HTMLDivElement,
  progress: HTMLProgressElement,
  meter: HTMLMeterElement,
  details: HTMLDetailsElement,
  summary: HTMLSummaryElement,
  template: HTMLTemplateElement,
  slot: HTMLSlotElement,
  data: HTMLDataElement,
  time: HTMLTimeElement,
  picture: HTMLPictureElement,
  source: HTMLSourceElement,
  track: HTMLTrackElement,
  embed: HTMLEmbedElement,
  object: HTMLObjectElement,
  param: HTMLParamElement,
  map: HTMLMapElement,
  area: HTMLAreaElement,
  base: HTMLBaseElement,
  title: HTMLTitleElement,
  thead: HTMLTableSectionElement,
  tbody: HTMLTableSectionElement,
  tfoot: HTMLTableSectionElement,
  caption: HTMLTableCaptionElement,
  col: HTMLColElement,
  colgroup: HTMLColGroupElement,
  article: HTMLElement,
  section: HTMLElement,
  nav: HTMLElement,
  aside: HTMLElement,
  header: HTMLElement,
  footer: HTMLElement,
  main: HTMLElement,
  figure: HTMLElement,
  figcaption: HTMLElement,
  address: HTMLElement,
  mark: HTMLElement,
  small: HTMLElement,
  strong: HTMLElement,
  em: HTMLElement,
  b: HTMLElement,
  i: HTMLElement,
  u: HTMLElement,
  s: HTMLElement,
  sub: HTMLElement,
  sup: HTMLElement,
  code: HTMLElement,
  kbd: HTMLElement,
  samp: HTMLElement,
  var: HTMLElement,
  abbr: HTMLElement,
  cite: HTMLElement,
  dfn: HTMLElement,
  ins: HTMLElement,
  del: HTMLElement,
  wbr: HTMLElement,
  ul: HTMLElement,
  dl: HTMLElement,
  dt: HTMLElement,
  dd: HTMLElement,
  noscript: HTMLElement,
  center: HTMLElement,
  font: HTMLElement,
  dir: HTMLElement,
  menu: HTMLElement,
  ruby: HTMLElement,
  rt: HTMLElement,
  rp: HTMLElement,
  bdi: HTMLElement,
  bdo: HTMLElement,
  output: HTMLElement,
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
  constructor(obj, factory) {
    //DEBUG('Document.constructor', { obj, factory });

    super(obj, null, factory);

    if(!factories(this)) {
      const f = new Factory();
      Factory.set(this, f);
    }
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

  createDocumentFragment() {
    const frag = new DocumentFragment();
    ownerDocument(frag, this);
    return frag;
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

  get documentElement() {
    let element = this.firstElementChild;
    while(element) {
      if(/^html$/i.test(element.tagName)) return element;
      element = element.nextElementSibling;
    }
    return null;
  }

  getElementById(id) {
    if(!id) return null;

    const raw = Node.raw(this);
    if(!raw) return null;

    // Search through raw DOM structure
    const search = node => {
      if(!node) return null;

      // Check if current node is an element with matching id
      if(isObject(node) && 'tagName' in node && !('tagName' in node) === false) {
        if(node.attributes && node.attributes.id === id) {
          // Found it - return the wrapped Element
          return GetNode(node, this);
        }
      }

      // Search children
      if(node.children && Array.isArray(node.children)) {
        for(const child of node.children) {
          if(isObject(child) && 'tagName' in child) {
            const found = search(child);
            if(found) return found;
          }
        }
      }

      return null;
    };

    return search(raw);
  }

  [inspectSymbol](depth, opts) {
    return `\x1b[1;31m${className(this) || 'Document'}\x1b[0m`;
  }

  static [Symbol.hasInstance](obj) {
    return isObject(obj) && [Document.prototype].indexOf(Object.getPrototypeOf(obj)) != -1;
  }
}

extend(Document.prototype, nonenumerable({ [Symbol.toStringTag]: 'Document', nodeType: DOCUMENT_NODE }));

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

  [inspectSymbol]() {
    const [fn, name] = Node.raw(this);
    return `\x1b[1;31m${className(this) || 'Attr'}\x1b[0m { \x1b[1;35m${name}\x1b[1;34m=${quote(fn(), '"')}\x1b[0m }`;
  }
}

extend(
  Attr.prototype,
  nonenumerable({
    nodeType: ATTRIBUTE_NODE,
    [Symbol.toStringTag]: 'Attr',
  }),
);

//const charData = gettersetter(new WeakMap());

function setCharacterData(node, value) {
  const oldValue = textValues(node)();

  if(oldValue !== value) {
    textValues(node)(value);
    MutationObserver.eventFor(node, MutationRecord.characterData(node, oldValue));
  }

  return value;
}

export class CharacterData extends Node {
  constructor(gs, owner) {
    super(null, owner);

    if(isFunction(gs)) textValues(this, gs);
  }

  get data() {
    return textValues(this)();
  }

  set data(v) {
    setCharacterData(this, v);
  }

  appendData(data) {
    const s = textValues(this)() + data;
    setCharacterData(this, s);
    return s;
  }

  deleteData(offset, count) {
    const s = textValues(this)();
    setCharacterData(this, s.slice(0, offset) + s.slice(offset + count));
  }

  insertData(offset, data) {
    const s = textValues(this)();
    setCharacterData(this, s.slice(0, offset) + data + s.slice(offset));
  }

  replaceData(offset, count, data) {
    const s = textValues(this)();
    setCharacterData(this, s.slice(0, offset) + data + s.slice(offset + count));
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

  [inspectSymbol](depth, opts) {
    return `\x1b[1;31m${className(this) || 'Text'}\x1b[0m \x1b[38;2;192;2550m${quote(this.data, "'")}\x1b[0m`;
  }

  static cache = MakeCache2((key, owner) => new Text(key, owner));
}

//Object.setPrototypeOf(Text.prototype, Node.prototype);

extend(
  Text.prototype,
  nonenumerable({
    nodeType: TEXT_NODE,
    nodeName: '#text',
    [Symbol.toStringTag]: 'Text',
    get data() {
      return textValues(this)?.();
    },
    set data(v) {
      setCharacterData(this, v);
    },
    get nodeValue() {
      return textValues(this)?.();
    },
  }),
);

//extend(Text.prototype, Interface.prototype);

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

  [inspectSymbol](depth, opts) {
    return `\x1b[38;5;236m${className(this) || 'Comment'} \x1b[38;2;184;0;234m${this.data}\x1b[0m`;
  }

  static cache = MakeCache2((node, owner) => new Comment(node, owner));
}

Comment.prototype.__proto__ = Node.prototype;

extend(
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

  [inspectSymbol](depth, opts) {
    return `\x1b[1;31m${className(this) || 'TokenList'}\x1b[0m [` + [...this].join(',') + ']';
  }

  [Symbol.iterator]() {
    return this.values();
  }

  static tokens = Tokens;
}

extend(
  TokenList.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'TokenList',
  }),
);

const tokenListFacade = arrayFacade({}, (container, i) => container.item(i));

extend(TokenList.prototype, nonenumerable(tokenListFacade));
extend(TokenList.prototype, { [Symbol.toStringTag]: 'TokenList' });

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

  [inspectSymbol](depth, opts) {
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

extend(
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

extend(Serializer.prototype, nonenumerable({ [Symbol.toStringTag]: 'Serializer' }));

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

extend(ListAdapter.prototype, nonenumerable({ [Symbol.toStringTag]: 'ListAdapter' }));

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
  addedNodes = [];
  removedNodes = [];
  attributeName = null;
  attributeNamespace = null;
  nextSibling = null;
  previousSibling = null;
  oldValue = null;

  constructor(opts = {}) {
    define(this, opts);
  }

  static attribute(name, ns = null, target, oldValue) {
    return new MutationRecord({ type: 'attribute', attributeName: name, attributeNamespace: ns, target, oldValue });
  }

  static characterData(target, oldValue) {
    return new MutationRecord({ type: 'characterData', target, oldValue });
  }

  static childList(target, { addedNodes = [], removedNodes = [], nextSibling = null, previousSibling = null } = {}) {
    return new MutationRecord({ type: 'childList', target, addedNodes, removedNodes, nextSibling, previousSibling });
  }
}

MutationRecord.prototype[Symbol.toStringTag] = 'MutationRecord';

export class MutationObserver {
  #callback;
  static #observe = weakMapper(() => new Array(), new WeakMap());
  #targets = new Set();
  #queue = [];
  #scheduled = false;

  static observationsFor(target) {
    return MutationObserver.#observe.get(target) ?? [];
  }

  static eventFor(target, ...records) {
    const recordsFor = weakMapper(() => new Array(), new Map());

    for(let node = target; node; node = Node.parent(node)) {
      for(let { observer, ...options } of this.observationsFor(node)) {
        if(node !== target && !options.subtree) continue;

        for(let record of records) {
          const key = { attribute: 'attributes' }[record.type] ?? record.type;

          if(!options[key]) continue;
          if(record.type == 'attribute' && Array.isArray(options.attributeFilter) && !options.attributeFilter.includes(record.attributeName)) continue;

          const deliver =
            record.type == 'attribute' && !options.attributeOldValue
              ? new MutationRecord({ ...record, oldValue: null })
              : record.type == 'characterData' && !options.characterDataOldValue
                ? new MutationRecord({ ...record, oldValue: null })
                : record;

          recordsFor(observer).push(deliver);
        }
      }
    }

    for(let [observer, records] of recordsFor.map) observer.event(...records);
  }

  constructor(callback) {
    this.#callback = callback;
  }

  observe(target, options = {}) {
    let { subtree = false, childList = false, attributes, attributeFilter, attributeOldValue, characterData, characterDataOldValue } = options;

    if(attributes === undefined) attributes = isObject(attributeFilter) || attributeOldValue !== undefined;
    if(characterData === undefined) characterData = characterDataOldValue !== undefined;

    if(!(childList || attributes || characterData)) throw new TypeError(`MutationObserver.observe(): one of 'childList', 'attributes', or 'characterData' must be true`);
    if(attributes === false && (attributeOldValue !== undefined || attributeFilter !== undefined))
      throw new TypeError(`MutationObserver.observe(): 'attributeOldValue'/'attributeFilter' require 'attributes'`);
    if(characterData === false && characterDataOldValue !== undefined) throw new TypeError(`MutationObserver.observe(): 'characterDataOldValue' requires 'characterData'`);

    const observations = MutationObserver.#observe(target);
    const existing = observations.findIndex(o => o.observer === this);

    if(existing != -1) observations.splice(existing, 1);

    observations.push({ observer: this, subtree, childList, attributes, attributeFilter, attributeOldValue: !!attributeOldValue, characterData, characterDataOldValue: !!characterDataOldValue });
    this.#targets.add(target);
  }

  disconnect() {
    for(const target of this.#targets) {
      const observations = MutationObserver.#observe.get(target);

      if(observations) {
        const index = observations.findIndex(o => o.observer === this);

        if(index != -1) observations.splice(index, 1);
      }
    }

    this.#targets.clear();
    this.#queue = [];
  }

  takeRecords() {
    const records = this.#queue;
    this.#queue = [];
    return records;
  }

  event(...records) {
    if(!records.length) return;

    this.#queue.push(...records);

    if(!this.#scheduled) {
      this.#scheduled = true;

      queueMicrotask(() => {
        this.#scheduled = false;

        const records = this.takeRecords();

        if(records.length) this.#callback(records, this);
      });
    }
  }
}

MutationObserver.prototype[Symbol.toStringTag] = 'MutationObserver';

/* ========== Navigator ========== */

export class Navigator {
  #userAgent;
  #platform;
  #language;
  #languages;

  constructor(options = {}) {
    this.#userAgent = options.userAgent ?? 'QuickJS/1.0';
    this.#platform = options.platform ?? process.platform ?? 'unknown';
    this.#language = options.language ?? 'en-US';
    this.#languages = options.languages ?? Object.freeze(['en-US', 'en']);
  }

  get userAgent() {
    return this.#userAgent;
  }

  get platform() {
    return this.#platform;
  }

  get language() {
    return this.#language;
  }

  get languages() {
    return this.#languages;
  }

  get onLine() {
    return true;
  }

  get cookieEnabled() {
    return false;
  }

  get hardwareConcurrency() {
    return 1;
  }

  get maxTouchPoints() {
    return 0;
  }

  get vendor() {
    return '';
  }

  get appName() {
    return 'QuickJS';
  }

  get appVersion() {
    return this.#userAgent;
  }

  get product() {
    return 'QuickJS';
  }

  javaEnabled() {
    return false;
  }

  sendBeacon() {
    return false;
  }

  get mediaDevices() {
    return null;
  }

  get clipboard() {
    return null;
  }

  get geolocation() {
    return null;
  }

  get permissions() {
    return null;
  }
}

extend(Navigator.prototype, nonenumerable({ [Symbol.toStringTag]: 'Navigator' }));

/* ========== Location ========== */

export class Location {
  #href;

  constructor(href = 'about:blank') {
    this.#href = href;
  }

  get href() {
    return this.#href;
  }

  set href(value) {
    this.#href = value;
  }

  get protocol() {
    try {
      return new URL(this.#href).protocol;
    } catch {
      return '';
    }
  }

  get host() {
    try {
      return new URL(this.#href).host;
    } catch {
      return '';
    }
  }

  get hostname() {
    try {
      return new URL(this.#href).hostname;
    } catch {
      return '';
    }
  }

  get port() {
    try {
      return new URL(this.#href).port;
    } catch {
      return '';
    }
  }

  get pathname() {
    try {
      return new URL(this.#href).pathname;
    } catch {
      return '';
    }
  }

  get search() {
    try {
      return new URL(this.#href).search;
    } catch {
      return '';
    }
  }

  get hash() {
    try {
      return new URL(this.#href).hash;
    } catch {
      return '';
    }
  }

  get origin() {
    try {
      return new URL(this.#href).origin;
    } catch {
      return '';
    }
  }

  reload() {}

  replace(url) {
    this.#href = url;
  }

  assign(url) {
    this.#href = url;
  }

  toString() {
    return this.#href;
  }
}

extend(Location.prototype, nonenumerable({ [Symbol.toStringTag]: 'Location' }));

/* ========== Storage (localStorage / sessionStorage) ========== */

export class Storage {
  #data = new Map();

  get length() {
    return this.#data.size;
  }

  key(index) {
    return [...this.#data.keys()][index] ?? null;
  }

  getItem(key) {
    return this.#data.has(key) ? this.#data.get(key) : null;
  }

  setItem(key, value) {
    this.#data.set(String(key), String(value));
  }

  removeItem(key) {
    this.#data.delete(key);
  }

  clear() {
    this.#data.clear();
  }
}

extend(Storage.prototype, nonenumerable({ [Symbol.toStringTag]: 'Storage' }));

/* ========== Window ========== */

export class Window extends EventTarget {
  #document = null;
  #navigator;
  #location;
  #localStorage;
  #sessionStorage;
  #timers = new Map();
  #nextTimerId = 1;
  #rafCallbacks = new Map();
  #nextRafId = 1;

  constructor(options = {}) {
    super();

    this.#navigator = options.navigator ?? new Navigator(options);
    this.#location = options.location ?? new Location(options.href ?? 'about:blank');
    this.#localStorage = options.localStorage ?? new Storage();
    this.#sessionStorage = options.sessionStorage ?? new Storage();
  }

  /* self-referencing window */
  get window() {
    return this;
  }

  get self() {
    return this;
  }

  get globalThis() {
    return this;
  }

  get document() {
    return this.#document;
  }

  set document(doc) {
    this.#document = doc;
  }

  get navigator() {
    return this.#navigator;
  }

  get location() {
    return this.#location;
  }

  set location(value) {
    if(isString(value)) this.#location.href = value;
    else this.#location = value;
  }

  get localStorage() {
    return this.#localStorage;
  }

  get sessionStorage() {
    return this.#sessionStorage;
  }

  get console() {
    return console;
  }

  /* timer APIs */
  setTimeout(callback, delay = 0, ...args) {
    if(!isFunction(callback)) return 0;

    const id = this.#nextTimerId++;
    const timer = _setTimeout(() => {
      this.#timers.delete(id);
      callback(...args);
    }, delay);

    this.#timers.set(id, { timer, type: 'timeout' });
    return id;
  }

  clearTimeout(id) {
    const entry = this.#timers.get(id);

    if(entry?.type === 'timeout') {
      _clearTimeout(entry.timer);
      this.#timers.delete(id);
    }
  }

  setInterval(callback, delay = 0, ...args) {
    if(!isFunction(callback)) return 0;

    const id = this.#nextTimerId++;
    const timer = _setInterval(callback, delay, ...args);

    this.#timers.set(id, { timer, type: 'interval' });
    return id;
  }

  clearInterval(id) {
    const entry = this.#timers.get(id);

    if(entry?.type === 'interval') {
      _clearInterval(entry.timer);
      this.#timers.delete(id);
    }
  }

  requestAnimationFrame(callback) {
    if(!isFunction(callback)) return 0;

    const id = this.#nextRafId++;
    /* schedule via setTimeout(16ms) as approximation of ~60fps */
    const timer = _setTimeout(() => {
      this.#rafCallbacks.delete(id);
      callback(Date.now());
    }, 16);

    this.#rafCallbacks.set(id, timer);
    return id;
  }

  cancelAnimationFrame(id) {
    const timer = this.#rafCallbacks.get(id);

    if(timer !== undefined) {
      _clearTimeout(timer);
      this.#rafCallbacks.delete(id);
    }
  }

  /* stub dialog APIs */
  alert(message) {
    console.log('[alert]', message);
  }

  confirm(message) {
    console.log('[confirm]', message);
    return true;
  }

  prompt(message, defaultValue = '') {
    console.log('[prompt]', message);
    return defaultValue;
  }

  /* stub fetch */
  async fetch(input, init) {
    throw new Error('fetch is not available in this environment');
  }

  /* atob / btoa */
  atob(data) {
    return Buffer.from(data, 'base64').toString('binary');
  }

  btoa(data) {
    return Buffer.from(data, 'binary').toString('base64');
  }

  /* performance stub */
  get performance() {
    return {
      now() {
        return Date.now();
      },
      timing: null,
      navigation: null,
    };
  }

  /* queueMicrotask pass-through */
  queueMicrotask(callback) {
    queueMicrotask(callback);
  }

  /* innerWidth / innerHeight stubs */
  get innerWidth() {
    return 1024;
  }

  get innerHeight() {
    return 768;
  }

  get outerWidth() {
    return 1024;
  }

  get outerHeight() {
    return 768;
  }

  get devicePixelRatio() {
    return 1;
  }

  get screenX() {
    return 0;
  }

  get screenY() {
    return 0;
  }

  get scrollX() {
    return 0;
  }

  get scrollY() {
    return 0;
  }

  get pageXOffset() {
    return 0;
  }

  get pageYOffset() {
    return 0;
  }

  scrollTo() {}

  scrollBy() {}

  /* close stub */
  close() {}

  /* stop stub */
  stop() {}

  /* focus / blur stubs */
  focus() {}

  blur() {}

  /* print stub */
  print() {}

  /* open stub */
  open() {
    return null;
  }

  /* postMessage stub */
  postMessage() {}

  /* getComputedStyle stub */
  getComputedStyle() {
    return new CSSStyleDeclaration({});
  }

  /* matchMedia stub */
  matchMedia() {
    return {
      matches: false,
      media: '',
      addListener() {},
      removeListener() {},
      addEventListener() {},
      removeEventListener() {},
      dispatchEvent() {
        return true;
      },
    };
  }

  /* cleanup all timers (call when shutting down) */
  cleanup() {
    for(const [id, entry] of this.#timers) {
      if(entry.type === 'timeout') _clearTimeout(entry.timer);
      else _clearInterval(entry.timer);
    }
    this.#timers.clear();

    for(const [id, timer] of this.#rafCallbacks) _clearTimeout(timer);
    this.#rafCallbacks.clear();
  }
}

extend(Window.prototype, nonenumerable({ [Symbol.toStringTag]: 'Window' }));

/* ========== DocumentFragment (9.4) ========== */

export class DocumentFragment extends Node {
  constructor(children = []) {
    super({ children }, null);
  }

  get nodeType() {
    return DOCUMENT_FRAGMENT_NODE;
  }

  get nodeName() {
    return '#document-fragment';
  }

  appendChild(node) {
    if(node instanceof DocumentFragment) {
      const raw = Node.raw(this);
      const fragRaw = Node.raw(node);

      if(!fragRaw.children || fragRaw.children.length === 0) return node;

      const children = (raw.children ??= []);

      for(const childRaw of fragRaw.children) {
        children.push(childRaw);

        const wrappedChild = GetNode(childRaw, this.childNodes);
        setParentOwner(wrappedChild, null);
        parentNodes(wrappedChild, this);
      }

      fragRaw.children = [];

      return node;
    }

    return super.appendChild(node);
  }
}

extend(
  DocumentFragment.prototype,
  nonenumerable({
    [Symbol.toStringTag]: 'DocumentFragment',
    nodeType: DOCUMENT_FRAGMENT_NODE,
  }),
);

/* Install timer APIs on globalThis for browser-like environment */
export function installTimers(target = globalThis) {
  if(!target.setTimeout) target.setTimeout = _setTimeout;
  if(!target.clearTimeout) target.clearTimeout = _clearTimeout;
  if(!target.setInterval) target.setInterval = _setInterval;
  if(!target.clearInterval) target.clearInterval = _clearInterval;
  if(!target.queueMicrotask) target.queueMicrotask = queueMicrotask;
}
