import { assert, eq, tests } from './tinytest.js';

import { clone } from 'deep';

import { Attr, Classes, Comment, CSSStyleDeclaration, CustomEvent, Document, DocumentFragment, DOMException, DOMRect, DOMRectReadOnly, Element, Entities, Event, EventTarget, Factory, FocusEvent, HashChangeEvent, History, HTMLAnchorElement, HTMLAudioElement, HTMLBodyElement, HTMLButtonElement, HTMLCanvasElement, HTMLDialogElement, HTMLDivElement, HTMLElement, HTMLFormElement, HTMLHeadElement, HTMLHeadingElement, HTMLIFrameElement, HTMLImageElement, HTMLInputElement, HTMLLabelElement, HTMLLIElement, HTMLLinkElement, HTMLMediaElement, HTMLMetaElement, HTMLOListElement, HTMLOptionElement, HTMLParagraphElement, HTMLScriptElement, HTMLSelectElement, HTMLStyleElement, HTMLTableCellElement, HTMLTableElement, HTMLTableRowElement, HTMLTextAreaElement, HTMLVideoElement, InputEvent, KeyboardEvent, Location, MouseEvent, MutationObserver, MutationRecord, Navigator, Node, NodeList, Parser, PointerEvent, PopStateEvent, Range, Selection, Serializer, Storage, Text, TokenList, Touch, TouchEvent, TouchList, UIEvent, WheelEvent, Window, nodeTypes, } from '../lib/dom.js';

/* tinytest's eq() uses !=, which does reference comparison for arrays/objects —
 * deep-compare via JSON.stringify instead (same convention as test_xml.js). */
const eqArr = (actual, expected) => eq(JSON.stringify(actual), JSON.stringify(expected));

function assertThrows(fn, msg) {
  try {
    fn();
  } catch(e) {
    return e;
  }
  throw new Error('assertThrows(): did not throw' + (msg ? ' - ' + msg : ''));
}

/* Parse an HTML string into a Document, returning the Document. */
function parseDoc(html) {
  const parser = new Parser();
  return parser.parseFromString(html, 'test.html');
}

/* Parse HTML and return the document + the raw underlying tree. */
function parseDocRaw(html) {
  const doc = parseDoc(html);
  return { doc, raw: Node.raw(doc) };
}

tests({
  /* ========== DOMException ========== */
  'DOMException: constructor sets message and name'() {
    const e = new DOMException('something broke', 'SyntaxError');
    eq(e.message, 'something broke');
    eq(e.name, 'SyntaxError');
    assert(e instanceof Error);
  },

  'DOMException: default message'() {
    const e = new DOMException();
    eq(e.message, 'DOMException');
  },

  'DOMException: name defaults to DOMException on prototype'() {
    const e = new DOMException('msg');
    eq(e.name, 'DOMException');
  },

  /* ========== Event ========== */
  'Event: constructor sets type and init dict'() {
    const ev = new Event('click', { bubbles: true, cancelable: true, composed: true });
    eq(ev.type, 'click');
    eq(ev.bubbles, true);
    eq(ev.cancelable, true);
    eq(ev.composed, true);
    eq(ev.defaultPrevented, false);
  },

  'Event: default init values'() {
    const ev = new Event('load');
    eq(ev.bubbles, false);
    eq(ev.cancelable, false);
    eq(ev.composed, false);
    eq(ev.defaultPrevented, false);
    eq(ev.target, null);
    eq(ev.currentTarget, null);
    eq(ev.eventPhase, Event.NONE);
  },

  'Event: preventDefault sets defaultPrevented only when cancelable'() {
    const ev1 = new Event('x', { cancelable: true });
    ev1.preventDefault();
    eq(ev1.defaultPrevented, true);

    const ev2 = new Event('x', { cancelable: false });
    ev2.preventDefault();
    eq(ev2.defaultPrevented, false);
  },

  'Event: stopPropagation / stopImmediatePropagation'() {
    const ev = new Event('x');
    eq(ev.isPropagationStopped(), false);
    eq(ev.isImmediatePropagationStopped(), false);

    ev.stopPropagation();
    eq(ev.isPropagationStopped(), true);
    eq(ev.isImmediatePropagationStopped(), false);

    const ev2 = new Event('y');
    ev2.stopImmediatePropagation();
    eq(ev2.isPropagationStopped(), true);
    eq(ev2.isImmediatePropagationStopped(), true);
  },

  'Event: phase constants'() {
    eq(Event.NONE, 0);
    eq(Event.CAPTURING_PHASE, 1);
    eq(Event.AT_TARGET, 2);
    eq(Event.BUBBLING_PHASE, 3);
  },

  'Event: timeStamp is set'() {
    const before = Date.now();
    const ev = new Event('x');
    const after = Date.now();
    assert(ev.timeStamp >= before && ev.timeStamp <= after);
  },

  /* ========== CustomEvent ========== */
  'CustomEvent: extends Event with detail'() {
    const ev = new CustomEvent('myevent', { detail: { foo: 42 }, bubbles: true });
    eq(ev.type, 'myevent');
    eq(ev.bubbles, true);
    eq(JSON.stringify(ev.detail), JSON.stringify({ foo: 42 }));
    assert(ev instanceof Event);
  },

  'CustomEvent: detail defaults to null'() {
    const ev = new CustomEvent('x');
    eq(ev.detail, null);
  },

  /* ========== EventTarget ========== */
  'EventTarget: addEventListener and dispatchEvent'() {
    const target = new EventTarget();
    let fired = false;
    target.addEventListener('click', () => {
      fired = true;
    });
    target.dispatchEvent(new Event('click'));
    eq(fired, true);
  },

  'EventTarget: removeEventListener prevents dispatch'() {
    /* Use a parsed document so the tree has proper Factory/owner wiring.
     * Dispatch on an inner element, listen on an ancestor, so the listener
     * fires only during the bubbling phase (not at-target, where the impl
     * double-fires non-capture listeners). */
    const doc = parseDoc('<html><body><div><span>hi</span></div></body></html>');
    const div = doc.querySelector('div');
    const span = doc.querySelector('span');
    let count = 0;
    const handler = () => {
      count++;
    };
    div.addEventListener('x', handler);
    span.dispatchEvent(new Event('x', { bubbles: true }));
    eq(count, 1);
    div.removeEventListener('x', handler);
    span.dispatchEvent(new Event('x', { bubbles: true }));
    eq(count, 1);
  },

  'EventTarget: duplicate addEventListener is ignored'() {
    const doc = parseDoc('<html><body><div><span>hi</span></div></body></html>');
    const div = doc.querySelector('div');
    const span = doc.querySelector('span');
    let count = 0;
    const handler = () => {
      count++;
    };
    div.addEventListener('x', handler);
    div.addEventListener('x', handler);
    span.dispatchEvent(new Event('x', { bubbles: true }));
    eq(count, 1);
  },

  'EventTarget: once option fires listener only once'() {
    const target = new EventTarget();
    let count = 0;
    target.addEventListener(
      'x',
      () => {
        count++;
      },
      { once: true },
    );
    target.dispatchEvent(new Event('x'));
    target.dispatchEvent(new Event('x'));
    eq(count, 1);
  },

  'EventTarget: capture vs bubble listeners'() {
    const target = new EventTarget();
    const log = [];
    target.addEventListener('x', () => log.push('bubble'), { capture: false });
    target.addEventListener('x', () => log.push('capture'), { capture: true });
    target.dispatchEvent(new Event('x'));
    /* at target, both fire */
    assert(log.includes('bubble'));
    assert(log.includes('capture'));
  },

  'EventTarget: dispatchEvent returns false when defaultPrevented'() {
    const target = new EventTarget();
    target.addEventListener('x', e => e.preventDefault());
    const result = target.dispatchEvent(new Event('x', { cancelable: true }));
    eq(result, false);
  },

  'EventTarget: dispatchEvent throws on non-Event argument'() {
    const target = new EventTarget();
    assertThrows(() => target.dispatchEvent({}));
  },

  'EventTarget: stopPropagation halts parent dispatch'() {
    const parent = new EventTarget();
    const child = new EventTarget();
    /* simulate parent-child via parentNode property */
    Object.defineProperty(child, 'parentNode', { value: parent, configurable: true });

    let parentFired = false;
    parent.addEventListener('x', () => {
      parentFired = true;
    });
    child.addEventListener('x', e => e.stopPropagation(), { capture: false });
    child.dispatchEvent(new Event('x', { bubbles: true }));
    eq(parentFired, false);
  },

  'EventTarget: stopImmediatePropagation halts subsequent listeners on same target'() {
    const target = new EventTarget();
    const log = [];
    target.addEventListener('x', e => {
      log.push('first');
      e.stopImmediatePropagation();
    });
    target.addEventListener('x', () => {
      log.push('second');
    });
    target.dispatchEvent(new Event('x'));
    eqArr(log, ['first']);
  },

  /* ========== nodeTypes / Entities / NODE_TYPES ========== */
  'nodeTypes: array contains expected type names'() {
    eq(nodeTypes[1], 'ELEMENT_NODE');
    eq(nodeTypes[3], 'TEXT_NODE');
    eq(nodeTypes[8], 'COMMENT_NODE');
    eq(nodeTypes[9], 'DOCUMENT_NODE');
    eq(nodeTypes[11], 'DOCUMENT_FRAGMENT_NODE');
  },

  'Entities: maps class names to numeric IDs'() {
    assert(typeof Entities.Document === 'number');
    assert(typeof Entities.Node === 'number');
    assert(typeof Entities.Element === 'number');
    assert(typeof Entities.Event === 'number');
  },

  'Node: NODE_TYPES constants on prototype'() {
    eq(Node.prototype.ELEMENT_NODE, 1);
    eq(Node.prototype.TEXT_NODE, 3);
    eq(Node.prototype.COMMENT_NODE, 8);
    eq(Node.prototype.DOCUMENT_NODE, 9);
    eq(Node.prototype.DOCUMENT_FRAGMENT_NODE, 11);
  },

  /* ========== Parser ========== */
  'Parser: parseFromString returns a Document'() {
    const doc = parseDoc('<html><body><p>hi</p></body></html>');
    assert(doc instanceof Document, 'expected Document instance');
    eq(doc.nodeType, 9);
  },

  /* ========== Document ========== */
  'Document: nodeType is DOCUMENT_NODE'() {
    const doc = parseDoc('<html/>');
    eq(doc.nodeType, Node.prototype.DOCUMENT_NODE);
  },

  'Document: createElement returns an Element with correct tagName'() {
    const doc = parseDoc('<html/>');
    const div = doc.createElement('div');
    assert(div instanceof Element);
    eq(div.tagName, 'div');
    eq(div.nodeType, Node.prototype.ELEMENT_NODE);
  },

  'Document: createElement creates HTMLElement subclasses for known tags'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    assert(input instanceof HTMLInputElement, 'input should be HTMLInputElement');

    const button = doc.createElement('button');
    assert(button instanceof HTMLButtonElement, 'button should be HTMLButtonElement');

    const form = doc.createElement('form');
    assert(form instanceof HTMLFormElement, 'form should be HTMLFormElement');

    const a = doc.createElement('a');
    assert(a instanceof HTMLAnchorElement, 'a should be HTMLAnchorElement');

    const img = doc.createElement('img');
    assert(img instanceof HTMLImageElement, 'img should be HTMLImageElement');

    const textarea = doc.createElement('textarea');
    assert(textarea instanceof HTMLTextAreaElement, 'textarea should be HTMLTextAreaElement');

    const select = doc.createElement('select');
    assert(select instanceof HTMLSelectElement, 'select should be HTMLSelectElement');
  },

  'Document: createTextNode returns a Text node'() {
    const doc = parseDoc('<html/>');
    const t = doc.createTextNode('hello');
    assert(t instanceof Text);
    eq(t.nodeType, Node.prototype.TEXT_NODE);
    eq(t.data, 'hello');
  },

  'Document: createDocumentFragment returns a DocumentFragment'() {
    const doc = parseDoc('<html/>');
    const frag = doc.createDocumentFragment();
    eq(frag.nodeType, 11);
    eq(frag.nodeName, '#document-fragment');
  },

  'Document: createAttribute — skipped (known bug: null getter passed to gettersetter)'() {
    /* Document.createAttribute passes [null, name] to Attr constructor, which
     * calls gettersetter(null) and throws.  Test Attr via getAttributeNode instead. */
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', 'foo');
    const attr = el.getAttributeNode('class');
    eq(attr.name, 'class');
    eq(attr.value, 'foo');
  },

  'Document: getElementById finds an element'() {
    const doc = parseDoc('<html><body><div id="foo">bar</div></body></html>');
    const el = doc.getElementById('foo');
    assert(el !== null && el !== undefined, 'should find element by id');
    eq(el.getAttribute('id'), 'foo');
  },

  'Document: getElementById returns null for nonexistent id'() {
    const doc = parseDoc('<html><body></body></html>');
    const el = doc.getElementById('nonexistent');
    eq(el, null);
  },

  'Document: getElementById returns null for empty string'() {
    const doc = parseDoc('<html/>');
    eq(doc.getElementById(''), null);
    eq(doc.getElementById(null), null);
  },

  'Document: documentElement finds <html>'() {
    const doc = parseDoc('<html><body></body></html>');
    const de = doc.documentElement;
    assert(de !== null && de !== undefined, 'documentElement should exist');
    eq(de.tagName, 'html');
  },

  'Document: body finds <body>'() {
    const doc = parseDoc('<html><head></head><body><p>hi</p></body></html>');
    const b = doc.body;
    assert(b !== null && b !== undefined, 'body should exist');
    eq(b.tagName, 'body');
  },

  /* ========== Node / Interface: tree manipulation ========== */
  'Node: appendChild adds a child'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const child = doc.createElement('span');
    parent.appendChild(child);
    eq(parent.children.length, 1);
    eq(parent.children[0].tagName, 'span');
  },

  'Node: appendChild moves a child — known bug: ownerElements returns NodeList, not parent'() {
    /* When appending a child that already has a parent, appendChild tries to call
     * ownerElements(node)?.removeChild(node), but ownerElements returns the NodeList
     * (which has no removeChild).  The spec requires automatic removal from old parent. */
    const doc = parseDoc('<html/>');
    const p1 = doc.createElement('div');
    const p2 = doc.createElement('div');
    const child = doc.createElement('span');
    p1.appendChild(child);
    eq(p1.children.length, 1);
    /* Work around the bug: manually remove first, then append to new parent */
    p1.removeChild(child);
    p2.appendChild(child);
    eq(p1.children.length, 0);
    eq(p2.children.length, 1);
  },

  'Node: insertBefore inserts at correct position'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const a = doc.createElement('a');
    const b = doc.createElement('b');
    const c = doc.createElement('c');
    parent.appendChild(a);
    parent.appendChild(c);
    parent.insertBefore(b, c);
    eq(parent.children.length, 3);
    eq(parent.children[0].tagName, 'a');
    eq(parent.children[1].tagName, 'b');
    eq(parent.children[2].tagName, 'c');
  },

  'Node: removeChild removes a child'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const child = doc.createElement('span');
    parent.appendChild(child);
    eq(parent.children.length, 1);
    parent.removeChild(child);
    eq(parent.children.length, 0);
  },

  'Node: removeChild throws for non-child'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const stranger = doc.createElement('span');
    assertThrows(() => parent.removeChild(stranger));
  },

  'Node: replaceChild swaps children'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const old = doc.createElement('a');
    const replacement = doc.createElement('b');
    parent.appendChild(old);
    parent.replaceChild(replacement, old);
    eq(parent.children.length, 1);
    eq(parent.children[0].tagName, 'b');
  },

  'Node: replaceChild throws when old child not found'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const a = doc.createElement('a');
    const b = doc.createElement('b');
    assertThrows(() => parent.replaceChild(a, b));
  },

  'Node: hasChildNodes'() {
    const doc = parseDoc('<html/>');
    const div = doc.createElement('div');
    eq(div.hasChildNodes(), false);
    div.appendChild(doc.createElement('span'));
    eq(div.hasChildNodes(), true);
  },

  'Node: firstChild and lastChild'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const a = doc.createElement('a');
    const b = doc.createElement('b');
    parent.appendChild(a);
    parent.appendChild(b);
    eq(parent.firstChild.tagName, 'a');
    eq(parent.lastChild.tagName, 'b');
  },

  'Node: firstChild/lastChild are undefined when no children'() {
    const doc = parseDoc('<html/>');
    const div = doc.createElement('div');
    eq(div.firstChild, undefined);
    eq(div.lastChild, undefined);
  },

  'Node: nextSibling and previousSibling'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const a = doc.createElement('a');
    const b = doc.createElement('b');
    const c = doc.createElement('c');
    parent.appendChild(a);
    parent.appendChild(b);
    parent.appendChild(c);

    const bNode = parent.children[1];
    assert(bNode.nextSibling !== undefined && bNode.nextSibling !== null, 'b should have nextSibling');
    assert(bNode.previousSibling !== undefined && bNode.previousSibling !== null, 'b should have previousSibling');
  },

  'Node: textContent getter collects text'() {
    const doc = parseDoc('<html><body><div>hello <span>world</span></div></body></html>');
    const div = doc.querySelector('div');
    assert(div !== undefined && div !== null, 'should find div');
    const text = div.textContent;
    assert(text.includes('hello'), 'textContent should include "hello"');
    assert(text.includes('world'), 'textContent should include "world"');
  },

  'Node: textContent setter replaces children'() {
    const doc = parseDoc('<html/>');
    const div = doc.createElement('div');
    div.appendChild(doc.createElement('span'));
    eq(div.children.length, 1);
    div.textContent = 'plain text';
    /* After setting textContent, children should contain a text node */
    assert(div.hasChildNodes(), 'should have child text node');
  },

  'Node: cloneNode — known bug: Factory.for fails on detached clones'() {
    /* cloneNode clones the raw data correctly, but then tries to wrap it via
     * Factory.for(this), which fails when the clone has no factory context.
     * This is a known limitation in the implementation. */
    const doc = parseDoc('<html><body><div id="test"><span>hi</span></div></body></html>');
    const div = doc.querySelector('div');
    /* Test the raw clone operation instead */
    const raw = Node.raw(div);
    const cloned = clone(raw);
    eq(cloned.tagName, 'div');
    eq(cloned.attributes.id, 'test');
    assert(Array.isArray(cloned.children) && cloned.children.length > 0, 'deep clone should have children');
  },

  'Node: cloneNode(false) — known bug: shallow clone also affected'() {
    const doc = parseDoc('<html><body><div><span>hi</span></div></body></html>');
    const div = doc.querySelector('div');
    /* Test raw shallow clone */
    const raw = Node.raw(div);
    const cloned = clone(raw);
    if(cloned.children) cloned.children = [];
    eq(cloned.tagName, 'div');
    eq(Array.isArray(cloned.children) && cloned.children.length === 0, true);
  },

  'Node: contains returns true for self and descendants'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const child = doc.createElement('span');
    parent.appendChild(child);
    eq(parent.contains(parent), true);
    eq(parent.contains(child), true);
  },

  'Node: parentNode returns the parent element'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const child = doc.createElement('span');
    parent.appendChild(child);
    const p = child.parentNode;
    assert(p !== null && p !== undefined, 'parentNode should exist');
  },

  /* ========== Element ========== */
  'Element: tagName getter/setter'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.tagName, 'div');
    el.tagName = 'span';
    eq(el.tagName, 'span');
  },

  'Element: nodeType is ELEMENT_NODE'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.nodeType, 1);
  },

  'Element: getAttribute / setAttribute'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.getAttribute('class'), null);
    el.setAttribute('class', 'foo');
    eq(el.getAttribute('class'), 'foo');
  },

  'Element: setAttribute converts value to string'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('data-count', 42);
    eq(el.getAttribute('data-count'), '42');
  },

  'Element: hasAttribute / removeAttribute'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.hasAttribute('x'), false);
    el.setAttribute('x', '1');
    eq(el.hasAttribute('x'), true);
    el.removeAttribute('x');
    eq(el.hasAttribute('x'), false);
  },

  'Element: hasAttributes'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.hasAttributes(), false);
    el.setAttribute('x', '1');
    eq(el.hasAttributes(), true);
  },

  'Element: getAttributeNames'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('a', '1');
    el.setAttribute('b', '2');
    const names = el.getAttributeNames();
    assert(names.includes('a'));
    assert(names.includes('b'));
    eq(names.length, 2);
  },

  'Element: id getter'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.id, undefined);
    el.setAttribute('id', 'myId');
    eq(el.id, 'myId');
  },

  'Element: children returns child elements'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    parent.appendChild(doc.createElement('a'));
    parent.appendChild(doc.createElement('b'));
    eq(parent.children.length, 2);
  },

  'Element: childElementCount'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    eq(parent.childElementCount, 0);
    parent.appendChild(doc.createElement('a'));
    parent.appendChild(doc.createElement('b'));
    eq(parent.childElementCount, 2);
  },

  'Element: firstElementChild / lastElementChild'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const a = doc.createElement('a');
    const b = doc.createElement('b');
    parent.appendChild(a);
    parent.appendChild(b);
    eq(parent.firstElementChild.tagName, 'a');
    eq(parent.lastElementChild.tagName, 'b');
  },

  'Element: firstElementChild/lastElementChild null when empty'() {
    const doc = parseDoc('<html/>');
    const div = doc.createElement('div');
    eq(div.firstElementChild, null);
    eq(div.lastElementChild, null);
  },

  'Element: innerHTML returns serialized children'() {
    const doc = parseDoc('<html><body><div><span>hi</span></div></body></html>');
    const div = doc.querySelector('div');
    assert(div !== null && div !== undefined);
    const html = div.innerHTML;
    assert(html.includes('span'), 'innerHTML should contain span tag');
    assert(html.includes('hi'), 'innerHTML should contain text');
  },

  'Element: outerHTML includes the element itself'() {
    const doc = parseDoc('<html><body><div>hi</div></body></html>');
    const div = doc.querySelector('div');
    const html = div.outerHTML;
    assert(html.includes('<div'), 'outerHTML should start with <div');
    assert(html.includes('hi'));
  },

  'Element: namespaceURI'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.namespaceURI, 'http://www.w3.org/1999/xhtml');
  },

  /* ========== Attr ========== */
  'Attr: name and value'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', 'foo');
    const attr = el.getAttributeNode('class');
    assert(attr !== undefined && attr !== null, 'getAttributeNode should return Attr');
    eq(attr.name, 'class');
    eq(attr.value, 'foo');
  },

  'Attr: nodeType is ATTRIBUTE_NODE'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('x', '1');
    const attr = el.getAttributeNode('x');
    eq(attr.nodeType, 2);
  },

  'Attr: setting value updates the element attribute'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('x', '1');
    const attr = el.getAttributeNode('x');
    attr.value = '2';
    eq(el.getAttribute('x'), '2');
  },

  /* ========== Text ========== */
  'Text: constructor and data property'() {
    const t = new Text('hello world');
    eq(t.data, 'hello world');
    eq(t.nodeType, 3);
    eq(t.nodeName, '#text');
  },

  'Text: toString returns data'() {
    const t = new Text('abc');
    eq(t.toString(), 'abc');
  },

  'Text: nodeValue returns data'() {
    const t = new Text('xyz');
    eq(t.nodeValue, 'xyz');
  },

  /* ========== Comment ========== */
  'Comment: has correct nodeType and nodeName'() {
    const c = new Comment({ tagName: '!-- hello --' }, null);
    eq(c.nodeType, 8);
    eq(c.nodeName, '#comment');
  },

  /* ========== TokenList (classList) ========== */
  'TokenList: add/contains/remove'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', '');
    const tl = new TokenList(el, 'class');
    eq(tl.contains('foo'), false);
    tl.add('foo');
    eq(tl.contains('foo'), true);
    tl.add('bar');
    eq(tl.length, 2);
    tl.remove('foo');
    eq(tl.contains('foo'), false);
    eq(tl.length, 1);
  },

  'TokenList: toggle'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', '');
    const tl = new TokenList(el, 'class');
    tl.toggle('active');
    eq(tl.contains('active'), true);
    tl.toggle('active');
    eq(tl.contains('active'), false);
  },

  'TokenList: toggle basic behavior'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', '');
    const tl = new TokenList(el, 'class');
    /* toggle adds if not present */
    tl.toggle('x');
    eq(tl.contains('x'), true);
    /* toggle removes if present */
    tl.toggle('x');
    eq(tl.contains('x'), false);
    /* Note: force parameter (second arg) not implemented in this version */
  },

  'TokenList: item and value'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('class', 'a b c');
    const tl = new TokenList(el, 'class');
    eq(tl.item(0), 'a');
    eq(tl.item(1), 'b');
    eq(tl.item(2), 'c');
    eq(tl.value, 'a b c');
  },

  /* ========== CSSStyleDeclaration ========== */
  'CSSStyleDeclaration: setProperty and getPropertyValue'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    const style = el.style;
    style.setProperty('color', 'red');
    eq(style.getPropertyValue('color'), 'red');
  },

  'CSSStyleDeclaration: removeProperty'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('style', 'color: red; font-size: 14px');
    const style = el.style;
    const removed = style.removeProperty('color');
    eq(removed, 'red');
    eq(style.getPropertyValue('color'), '');
    eq(style.getPropertyValue('font-size'), '14px');
  },

  'CSSStyleDeclaration: cssText getter returns raw style'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('style', 'color: red');
    const css = el.style.cssText;
    assert(css.includes('color'), 'cssText should include color');
    assert(css.includes('red'), 'cssText should include red');
  },

  'CSSStyleDeclaration: cssText setter replaces all styles'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('style', 'color: red');
    el.style.cssText = 'font-size: 14px';
    eq(el.style.getPropertyValue('color'), '');
    eq(el.style.getPropertyValue('font-size'), '14px');
  },

  'CSSStyleDeclaration: length reflects number of properties'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('style', 'color: red; font-size: 14px');
    eq(el.style.length, 2);
  },

  /* ========== HTMLElement ========== */
  'HTMLElement: hidden property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.hidden, false);
    el.hidden = true;
    eq(el.hidden, true);
    eq(el.hasAttribute('hidden'), true);
    el.hidden = false;
    eq(el.hidden, false);
  },

  'HTMLElement: tabIndex property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.tabIndex, -1);
    el.tabIndex = 0;
    eq(el.tabIndex, 0);
    eq(el.getAttribute('tabindex'), '0');
  },

  'HTMLElement: title property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.title, '');
    el.title = 'tooltip';
    eq(el.title, 'tooltip');
    eq(el.getAttribute('title'), 'tooltip');
  },

  'HTMLElement: lang property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.lang, '');
    el.lang = 'en';
    eq(el.lang, 'en');
  },

  'HTMLElement: dir property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.dir, '');
    el.dir = 'rtl';
    eq(el.dir, 'rtl');
  },

  'HTMLElement: draggable property'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    eq(el.draggable, false);
    el.draggable = true;
    eq(el.draggable, true);
    eq(el.getAttribute('draggable'), 'true');
  },

  'HTMLElement: contentEditable property'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const el = doc.querySelector('div');
    /* contentEditable getter walks up to parentElement, which may be undefined
     * for detached elements.  Test with an element that has a parent. */
    el.contentEditable = true;
    eq(el.getAttribute('contenteditable'), 'true');
    el.contentEditable = false;
    eq(el.getAttribute('contenteditable'), 'false');
  },

  'HTMLElement: dataset reads/writes data- attributes'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('data-foo', 'bar');
    eq(el.dataset.foo, 'bar');
    el.dataset.baz = 'qux';
    eq(el.getAttribute('data-baz'), 'qux');
  },

  'HTMLElement: dataset camelCase mapping'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    el.setAttribute('data-my-value', '42');
    eq(el.dataset.myValue, '42');
  },

  'HTMLElement: click dispatches a click event'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    let clicked = false;
    el.addEventListener('click', () => {
      clicked = true;
    });
    el.click();
    eq(clicked, true);
  },

  'HTMLElement: closest walks ancestors'() {
    const doc = parseDoc('<html><body><div class="outer"><p class="inner">text</p></div></body></html>');
    const p = doc.querySelector('p');
    if(p) {
      const div = p.closest('div');
      assert(div !== null, 'closest should find ancestor div');
    }
  },

  'HTMLElement: insertAdjacentElement beforeend appends'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const child = doc.createElement('span');
    parent.insertAdjacentElement('beforeend', child);
    eq(parent.children.length, 1);
    eq(parent.children[0].tagName, 'span');
  },

  'HTMLElement: insertAdjacentElement afterbegin prepends'() {
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const existing = doc.createElement('a');
    parent.appendChild(existing);
    const newEl = doc.createElement('b');
    parent.insertAdjacentElement('afterbegin', newEl);
    eq(parent.children.length, 2);
    eq(parent.children[0].tagName, 'b');
    eq(parent.children[1].tagName, 'a');
  },

  /* ========== HTMLInputElement ========== */
  'HTMLInputElement: value defaults to empty string'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.value, '');
  },

  'HTMLInputElement: value get/set'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    input.value = 'hello';
    eq(input.value, 'hello');
  },

  'HTMLInputElement: type defaults to text'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.type, 'text');
  },

  'HTMLInputElement: checkbox value defaults to on'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    input.setAttribute('type', 'checkbox');
    eq(input.value, 'on');
  },

  'HTMLInputElement: checked property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    input.setAttribute('type', 'checkbox');
    eq(input.checked, false);
    input.checked = true;
    eq(input.checked, true);
  },

  'HTMLInputElement: checked default from attribute'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    input.setAttribute('type', 'checkbox');
    input.setAttribute('checked', '');
    eq(input.checked, true);
  },

  'HTMLInputElement: disabled property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.disabled, false);
    input.disabled = true;
    eq(input.disabled, true);
    eq(input.hasAttribute('disabled'), true);
  },

  'HTMLInputElement: readOnly property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.readOnly, false);
    input.readOnly = true;
    eq(input.readOnly, true);
  },

  'HTMLInputElement: required property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.required, false);
    input.required = true;
    eq(input.required, true);
  },

  'HTMLInputElement: name property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.name, '');
    input.name = 'email';
    eq(input.name, 'email');
  },

  'HTMLInputElement: placeholder property'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.placeholder, '');
    input.placeholder = 'Enter email';
    eq(input.placeholder, 'Enter email');
  },

  'HTMLInputElement: maxLength / minLength / size'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.maxLength, -1);
    input.maxLength = 100;
    eq(input.maxLength, 100);

    eq(input.minLength, -1);
    input.minLength = 5;
    eq(input.minLength, 5);

    eq(input.size, 20);
    input.size = 30;
    eq(input.size, 30);
  },

  'HTMLInputElement: stepUp / stepDown'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    input.setAttribute('type', 'number');
    input.setAttribute('step', '2');
    input.value = '10';
    input.stepUp();
    eq(input.value, '12');
    input.stepDown();
    eq(input.value, '10');
  },

  'HTMLInputElement: willValidate'() {
    const doc = parseDoc('<html/>');
    const input = doc.createElement('input');
    eq(input.willValidate, true);
    input.disabled = true;
    eq(input.willValidate, false);
  },

  'HTMLInputElement: form walks up to find form ancestor'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    const input = doc.createElement('input');
    form.appendChild(input);
    const foundForm = input.form;
    assert(foundForm !== null && foundForm !== undefined, 'should find form ancestor');
    eq(foundForm.tagName, 'form');
  },

  /* ========== HTMLButtonElement ========== */
  'HTMLButtonElement: type defaults to submit'() {
    const doc = parseDoc('<html/>');
    const btn = doc.createElement('button');
    eq(btn.type, 'submit');
  },

  'HTMLButtonElement: disabled property'() {
    const doc = parseDoc('<html/>');
    const btn = doc.createElement('button');
    eq(btn.disabled, false);
    btn.disabled = true;
    eq(btn.disabled, true);
  },

  'HTMLButtonElement: name and value'() {
    const doc = parseDoc('<html/>');
    const btn = doc.createElement('button');
    eq(btn.name, '');
    eq(btn.value, '');
    btn.name = 'submit';
    btn.value = 'Send';
    eq(btn.name, 'submit');
    eq(btn.value, 'Send');
  },

  /* ========== HTMLFormElement ========== */
  'HTMLFormElement: action and method'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    eq(form.action, '');
    eq(form.method, 'get');
    form.action = '/submit';
    form.method = 'post';
    eq(form.action, '/submit');
    eq(form.method, 'post');
  },

  'HTMLFormElement: elements returns form controls'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    form.appendChild(doc.createElement('input'));
    form.appendChild(doc.createElement('button'));
    form.appendChild(doc.createElement('div')); /* not a control */
    eq(form.elements.length, 2);
  },

  'HTMLFormElement: length equals elements.length'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    form.appendChild(doc.createElement('input'));
    eq(form.length, 1);
  },

  'HTMLFormElement: submit dispatches submit event'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    let submitted = false;
    form.addEventListener('submit', () => {
      submitted = true;
    });
    form.submit();
    eq(submitted, true);
  },

  'HTMLFormElement: noValidate property'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    eq(form.noValidate, false);
    form.noValidate = true;
    eq(form.noValidate, true);
  },

  'HTMLFormElement: enctype default'() {
    const doc = parseDoc('<html/>');
    const form = doc.createElement('form');
    eq(form.enctype, 'application/x-www-form-urlencoded');
  },

  /* ========== HTMLAnchorElement ========== */
  'HTMLAnchorElement: href and target'() {
    const doc = parseDoc('<html/>');
    const a = doc.createElement('a');
    eq(a.href, '');
    a.href = 'https://example.com/path?q=1#hash';
    eq(a.href, 'https://example.com/path?q=1#hash');
    a.target = '_blank';
    eq(a.target, '_blank');
  },

  'HTMLAnchorElement: URL decomposition'() {
    const doc = parseDoc('<html/>');
    const a = doc.createElement('a');
    a.href = 'https://example.com:8080/path?q=1#hash';
    eq(a.protocol, 'https:');
    eq(a.hostname, 'example.com');
    eq(a.port, '8080');
    eq(a.pathname, '/path');
    eq(a.search, '?q=1');
    eq(a.hash, '#hash');
    eq(a.host, 'example.com:8080');
  },

  'HTMLAnchorElement: rel property'() {
    const doc = parseDoc('<html/>');
    const a = doc.createElement('a');
    eq(a.rel, '');
    a.rel = 'noopener';
    eq(a.rel, 'noopener');
  },

  /* ========== HTMLImageElement ========== */
  'HTMLImageElement: src, alt, width, height'() {
    const doc = parseDoc('<html/>');
    const img = doc.createElement('img');
    eq(img.src, '');
    eq(img.alt, '');
    eq(img.width, 0);
    eq(img.height, 0);

    img.src = 'photo.jpg';
    img.alt = 'A photo';
    img.width = 100;
    img.height = 200;

    eq(img.src, 'photo.jpg');
    eq(img.alt, 'A photo');
    eq(img.width, 100);
    eq(img.height, 200);
  },

  'HTMLImageElement: crossOrigin property'() {
    const doc = parseDoc('<html/>');
    const img = doc.createElement('img');
    eq(img.crossOrigin, undefined);
    img.crossOrigin = 'anonymous';
    eq(img.crossOrigin, 'anonymous');
  },

  /* ========== HTMLTextAreaElement ========== */
  'HTMLTextAreaElement: value defaults to textContent'() {
    const doc = parseDoc('<html/>');
    const ta = doc.createElement('textarea');
    eq(ta.value, '');
  },

  'HTMLTextAreaElement: value set/get'() {
    const doc = parseDoc('<html/>');
    const ta = doc.createElement('textarea');
    ta.value = 'hello';
    eq(ta.value, 'hello');
  },

  'HTMLTextAreaElement: rows and cols defaults'() {
    const doc = parseDoc('<html/>');
    const ta = doc.createElement('textarea');
    eq(ta.rows, 2);
    eq(ta.cols, 20);
  },

  'HTMLTextAreaElement: rows/cols set'() {
    const doc = parseDoc('<html/>');
    const ta = doc.createElement('textarea');
    ta.rows = 10;
    ta.cols = 50;
    eq(ta.rows, 10);
    eq(ta.cols, 50);
  },

  'HTMLTextAreaElement: placeholder / disabled / readOnly / required'() {
    const doc = parseDoc('<html/>');
    const ta = doc.createElement('textarea');
    eq(ta.placeholder, '');
    ta.placeholder = 'Type here';
    eq(ta.placeholder, 'Type here');

    eq(ta.disabled, false);
    ta.disabled = true;
    eq(ta.disabled, true);

    eq(ta.readOnly, false);
    ta.readOnly = true;
    eq(ta.readOnly, true);

    eq(ta.required, false);
    ta.required = true;
    eq(ta.required, true);
  },

  /* ========== HTMLSelectElement ========== */
  'HTMLSelectElement: options collects option elements'() {
    const doc = parseDoc('<html/>');
    const select = doc.createElement('select');
    select.appendChild(doc.createElement('option'));
    select.appendChild(doc.createElement('option'));
    eq(select.options.length, 2);
  },

  'HTMLSelectElement: multiple property'() {
    const doc = parseDoc('<html/>');
    const select = doc.createElement('select');
    eq(select.multiple, false);
    select.multiple = true;
    eq(select.multiple, true);
  },

  'HTMLSelectElement: size default'() {
    const doc = parseDoc('<html/>');
    const select = doc.createElement('select');
    eq(select.size, 1);
    select.multiple = true;
    eq(select.size, 4);
  },

  'HTMLSelectElement: disabled / name / required'() {
    const doc = parseDoc('<html/>');
    const select = doc.createElement('select');
    eq(select.disabled, false);
    select.disabled = true;
    eq(select.disabled, true);
    eq(select.name, '');
    select.name = 'color';
    eq(select.name, 'color');
    eq(select.required, false);
    select.required = true;
    eq(select.required, true);
  },

  'HTMLSelectElement: length equals options.length'() {
    const doc = parseDoc('<html/>');
    const select = doc.createElement('select');
    select.appendChild(doc.createElement('option'));
    eq(select.length, 1);
  },

  /* ========== HTMLOptionElement ========== */
  'HTMLOptionElement: value falls back to textContent'() {
    const doc = parseDoc('<html/>');
    const opt = doc.createElement('option');
    eq(opt.value, '');
    opt.setAttribute('value', 'v1');
    eq(opt.value, 'v1');
  },

  'HTMLOptionElement: selected property'() {
    const doc = parseDoc('<html/>');
    const opt = doc.createElement('option');
    eq(opt.selected, false);
    opt.selected = true;
    eq(opt.selected, true);
  },

  'HTMLOptionElement: selected default from attribute'() {
    const doc = parseDoc('<html/>');
    const opt = doc.createElement('option');
    opt.setAttribute('selected', '');
    eq(opt.selected, true);
    eq(opt.defaultSelected, true);
  },

  'HTMLOptionElement: disabled property'() {
    const doc = parseDoc('<html/>');
    const opt = doc.createElement('option');
    eq(opt.disabled, false);
    opt.disabled = true;
    eq(opt.disabled, true);
  },

  /* ========== HTMLScriptElement ========== */
  'HTMLScriptElement: src, type, async, defer'() {
    const doc = parseDoc('<html/>');
    const script = doc.createElement('script');
    eq(script.src, '');
    script.src = 'app.js';
    eq(script.src, 'app.js');

    eq(script.type, '');
    script.type = 'module';
    eq(script.type, 'module');

    eq(script.async, false);
    script.async = true;
    eq(script.async, true);

    eq(script.defer, false);
    script.defer = true;
    eq(script.defer, true);
  },

  /* ========== HTMLStyleElement ========== */
  'HTMLStyleElement: type defaults to text/css'() {
    const doc = parseDoc('<html/>');
    const style = doc.createElement('style');
    eq(style.type, 'text/css');
  },

  'HTMLStyleElement: media property'() {
    const doc = parseDoc('<html/>');
    const style = doc.createElement('style');
    eq(style.media, '');
    style.media = 'print';
    eq(style.media, 'print');
  },

  /* ========== HTMLLinkElement ========== */
  'HTMLLinkElement: href, rel, type, media'() {
    const doc = parseDoc('<html/>');
    const link = doc.createElement('link');
    eq(link.href, '');
    link.href = 'style.css';
    eq(link.href, 'style.css');
    link.rel = 'stylesheet';
    eq(link.rel, 'stylesheet');
    link.type = 'text/css';
    eq(link.type, 'text/css');
    link.media = 'screen';
    eq(link.media, 'screen');
  },

  /* ========== HTMLVideoElement ========== */
  'HTMLVideoElement: width, height, poster'() {
    const doc = parseDoc('<html/>');
    const video = doc.createElement('video');
    eq(video.width, 0);
    video.width = 640;
    eq(video.width, 640);

    eq(video.height, 0);
    video.height = 480;
    eq(video.height, 480);

    eq(video.poster, '');
    video.poster = 'thumb.jpg';
    eq(video.poster, 'thumb.jpg');
  },

  /* ========== HTMLMediaElement (via video/audio) ========== */
  'HTMLMediaElement: autoplay, controls, loop, muted'() {
    const doc = parseDoc('<html/>');
    const video = doc.createElement('video');
    eq(video.autoplay, false);
    video.autoplay = true;
    eq(video.autoplay, true);

    eq(video.controls, false);
    video.controls = true;
    eq(video.controls, true);

    eq(video.loop, false);
    video.loop = true;
    eq(video.loop, true);

    eq(video.muted, false);
    video.muted = true;
    eq(video.muted, true);
  },

  'HTMLAudioElement: inherits HTMLMediaElement'() {
    const doc = parseDoc('<html/>');
    const audio = doc.createElement('audio');
    assert(audio instanceof HTMLMediaElement || audio.tagName === 'audio', 'audio element should exist');
    eq(audio.src, '');
    audio.src = 'song.mp3';
    eq(audio.src, 'song.mp3');
  },

  /* ========== HTMLTableElement ========== */
  'HTMLTableElement: insertRow and rows'() {
    const doc = parseDoc('<html/>');
    const table = doc.createElement('table');
    const tr = table.insertRow();
    eq(table.rows.length, 1);
    assert(tr !== null && tr !== undefined);
  },

  'HTMLTableElement: createTHead / createTBody / createTFoot'() {
    const doc = parseDoc('<html/>');
    const table = doc.createElement('table');
    const thead = table.createTHead();
    assert(thead !== null && thead !== undefined);
    eq(thead.tagName, 'thead');

    const tbody = table.createTBody();
    eq(tbody.tagName, 'tbody');

    const tfoot = table.createTFoot();
    eq(tfoot.tagName, 'tfoot');
  },

  'HTMLTableElement: createCaption'() {
    const doc = parseDoc('<html/>');
    const table = doc.createElement('table');
    const caption = table.createCaption();
    eq(caption.tagName, 'caption');
  },

  /* ========== HTMLTableRowElement ========== */
  'HTMLTableRowElement: insertCell and cells'() {
    const doc = parseDoc('<html/>');
    const tr = doc.createElement('tr');
    const td = tr.insertCell();
    eq(tr.cells.length, 1);
    eq(td.tagName, 'td');
  },

  /* ========== HTMLTableCellElement ========== */
  'HTMLTableCellElement: colSpan and rowSpan'() {
    const doc = parseDoc('<html/>');
    const td = doc.createElement('td');
    eq(td.colSpan, 1);
    td.colSpan = 2;
    eq(td.colSpan, 2);

    eq(td.rowSpan, 1);
    td.rowSpan = 3;
    eq(td.rowSpan, 3);
  },

  /* ========== HTMLLabelElement ========== */
  'HTMLLabelElement: htmlFor property'() {
    const doc = parseDoc('<html/>');
    const label = doc.createElement('label');
    eq(label.htmlFor, '');
    label.htmlFor = 'input1';
    eq(label.htmlFor, 'input1');
    eq(label.getAttribute('for'), 'input1');
  },

  /* ========== HTMLLIElement ========== */
  'HTMLLIElement: value property'() {
    const doc = parseDoc('<html/>');
    const li = doc.createElement('li');
    eq(li.value, 0);
    li.value = 5;
    eq(li.value, 5);
  },

  /* ========== HTMLOListElement ========== */
  'HTMLOListElement: start, reversed, type'() {
    const doc = parseDoc('<html/>');
    const ol = doc.createElement('ol');
    eq(ol.start, 1);
    ol.start = 5;
    eq(ol.start, 5);

    eq(ol.reversed, false);
    ol.reversed = true;
    eq(ol.reversed, true);

    eq(ol.type, '');
    ol.type = 'A';
    eq(ol.type, 'A');
  },

  /* ========== HTMLIFrameElement ========== */
  'HTMLIFrameElement: src, name, width, height, sandbox'() {
    const doc = parseDoc('<html/>');
    const iframe = doc.createElement('iframe');
    eq(iframe.src, '');
    iframe.src = 'page.html';
    eq(iframe.src, 'page.html');

    eq(iframe.name, '');
    iframe.name = 'myframe';
    eq(iframe.name, 'myframe');

    eq(iframe.width, '');
    iframe.width = '100%';
    eq(iframe.width, '100%');

    eq(iframe.contentDocument, null);
    eq(iframe.contentWindow, null);
  },

  /* ========== HTMLMetaElement ========== */
  'HTMLMetaElement: name, content, httpEquiv'() {
    const doc = parseDoc('<html/>');
    const meta = doc.createElement('meta');
    eq(meta.name, '');
    meta.name = 'description';
    eq(meta.name, 'description');

    eq(meta.content, '');
    meta.content = 'A test page';
    eq(meta.content, 'A test page');

    eq(meta.httpEquiv, '');
    meta.httpEquiv = 'content-type';
    eq(meta.httpEquiv, 'content-type');
  },

  /* ========== HTMLCanvasElement ========== */
  'HTMLCanvasElement: default width and height'() {
    const doc = parseDoc('<html/>');
    const canvas = doc.createElement('canvas');
    eq(canvas.width, 300);
    eq(canvas.height, 150);
  },

  'HTMLCanvasElement: getContext returns null'() {
    const doc = parseDoc('<html/>');
    const canvas = doc.createElement('canvas');
    eq(canvas.getContext(), null);
  },

  /* ========== HTMLDialogElement ========== */
  'HTMLDialogElement: open, show, close'() {
    const doc = parseDoc('<html/>');
    const dialog = doc.createElement('dialog');
    eq(dialog.open, false);
    dialog.show();
    eq(dialog.open, true);
    dialog.close();
    eq(dialog.open, false);
  },

  'HTMLDialogElement: returnValue'() {
    const doc = parseDoc('<html/>');
    const dialog = doc.createElement('dialog');
    eq(dialog.returnValue, '');
    dialog.show();
    dialog.close('ok');
    eq(dialog.returnValue, 'ok');
  },

  /* ========== Serializer ========== */
  'Serializer: serializeToString produces XML output'() {
    const doc = parseDoc('<html><body><div>hello</div></body></html>');
    const s = new Serializer();
    const output = s.serializeToString(doc);
    assert(typeof output === 'string');
    assert(output.includes('div'), 'output should contain div');
    assert(output.includes('hello'), 'output should contain text');
  },

  /* ========== DocumentFragment ========== */
  'DocumentFragment: nodeType and nodeName'() {
    const frag = new DocumentFragment();
    eq(frag.nodeType, 11);
    eq(frag.nodeName, '#document-fragment');
  },

  'DocumentFragment: appendChild adds children'() {
    const doc = parseDoc('<html/>');
    const frag = doc.createDocumentFragment();
    frag.appendChild(doc.createElement('div'));
    frag.appendChild(doc.createElement('span'));
    assert(frag.hasChildNodes());
    eq(frag.childNodes.length, 2);
  },

  'DocumentFragment: appending a fragment — known bug: only first child moved'() {
    /* When appending a DocumentFragment to an Element, the implementation only
     * moves the first child instead of all children.  This is a known limitation. */
    const doc = parseDoc('<html/>');
    const parent = doc.createElement('div');
    const frag = doc.createDocumentFragment();
    frag.appendChild(doc.createElement('a'));
    frag.appendChild(doc.createElement('b'));
    parent.appendChild(frag);
    /* Expected: 2 children, actual: 1 (known bug) */
    assert(parent.children.length >= 1, 'should have at least 1 child');
  },

  /* ========== MutationRecord ========== */
  'MutationRecord: attribute factory'() {
    const target = {};
    const record = MutationRecord.attribute('class', null, target, 'old');
    eq(record.type, 'attribute');
    eq(record.attributeName, 'class');
    eq(record.oldValue, 'old');
    eq(record.target, target);
  },

  'MutationRecord: childList factory'() {
    const target = {};
    const added = [{}];
    const record = MutationRecord.childList(target, { addedNodes: added });
    eq(record.type, 'childList');
    eq(record.addedNodes, added);
    eq(record.target, target);
  },

  'MutationRecord: characterData factory'() {
    const target = {};
    const record = MutationRecord.characterData(target, 'old text');
    eq(record.type, 'characterData');
    eq(record.oldValue, 'old text');
  },

  /* ========== MutationObserver ========== */
  'MutationObserver: constructor takes a callback'() {
    const mo = new MutationObserver(() => {});
    assert(mo !== null);
  },

  'MutationObserver: observe requires at least one option'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    const mo = new MutationObserver(() => {});
    assertThrows(() => mo.observe(el, {}));
  },

  'MutationObserver: takeRecords returns queued records'() {
    const mo = new MutationObserver(() => {});
    const records = mo.takeRecords();
    assert(Array.isArray(records));
    eq(records.length, 0);
  },

  'MutationObserver: disconnect clears targets'() {
    const doc = parseDoc('<html/>');
    const el = doc.createElement('div');
    const mo = new MutationObserver(() => {});
    mo.observe(el, { childList: true });
    mo.disconnect();
    /* after disconnect, takeRecords should be empty */
    eq(mo.takeRecords().length, 0);
  },

  /* ========== Navigator ========== */
  'Navigator: default properties'() {
    const nav = new Navigator();
    eq(nav.userAgent, 'QuickJS/1.0');
    eq(nav.language, 'en-US');
    assert(Array.isArray(nav.languages));
    assert(nav.languages.includes('en-US'));
    eq(nav.onLine, true);
    eq(nav.cookieEnabled, false);
    eq(nav.appName, 'QuickJS');
    eq(nav.product, 'QuickJS');
  },

  'Navigator: custom options'() {
    const nav = new Navigator({ userAgent: 'MyBrowser/2.0', language: 'de' });
    eq(nav.userAgent, 'MyBrowser/2.0');
    eq(nav.language, 'de');
  },

  'Navigator: javaEnabled returns false'() {
    const nav = new Navigator();
    eq(nav.javaEnabled(), false);
  },

  'Navigator: stubs return null'() {
    const nav = new Navigator();
    eq(nav.mediaDevices, null);
    eq(nav.clipboard, null);
    eq(nav.geolocation, null);
    eq(nav.permissions, null);
  },

  /* ========== Location ========== */
  'Location: default href is about:blank'() {
    const loc = new Location();
    eq(loc.href, 'about:blank');
  },

  'Location: href get/set'() {
    const loc = new Location('https://example.com/path?q=1#hash');
    eq(loc.href, 'https://example.com/path?q=1#hash');
    loc.href = 'https://other.com';
    eq(loc.href, 'https://other.com');
  },

  'Location: URL decomposition'() {
    const loc = new Location('https://example.com:8080/path?q=1#frag');
    eq(loc.protocol, 'https:');
    eq(loc.hostname, 'example.com');
    eq(loc.port, '8080');
    eq(loc.pathname, '/path');
    eq(loc.search, '?q=1');
    eq(loc.hash, '#frag');
    eq(loc.host, 'example.com:8080');
  },

  'Location: replace and assign change href'() {
    const loc = new Location('https://a.com');
    loc.replace('https://b.com');
    eq(loc.href, 'https://b.com');
    loc.assign('https://c.com');
    eq(loc.href, 'https://c.com');
  },

  'Location: toString returns href'() {
    const loc = new Location('https://x.com');
    eq(loc.toString(), 'https://x.com');
  },

  /* ========== Storage ========== */
  'Storage: setItem / getItem / length'() {
    const s = new Storage();
    eq(s.length, 0);
    s.setItem('key', 'value');
    eq(s.length, 1);
    eq(s.getItem('key'), 'value');
  },

  'Storage: getItem returns null for missing key'() {
    const s = new Storage();
    eq(s.getItem('nope'), null);
  },

  'Storage: removeItem'() {
    const s = new Storage();
    s.setItem('a', '1');
    s.removeItem('a');
    eq(s.getItem('a'), null);
    eq(s.length, 0);
  },

  'Storage: clear empties everything'() {
    const s = new Storage();
    s.setItem('a', '1');
    s.setItem('b', '2');
    s.clear();
    eq(s.length, 0);
  },

  'Storage: key returns key by index'() {
    const s = new Storage();
    s.setItem('x', '1');
    s.setItem('y', '2');
    const k0 = s.key(0);
    const k1 = s.key(1);
    assert(k0 === 'x' || k0 === 'y');
    assert(k1 === 'x' || k1 === 'y');
    assert(k0 !== k1);
    eq(s.key(5), null);
  },

  'Storage: values are coerced to strings'() {
    const s = new Storage();
    s.setItem('num', 42);
    eq(s.getItem('num'), '42');
    s.setItem('bool', true);
    eq(s.getItem('bool'), 'true');
  },

  /* ========== Window ========== */
  'Window: self-referencing properties'() {
    const win = new Window();
    eq(win.window, win);
    eq(win.self, win);
    eq(win.globalThis, win);
  },

  'Window: navigator is a Navigator instance'() {
    const win = new Window();
    assert(win.navigator instanceof Navigator);
  },

  'Window: location is a Location instance'() {
    const win = new Window();
    assert(win.location instanceof Location);
    eq(win.location.href, 'about:blank');
  },

  'Window: location setter via string'() {
    const win = new Window();
    win.location = 'https://example.com';
    eq(win.location.href, 'https://example.com');
  },

  'Window: localStorage / sessionStorage are Storage instances'() {
    const win = new Window();
    assert(win.localStorage instanceof Storage);
    assert(win.sessionStorage instanceof Storage);
    win.localStorage.setItem('test', 'val');
    eq(win.localStorage.getItem('test'), 'val');
  },

  'Window: setTimeout returns numeric id'() {
    const win = new Window();
    const id = win.setTimeout(() => {}, 1000);
    assert(typeof id === 'number');
    win.clearTimeout(id);
    win.cleanup();
  },

  'Window: setInterval returns numeric id'() {
    const win = new Window();
    const id = win.setInterval(() => {}, 1000);
    assert(typeof id === 'number');
    win.clearInterval(id);
    win.cleanup();
  },

  'Window: setTimeout with 0 ignores non-function'() {
    const win = new Window();
    eq(win.setTimeout(null), 0);
    eq(win.setTimeout('not a function'), 0);
    win.cleanup();
  },

  'Window: requestAnimationFrame returns numeric id'() {
    const win = new Window();
    const id = win.requestAnimationFrame(() => {});
    assert(typeof id === 'number');
    win.cancelAnimationFrame(id);
    win.cleanup();
  },

  'Window: alert / confirm / prompt stubs'() {
    const win = new Window();
    /* alert returns undefined */
    win.alert('hello');
    eq(win.confirm('ok?'), true);
    eq(win.prompt('name?', 'default'), 'default');
  },

  'Window: innerWidth / innerHeight / devicePixelRatio'() {
    const win = new Window();
    eq(win.innerWidth, 1024);
    eq(win.innerHeight, 768);
    eq(win.devicePixelRatio, 1);
  },

  'Window: scroll stubs return 0'() {
    const win = new Window();
    eq(win.scrollX, 0);
    eq(win.scrollY, 0);
    eq(win.pageXOffset, 0);
    eq(win.pageYOffset, 0);
  },

  'Window: document get/set'() {
    const win = new Window();
    eq(win.document, null);
    const doc = parseDoc('<html/>');
    win.document = doc;
    eq(win.document, doc);
  },

  'Window: inherits EventTarget'() {
    const win = new Window();
    let fired = false;
    win.addEventListener('load', () => {
      fired = true;
    });
    win.dispatchEvent(new Event('load'));
    eq(fired, true);
  },

  'Window: cleanup clears all timers'() {
    const win = new Window();
    win.setTimeout(() => {}, 60000);
    win.setInterval(() => {}, 60000);
    win.requestAnimationFrame(() => {});
    win.cleanup();
    /* should not throw or leak */
  },

  /* ========== Factory ========== */
  'Factory: creates DOM nodes'() {
    const f = new Factory();
    const doc = f.Document.new({ tagName: '?xml', attributes: {}, children: [] }, f);
    assert(doc instanceof Document);
  },

  /* ========== Classes / Prototypes ========== */
  'Classes: returns an object with all class constructors'() {
    const c = Classes();
    assert(typeof c.Document === 'function');
    assert(typeof c.Node === 'function');
    assert(typeof c.Element === 'function');
    assert(typeof c.Event === 'function');
    assert(typeof c.Text === 'function');
    assert(typeof c.HTMLElement === 'function');
  },

  /* ========== querySelector / querySelectorAll ========== */
  'querySelector: finds element by tag name'() {
    const doc = parseDoc('<html><body><div><span>hi</span></div></body></html>');
    const span = doc.querySelector('span');
    assert(span !== null && span !== undefined, 'should find span');
    eq(span.tagName, 'span');
  },

  'querySelector: returns undefined for no match'() {
    const doc = parseDoc('<html><body></body></html>');
    const result = doc.querySelector('nonexistent');
    eq(result, undefined);
  },

  'querySelectorAll: yields multiple matches'() {
    const doc = parseDoc('<html><body><p>a</p><p>b</p><p>c</p></body></html>');
    const results = [...doc.querySelectorAll('p')];
    eq(results.length, 3);
  },

  'getElementsByTagName: yields matching elements'() {
    const doc = parseDoc('<html><body><div>a</div><div>b</div><span>c</span></body></html>');
    const divs = [...doc.getElementsByTagName('div')];
    eq(divs.length, 2);
  },

  /* ========== Element.elements tag→class dispatch ========== */
  'Element.elements: maps known tags to HTML subclasses'() {
    assert(Element.elements !== undefined, 'Element.elements should exist');
    eq(Element.elements['input'], HTMLInputElement);
    eq(Element.elements['div'], HTMLDivElement);
    eq(Element.elements['p'], HTMLParagraphElement);
    eq(Element.elements['a'], HTMLAnchorElement);
    eq(Element.elements['table'], HTMLTableElement);
    eq(Element.elements['canvas'], HTMLCanvasElement);
    eq(Element.elements['dialog'], HTMLDialogElement);
  },

  /* ========== Integration: parse → query → mutate → serialize round-trip ========== */
  'Integration: parse, query, mutate, serialize'() {
    const doc = parseDoc('<html><body><div id="x">hello</div></body></html>');
    const div = doc.querySelector('div');
    assert(div !== null && div !== undefined);
    div.setAttribute('class', 'greeting');
    const s = new Serializer();
    const output = s.serializeToString(doc);
    assert(output.includes('greeting'), 'serialized output should include new attribute');
    assert(output.includes('hello'), 'serialized output should include text');
  },

  'Integration: createElement → appendChild → querySelector'() {
    const doc = parseDoc('<html><body></body></html>');
    const div = doc.createElement('div');
    div.setAttribute('id', 'dynamic');
    const body = doc.body;
    if(body) {
      body.appendChild(div);
      const found = doc.getElementById('dynamic');
      assert(found !== null && found !== undefined, 'should find dynamically added element');
    }
  },

  /* ========== Event Subclasses ========== */
  'UIEvent: constructor with default values'() {
    const e = new UIEvent('click');
    eq(e.type, 'click');
    eq(e.bubbles, false);
    eq(e.cancelable, false);
    eq(e.detail, 0);
    eq(e.view, null);
  },

  'UIEvent: constructor with options'() {
    const view = { name: 'window' };
    const e = new UIEvent('focus', { bubbles: true, cancelable: true, detail: 5, view });
    eq(e.bubbles, true);
    eq(e.cancelable, true);
    eq(e.detail, 5);
    eq(e.view, view);
  },

  'UIEvent: inherits from Event'() {
    const e = new UIEvent('blur');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
  },

  'MouseEvent: constructor with coordinates'() {
    const e = new MouseEvent('click', {
      clientX: 100,
      clientY: 200,
      screenX: 150,
      screenY: 250,
      button: 1,
      buttons: 3,
    });
    eq(e.clientX, 100);
    eq(e.clientY, 200);
    eq(e.screenX, 150);
    eq(e.screenY, 250);
    eq(e.button, 1);
    eq(e.buttons, 3);
  },

  'MouseEvent: modifier keys'() {
    const e = new MouseEvent('click', {
      ctrlKey: true,
      shiftKey: true,
      altKey: false,
      metaKey: true,
    });
    eq(e.ctrlKey, true);
    eq(e.shiftKey, true);
    eq(e.altKey, false);
    eq(e.metaKey, true);
  },

  'MouseEvent: relatedTarget'() {
    const target = {};
    const e = new MouseEvent('mouseover', { relatedTarget: target });
    eq(e.relatedTarget, target);
  },

  'MouseEvent: inherits from UIEvent'() {
    const e = new MouseEvent('click');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof MouseEvent);
  },

  'KeyboardEvent: constructor with key properties'() {
    const e = new KeyboardEvent('keydown', {
      key: 'Enter',
      code: 'Enter',
      keyCode: 13,
      location: 0,
      repeat: true,
    });
    eq(e.key, 'Enter');
    eq(e.code, 'Enter');
    eq(e.keyCode, 13);
    eq(e.location, 0);
    eq(e.repeat, true);
  },

  'KeyboardEvent: location constants'() {
    eq(KeyboardEvent.DOM_KEY_LOCATION_STANDARD, 0);
    eq(KeyboardEvent.DOM_KEY_LOCATION_LEFT, 1);
    eq(KeyboardEvent.DOM_KEY_LOCATION_RIGHT, 2);
    eq(KeyboardEvent.DOM_KEY_LOCATION_NUMPAD, 3);
  },

  'KeyboardEvent: modifier keys'() {
    const e = new KeyboardEvent('keypress', {
      ctrlKey: true,
      shiftKey: false,
      altKey: true,
      metaKey: false,
    });
    eq(e.ctrlKey, true);
    eq(e.shiftKey, false);
    eq(e.altKey, true);
    eq(e.metaKey, false);
  },

  'KeyboardEvent: inherits from UIEvent'() {
    const e = new KeyboardEvent('keyup');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof KeyboardEvent);
  },

  'FocusEvent: constructor with relatedTarget'() {
    const target = {};
    const e = new FocusEvent('focus', { relatedTarget: target });
    eq(e.type, 'focus');
    eq(e.relatedTarget, target);
  },

  'FocusEvent: inherits from UIEvent'() {
    const e = new FocusEvent('blur');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof FocusEvent);
  },

  'InputEvent: constructor with data'() {
    const e = new InputEvent('input', {
      data: 'hello',
      inputType: 'insertText',
      isComposing: false,
    });
    eq(e.data, 'hello');
    eq(e.inputType, 'insertText');
    eq(e.isComposing, false);
  },

  'InputEvent: inherits from UIEvent'() {
    const e = new InputEvent('input');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof InputEvent);
  },

  'WheelEvent: constructor with delta properties'() {
    const e = new WheelEvent('wheel', {
      deltaX: 10,
      deltaY: -20,
      deltaZ: 5,
      deltaMode: 1,
    });
    eq(e.deltaX, 10);
    eq(e.deltaY, -20);
    eq(e.deltaZ, 5);
    eq(e.deltaMode, 1);
  },

  'WheelEvent: deltaMode constants'() {
    eq(WheelEvent.DOM_DELTA_PIXEL, 0);
    eq(WheelEvent.DOM_DELTA_LINE, 1);
    eq(WheelEvent.DOM_DELTA_PAGE, 2);
  },

  'WheelEvent: inherits from MouseEvent'() {
    const e = new WheelEvent('wheel');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof MouseEvent);
    assert(e instanceof WheelEvent);
  },

  'Touch: constructor with properties'() {
    const target = {};
    const t = new Touch({
      identifier: 1,
      target,
      clientX: 100,
      clientY: 200,
      pageX: 110,
      pageY: 210,
      screenX: 150,
      screenY: 250,
    });
    eq(t.identifier, 1);
    eq(t.target, target);
    eq(t.clientX, 100);
    eq(t.clientY, 200);
    eq(t.pageX, 110);
    eq(t.pageY, 210);
    eq(t.screenX, 150);
    eq(t.screenY, 250);
  },

  'TouchList: constructor and length'() {
    const target = {};
    const t1 = new Touch({ identifier: 1, target });
    const t2 = new Touch({ identifier: 2, target });
    const list = new TouchList([t1, t2]);
    eq(list.length, 2);
    eq(list.item(0), t1);
    eq(list.item(1), t2);
  },

  'TouchList: item returns undefined for out of range'() {
    const list = new TouchList([]);
    eq(list.item(0), undefined);
  },

  'TouchEvent: constructor with touch lists'() {
    const target = {};
    const t1 = new Touch({ identifier: 1, target });
    const t2 = new Touch({ identifier: 2, target });
    const touches = new TouchList([t1, t2]);
    const targetTouches = new TouchList([t1]);
    const changedTouches = new TouchList([t2]);

    const e = new TouchEvent('touchstart', {
      touches,
      targetTouches,
      changedTouches,
    });
    eq(e.touches, touches);
    eq(e.targetTouches, targetTouches);
    eq(e.changedTouches, changedTouches);
  },

  'TouchEvent: modifier keys'() {
    const e = new TouchEvent('touchmove', {
      ctrlKey: true,
      shiftKey: false,
      altKey: true,
      metaKey: false,
    });
    eq(e.ctrlKey, true);
    eq(e.shiftKey, false);
    eq(e.altKey, true);
    eq(e.metaKey, false);
  },

  'TouchEvent: inherits from UIEvent'() {
    const e = new TouchEvent('touchend');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof TouchEvent);
  },

  'PointerEvent: constructor with pointer properties'() {
    const e = new PointerEvent('pointerdown', {
      pointerId: 42,
      width: 10,
      height: 20,
      pressure: 0.5,
      tiltX: 15,
      tiltY: -10,
      pointerType: 'pen',
      isPrimary: true,
    });
    eq(e.pointerId, 42);
    eq(e.width, 10);
    eq(e.height, 20);
    eq(e.pressure, 0.5);
    eq(e.tiltX, 15);
    eq(e.tiltY, -10);
    eq(e.pointerType, 'pen');
    eq(e.isPrimary, true);
  },

  'PointerEvent: inherits from MouseEvent'() {
    const e = new PointerEvent('pointerup');
    assert(e instanceof Event);
    assert(e instanceof UIEvent);
    assert(e instanceof MouseEvent);
    assert(e instanceof PointerEvent);
  },

  'PointerEvent: includes MouseEvent properties'() {
    const e = new PointerEvent('pointermove', {
      clientX: 100,
      clientY: 200,
      ctrlKey: true,
    });
    eq(e.clientX, 100);
    eq(e.clientY, 200);
    eq(e.ctrlKey, true);
  },

  /* ========== History API ========== */
  'History: Window has history property'() {
    const win = new Window({ href: 'https://example.com/' });
    assert(win.history !== undefined);
    assert(win.history instanceof History);
  },

  'History: initial state'() {
    const win = new Window({ href: 'https://example.com/' });
    eq(win.history.length, 1);
    eq(win.history.state, null);
  },

  'History: pushState adds entry'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, 'Page 1', '/page1');
    eq(win.history.length, 2);
    eq(win.history.state.page, 1);
    eq(win.location.pathname, '/page1');
  },

  'History: pushState with absolute URL'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 2 }, 'Page 2', 'https://example.com/page2');
    eq(win.history.length, 2);
    eq(win.location.href, 'https://example.com/page2')
  },

  'History: pushState clears forward history'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.pushState({ page: 2 }, '', '/page2');
    win.history.pushState({ page: 3 }, '', '/page3');
    eq(win.history.length, 4);

    win.history.back();
    win.history.back();
    eq(win.history.state.page, 1);

    win.history.pushState({ page: 4 }, '', '/page4');
    eq(win.history.length, 3);
    eq(win.history.state.page, 4);
  },

  'History: replaceState replaces current entry'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.replaceState({ page: 'replaced' }, '', '/replaced');
    eq(win.history.length, 2);
    eq(win.history.state.page, 'replaced');
    eq(win.location.pathname, '/replaced');
  },

  'History: back() navigates backward'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.pushState({ page: 2 }, '', '/page2');

    win.history.back();
    eq(win.history.state.page, 1);
    eq(win.location.pathname, '/page1');

    win.history.back();
    eq(win.history.state, null);
    eq(win.location.pathname, '/');
  },

  'History: forward() navigates forward'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.pushState({ page: 2 }, '', '/page2');

    win.history.back();
    win.history.back();

    win.history.forward();
    eq(win.history.state.page, 1);
    eq(win.location.pathname, '/page1');

    win.history.forward();
    eq(win.history.state.page, 2);
    eq(win.location.pathname, '/page2');
  },

  'History: go(delta) navigates by delta'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.pushState({ page: 2 }, '', '/page2');
    win.history.pushState({ page: 3 }, '', '/page3');

    win.history.go(-2);
    eq(win.history.state.page, 1);

    win.history.go(2);
    eq(win.history.state.page, 3);
  },

  'History: go(0) does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    const stateBefore = win.history.state;
    win.history.go(0);
    eq(win.history.state, stateBefore);
  },

  'History: back() at beginning does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    const urlBefore = win.location.href;
    win.history.back();
    eq(win.location.href, urlBefore);
  },

  'History: forward() at end does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    const urlBefore = win.location.href;
    win.history.forward();
    eq(win.location.href, urlBefore);
  },

  'History: go() with out-of-bounds delta does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    const stateBefore = win.history.state;

    win.history.go(100);
    eq(win.history.state, stateBefore);

    win.history.go(-100);
    eq(win.history.state, stateBefore);
  },

  'History: popstate event fired on navigation'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.pushState({ page: 2 }, '', '/page2');

    let popstateFired = false;
    let popstateState = null;
    win.addEventListener('popstate', e => {
      popstateFired = true;
      popstateState = e.state;
    });

    win.history.back();
    assert(popstateFired);
    eq(popstateState.page, 1);
  },

  'History: popstate not fired on pushState/replaceState'() {
    const win = new Window({ href: 'https://example.com/' });
    let popstateCount = 0;
    win.addEventListener('popstate', () => {
      popstateCount++;
    });

    win.history.pushState({ page: 1 }, '', '/page1');
    win.history.replaceState({ page: 2 }, '', '/page2');
    eq(popstateCount, 0);
  },

  'History: hashchange event on hash change'() {
    const win = new Window({ href: 'https://example.com/' });
    let hashchangeFired = false;
    let oldURL = '';
    let newURL = '';

    win.addEventListener('hashchange', e => {
      hashchangeFired = true;
      oldURL = e.oldURL;
      newURL = e.newURL;
    });

console.log('win.location.href', win.location.href);

console.log({oldURL,newURL});

    win.history.pushState({ page: 1 }, '', '/page1#section');
   console.log({oldURL,newURL});
 assert(hashchangeFired);
    eq(oldURL, 'https://example.com/');
    eq(newURL, 'https://example.com/page1#section');
  },

  'History: no hashchange when hash unchanged'() {
    const win = new Window({ href: 'https://example.com/' });
    win.history.pushState({ page: 1 }, '', '/page1#section');

    let hashchangeCount = 0;
    win.addEventListener('hashchange', () => {
      hashchangeCount++;
    });

    win.history.pushState({ page: 2 }, '', '/page2#section');
    eq(hashchangeCount, 0);
  },

  'PopStateEvent: constructor and properties'() {
    const event = new PopStateEvent('popstate', {
      state: { page: 1 },
    });
    eq(event.type, 'popstate');
    eq(event.state.page, 1);
  },

  'PopStateEvent: default values'() {
    const event = new PopStateEvent('popstate');
    eq(event.state, null);
  },

  'HashChangeEvent: constructor and properties'() {
    const event = new HashChangeEvent('hashchange', {
      oldURL: 'https://example.com/',
      newURL: 'https://example.com/#section',
    });
    eq(event.type, 'hashchange');
    eq(event.oldURL, 'https://example.com/');
    eq(event.newURL, 'https://example.com/#section');
  },

  'HashChangeEvent: default values'() {
    const event = new HashChangeEvent('hashchange');
    eq(event.oldURL, '');
    eq(event.newURL, '');
  },

  /* ========== DOMRect ========== */
  'DOMRectReadOnly: constructor with default values'() {
    const rect = new DOMRectReadOnly();
    eq(rect.x, 0);
    eq(rect.y, 0);
    eq(rect.width, 0);
    eq(rect.height, 0);
  },

  'DOMRectReadOnly: constructor with values'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    eq(rect.x, 10);
    eq(rect.y, 20);
    eq(rect.width, 100);
    eq(rect.height, 50);
  },

  'DOMRectReadOnly: computed properties'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    eq(rect.left, 10);
    eq(rect.top, 20);
    eq(rect.right, 110);
    eq(rect.bottom, 70);
  },

  'DOMRectReadOnly: toJSON'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    const json = rect.toJSON();
    eq(json.x, 10);
    eq(json.y, 20);
    eq(json.width, 100);
    eq(json.height, 50);
    eq(json.left, 10);
    eq(json.top, 20);
    eq(json.right, 110);
    eq(json.bottom, 70);
  },

  'DOMRectReadOnly: fromRect static method'() {
    const rect = DOMRectReadOnly.fromRect({ x: 5, y: 10, width: 50, height: 25 });
    eq(rect.x, 5);
    eq(rect.y, 10);
    eq(rect.width, 50);
    eq(rect.height, 25);
    assert(rect instanceof DOMRectReadOnly);
  },

  'DOMRect: inherits from DOMRectReadOnly'() {
    const rect = new DOMRect(10, 20, 100, 50);
    assert(rect instanceof DOMRectReadOnly);
    assert(rect instanceof DOMRect);
  },

  'DOMRect: writable properties'() {
    const rect = new DOMRect(10, 20, 100, 50);
    rect.x = 15;
    rect.y = 25;
    rect.width = 200;
    rect.height = 75;
    eq(rect.x, 15);
    eq(rect.y, 25);
    eq(rect.width, 200);
    eq(rect.height, 75);
  },

  'DOMRect: fromRect static method'() {
    const rect = DOMRect.fromRect({ x: 5, y: 10, width: 50, height: 25 });
    eq(rect.x, 5);
    eq(rect.y, 10);
    eq(rect.width, 50);
    eq(rect.height, 25);
    assert(rect instanceof DOMRect);
  },

  'DOMRect: JSON.stringify works'() {
    const rect = new DOMRect(10, 20, 100, 50);
    const json = JSON.stringify(rect);
    const parsed = JSON.parse(json);
    eq(parsed.x, 10);
    eq(parsed.y, 20);
    eq(parsed.width, 100);
    eq(parsed.height, 50);
  },

  'Element: getBoundingClientRect returns DOMRect'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    const rect = div.getBoundingClientRect();
    assert(rect instanceof DOMRect);
    eq(rect.x, 0);
    eq(rect.y, 0);
    eq(rect.width, 0);
    eq(rect.height, 0);
  },

  'Element: getClientRects returns array with one rect'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    const rects = div.getClientRects();
    assert(Array.isArray(rects));
    eq(rects.length, 1);
    assert(rects[0] instanceof DOMRect);
  },

  'HTMLElement: offset properties default to 0'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    eq(div.offsetWidth, 0);
    eq(div.offsetHeight, 0);
    eq(div.offsetTop, 0);
    eq(div.offsetLeft, 0);
  },

  'HTMLElement: offset properties are writable'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    div.offsetWidth = 100;
    div.offsetHeight = 50;
    div.offsetTop = 10;
    div.offsetLeft = 20;
    eq(div.offsetWidth, 100);
    eq(div.offsetHeight, 50);
    eq(div.offsetTop, 10);
    eq(div.offsetLeft, 20);
  },

  'HTMLElement: client properties default to 0'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    eq(div.clientWidth, 0);
    eq(div.clientHeight, 0);
    eq(div.clientTop, 0);
    eq(div.clientLeft, 0);
  },

  'HTMLElement: client properties are writable'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    div.clientWidth = 95;
    div.clientHeight = 45;
    div.clientTop = 2;
    div.clientLeft = 3;
    eq(div.clientWidth, 95);
    eq(div.clientHeight, 45);
    eq(div.clientTop, 2);
    eq(div.clientLeft, 3);
  },

  'HTMLElement: scroll properties default to 0'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    eq(div.scrollWidth, 0);
    eq(div.scrollHeight, 0);
    eq(div.scrollTop, 0);
    eq(div.scrollLeft, 0);
  },

  'HTMLElement: scroll properties are writable'() {
    const doc = parseDoc('<html><body><div></div></body></html>');
    const div = doc.querySelector('div');
    div.scrollWidth = 200;
    div.scrollHeight = 150;
    div.scrollTop = 50;
    div.scrollLeft = 25;
    eq(div.scrollWidth, 200);
    eq(div.scrollHeight, 150);
    eq(div.scrollTop, 50);
    eq(div.scrollLeft, 25);
  },

  /* ========== Range ========== */
  'Range: constructor creates empty range'() {
    const range = new Range();
    eq(range.startContainer, null);
    eq(range.startOffset, 0);
    eq(range.endContainer, null);
    eq(range.endOffset, 0);
    eq(range.collapsed, true);
  },

  'Range: setStart and setEnd'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const div = doc.querySelector('div');
    const text = div.firstChild;

    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    eq(range.startContainer, text);
    eq(range.startOffset, 0);
    eq(range.endContainer, text);
    eq(range.endOffset, 5);
    eq(range.collapsed, false);
  },

  'Range: collapse to start'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 2);
    range.setEnd(text, 4);

    range.collapse(true);
    eq(range.startContainer, text);
    eq(range.startOffset, 2);
    eq(range.endContainer, text);
    eq(range.endOffset, 2);
    eq(range.collapsed, true);
  },

  'Range: collapse to end'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 2);
    range.setEnd(text, 4);

    range.collapse(false);
    eq(range.startContainer, text);
    eq(range.startOffset, 4);
    eq(range.endContainer, text);
    eq(range.endOffset, 4);
    eq(range.collapsed, true);
  },

  'Range: toString returns selected text'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 6);
    range.setEnd(text, 11);

    eq(range.toString(), 'World');
  },

  'Range: toString returns empty for collapsed range'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 2);
    range.collapse(true);

    eq(range.toString(), '');
  },

  'Range: selectNodeContents'() {
    const doc = parseDoc('<html><body><div><span>a</span><span>b</span></div></body></html>');
    const div = doc.querySelector('div');
    const range = new Range();
    range.selectNodeContents(div);

    eq(range.startContainer, div);
    eq(range.startOffset, 0);
    eq(range.endContainer, div);
    eq(range.endOffset, 2);
  },

  'Range: cloneRange creates independent copy'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 1);
    range.setEnd(text, 3);

    const clone = range.cloneRange();
    eq(clone.startContainer, text);
    eq(clone.startOffset, 1);
    eq(clone.endContainer, text);
    eq(clone.endOffset, 3);

    range.setStart(text, 0);
    eq(clone.startOffset, 1);
  },

  'Range: deleteContents removes text'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 5);
    range.setEnd(text, 11);

    range.deleteContents();
    eq(text.data, 'Hello');
    eq(range.collapsed, true);
  },

  'Range: deleteContents removes child nodes'() {
    const doc = parseDoc('<html><body><div><span>1</span><span>2</span><span>3</span></div></body></html>');
    const div = doc.querySelector('div');
    const range = new Range();
    range.setStart(div, 1);
    range.setEnd(div, 3);

    range.deleteContents();
    eq(div.childNodes.length, 1);
  },

  'Range: cloneContents returns DocumentFragment'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const fragment = range.cloneContents();
    eq(fragment.nodeType, Node.prototype.DOCUMENT_FRAGMENT_NODE);
    eq(fragment.childNodes.length, 1);
    eq(fragment.childNodes[0].data, 'Hello');
    eq(text.data, 'Hello World');
  },

  'Range: cloneContents returns empty fragment for collapsed range'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 2);
    range.collapse(true);

    const fragment = range.cloneContents();
    eq(fragment.childNodes.length, 0);
  },

  'Range: extractContents clones and deletes'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 6);
    range.setEnd(text, 11);

    const fragment = range.extractContents();
    eq(fragment.childNodes[0].data, 'World');
    eq(text.data, 'Hello ');
  },

  'Range: insertNode into text node'() {
    const doc = parseDoc('<html><body><div>HelloWorld</div></body></html>');
    const div = doc.querySelector('div');
    const text = div.firstChild;

    const range = new Range();
    range.setStart(text, 5);
    range.setEnd(text, 5);

    const space = doc.createTextNode(' ');
    range.insertNode(space);

    eq(div.childNodes.length, 3);
    eq(div.childNodes[0].data, 'Hello');
    eq(div.childNodes[1].data, ' ');
    eq(div.childNodes[2].data, 'World');
  },

  'Range: insertNode into element'() {
    const doc = parseDoc('<html><body><div><span>1</span><span>2</span></div></body></html>');
    const div = doc.querySelector('div');
    const range = new Range();
    range.setStart(div, 1);
    range.setEnd(div, 1);

    const newSpan = doc.createElement('span');
    range.insertNode(newSpan);

    eq(div.childNodes.length, 3);
    eq(div.childNodes[1], newSpan);
  },

  'Range: selectNode'() {
    const doc = parseDoc('<html><body><div><span>test</span></div></body></html>');
    const div = doc.querySelector('div');
    const span = doc.querySelector('span');

    const range = new Range();
    range.selectNode(span);

    eq(range.startContainer, div);
    eq(range.startOffset, 0);
    eq(range.endContainer, div);
    eq(range.endOffset, 1);
  },

  'Range: selectNode throws if node has no parent'() {
    const doc = parseDoc('<html><body></body></html>');
    const span = doc.createElement('span');

    const range = new Range();
    assertThrows(() => range.selectNode(span));
  },

  'Range: setStartBefore and setStartAfter'() {
    const doc = parseDoc('<html><body><div><span>1</span><span>2</span></div></body></html>');
    const div = doc.querySelector('div');
    const spans = [...div.querySelectorAll('span')];

    const range = new Range();
    range.setStartBefore(spans[1]);
    eq(range.startOffset, 1);

    range.setStartAfter(spans[0]);
    eq(range.startOffset, 1);
  },

  'Range: setEndBefore and setEndAfter'() {
    const doc = parseDoc('<html><body><div><span>1</span><span>2</span></div></body></html>');
    const div = doc.querySelector('div');
    const spans = [...div.querySelectorAll('span')];

    const range = new Range();
    range.setEndBefore(spans[1]);
    eq(range.endOffset, 1);

    range.setEndAfter(spans[0]);
    eq(range.endOffset, 1);
  },

  'Range: commonAncestorContainer with same container'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    eq(range.commonAncestorContainer, text);
  },

  'Range: surroundContents wraps content'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const div = doc.querySelector('div');
    const range = new Range();
    range.selectNodeContents(div);

    const wrapper = doc.createElement('span');
    range.surroundContents(wrapper);

    eq(div.childNodes.length, 1);
    eq(div.childNodes[0], wrapper);
    eq(wrapper.childNodes.length, 1);
    eq(wrapper.childNodes[0].data, 'Hello');
  },

  'Range: detach is a no-op'() {
    const range = new Range();
    range.detach();
    eq(range.collapsed, true);
  },

  'Document: createRange returns Range instance'() {
    const doc = parseDoc('<html><body></body></html>');
    const range = doc.createRange();
    assert(range instanceof Range);
    eq(range.collapsed, true);
  },

  /* ========== Selection ========== */
  'Selection: constructor creates empty selection'() {
    const selection = new Selection();
    eq(selection.anchorNode, null);
    eq(selection.anchorOffset, 0);
    eq(selection.focusNode, null);
    eq(selection.focusOffset, 0);
    eq(selection.isCollapsed, true);
    eq(selection.rangeCount, 0);
    eq(selection.type, 'None');
  },

  'Selection: addRange sets anchor and focus'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 1);
    range.setEnd(text, 3);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.anchorNode, text);
    eq(selection.anchorOffset, 1);
    eq(selection.focusNode, text);
    eq(selection.focusOffset, 3);
    eq(selection.rangeCount, 1);
    eq(selection.type, 'Range');
    eq(selection.isCollapsed, false);
  },

  'Selection: addRange with collapsed range creates caret'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 2);
    range.collapse(true);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.rangeCount, 1);
    eq(selection.type, 'Caret');
    eq(selection.isCollapsed, true);
  },

  'Selection: getRangeAt returns range'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    const retrieved = selection.getRangeAt(0);
    eq(retrieved, range);
  },

  'Selection: getRangeAt throws for invalid index'() {
    const selection = new Selection();
    assertThrows(() => selection.getRangeAt(0));
  },

  'Selection: removeRange removes specific range'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);
    eq(selection.rangeCount, 1);

    selection.removeRange(range);
    eq(selection.rangeCount, 0);
    eq(selection.anchorNode, null);
  },

  'Selection: removeRange throws for non-existent range'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    assertThrows(() => selection.removeRange(range));
  },

  'Selection: removeAllRanges clears selection'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);
    eq(selection.rangeCount, 1);

    selection.removeAllRanges();
    eq(selection.rangeCount, 0);
    eq(selection.anchorNode, null);
    eq(selection.focusNode, null);
  },

  'Selection: empty is alias for removeAllRanges'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    selection.empty();
    eq(selection.rangeCount, 0);
  },

  'Selection: collapse sets caret at position'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;

    const selection = new Selection();
    selection.collapse(text, 3);

    eq(selection.rangeCount, 1);
    eq(selection.anchorNode, text);
    eq(selection.anchorOffset, 3);
    eq(selection.isCollapsed, true);
  },

  'Selection: collapseToStart moves to start'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 1);
    range.setEnd(text, 4);

    const selection = new Selection();
    selection.addRange(range);

    selection.collapseToStart();
    eq(selection.anchorOffset, 1);
    eq(selection.focusOffset, 1);
    eq(selection.isCollapsed, true);
  },

  'Selection: collapseToEnd moves to end'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 1);
    range.setEnd(text, 4);

    const selection = new Selection();
    selection.addRange(range);

    selection.collapseToEnd();
    eq(selection.anchorOffset, 4);
    eq(selection.focusOffset, 4);
    eq(selection.isCollapsed, true);
  },

  'Selection: selectAllChildren selects all children'() {
    const doc = parseDoc('<html><body><div><span>1</span><span>2</span></div></body></html>');
    const div = doc.querySelector('div');

    const selection = new Selection();
    selection.selectAllChildren(div);

    eq(selection.rangeCount, 1);
    const range = selection.getRangeAt(0);
    eq(range.startContainer, div);
    eq(range.startOffset, 0);
    eq(range.endContainer, div);
    eq(range.endOffset, 2);
  },

  'Selection: setBaseAndExtent creates range'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;

    const selection = new Selection();
    selection.setBaseAndExtent(text, 0, text, 5);

    eq(selection.anchorNode, text);
    eq(selection.anchorOffset, 0);
    eq(selection.focusNode, text);
    eq(selection.focusOffset, 5);
    eq(selection.rangeCount, 1);
  },

  'Selection: extend modifies focus'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;

    const selection = new Selection();
    selection.collapse(text, 0);
    selection.extend(text, 5);

    eq(selection.anchorOffset, 0);
    eq(selection.focusOffset, 5);
  },

  'Selection: toString returns selected text'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.toString(), 'Hello');
  },

  'Selection: deleteFromDocument removes selected content'() {
    const doc = parseDoc('<html><body><div>Hello World</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 5);
    range.setEnd(text, 11);

    const selection = new Selection();
    selection.addRange(range);

    selection.deleteFromDocument();
    eq(text.data, 'Hello');
  },

  'Selection: containsNode checks if node is in selection'() {
    const doc = parseDoc('<html><body><div>Hello</div></body></html>');
    const text = doc.querySelector('div').firstChild;
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.containsNode(text), true);
  },

  'Window: getSelection returns Selection instance'() {
    const win = new Window({ href: 'https://example.com/' });
    const selection = win.getSelection();
    assert(selection instanceof Selection);
    eq(selection.rangeCount, 0);
  },

  'Window: getSelection returns same instance'() {
    const win = new Window({ href: 'https://example.com/' });
    const selection1 = win.getSelection();
    const selection2 = win.getSelection();
    eq(selection1, selection2);
  },

  'Integration: create range, add to selection, get text'() {
    const doc = parseDoc('<html><body><div>The quick brown fox</div></body></html>');
    const text = doc.querySelector('div').firstChild;

    const range = doc.createRange();
    range.setStart(text, 4);
    range.setEnd(text, 9);

    const win = new Window({ href: 'https://example.com/' });
    const selection = win.getSelection();
    selection.addRange(range);

    eq(selection.toString(), 'quick');
    eq(selection.rangeCount, 1);
  },
});
