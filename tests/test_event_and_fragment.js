import { Event, EventTarget, Document, Element, DocumentFragment } from '../lib/dom.js';

console.log('=== Testing Event and DocumentFragment ===\n');

// Test 1: Event constructor
console.log('Test 1: Event constructor with type only');
const event1 = new Event('click');
console.log('  type:', event1.type);
console.log('  bubbles:', event1.bubbles);
console.log('  cancelable:', event1.cancelable);
console.log('  Pass:', event1.type === 'click' && event1.bubbles === false && event1.cancelable === false);

// Test 2: Event constructor with options
console.log('\nTest 2: Event constructor with options');
const event2 = new Event('submit', { bubbles: true, cancelable: true, composed: true });
console.log('  type:', event2.type);
console.log('  bubbles:', event2.bubbles);
console.log('  cancelable:', event2.cancelable);
console.log('  composed:', event2.composed);
console.log('  Pass:', event2.type === 'submit' && event2.bubbles === true && event2.cancelable === true && event2.composed === true);

// Test 3: Event preventDefault
console.log('\nTest 3: Event preventDefault');
const event3 = new Event('click', { cancelable: true });
console.log('  Before preventDefault:', event3.defaultPrevented);
event3.preventDefault();
console.log('  After preventDefault:', event3.defaultPrevented);
console.log('  Pass:', event3.defaultPrevented === true);

// Test 4: EventTarget addEventListener and dispatchEvent
console.log('\nTest 4: EventTarget addEventListener and dispatchEvent');
const target = new EventTarget();
let called = false;
target.addEventListener('test', () => { called = true; });
target.dispatchEvent(new Event('test'));
console.log('  Listener called:', called);
console.log('  Pass:', called === true);

// Test 5: EventTarget removeEventListener
console.log('\nTest 5: EventTarget removeEventListener');
const target2 = new EventTarget();
let callCount = 0;
const listener = () => callCount++;
target2.addEventListener('test', listener);
target2.dispatchEvent(new Event('test'));
console.log('  After first dispatch:', callCount);
target2.removeEventListener('test', listener);
target2.dispatchEvent(new Event('test'));
console.log('  After second dispatch (should not increment):', callCount);
console.log('  Pass:', callCount === 1);

// Test 6: EventTarget once option
console.log('\nTest 6: EventTarget once option');
const target3 = new EventTarget();
let onceCount = 0;
target3.addEventListener('test', () => onceCount++, { once: true });
target3.dispatchEvent(new Event('test'));
target3.dispatchEvent(new Event('test'));
target3.dispatchEvent(new Event('test'));
console.log('  Call count (should be 1):', onceCount);
console.log('  Pass:', onceCount === 1);

// Test 7: Event bubbling
console.log('\nTest 7: Event bubbling through DOM tree');
const doc = new Document({ tagName: 'html', attributes: {}, children: [] });
const parent = doc.createElement('div');
const child = doc.createElement('span');
parent.appendChild(child);

const events = [];
parent.addEventListener('click', (e) => {
  events.push({ target: e.target, currentTarget: e.currentTarget, phase: e.eventPhase });
});
child.addEventListener('click', (e) => {
  events.push({ target: e.target, currentTarget: e.currentTarget, phase: e.eventPhase });
});

child.dispatchEvent(new Event('click', { bubbles: true }));
console.log('  Events fired:', events.length);
console.log('  First event - target:', events[0]?.target === child, 'currentTarget:', events[0]?.currentTarget === child, 'phase:', events[0]?.phase === Event.AT_TARGET);
console.log('  Second event - target:', events[1]?.target === child, 'currentTarget:', events[1]?.currentTarget === parent, 'phase:', events[1]?.phase === Event.BUBBLING_PHASE);
console.log('  Pass:', events.length === 2 && events[0].phase === Event.AT_TARGET && events[1].phase === Event.BUBBLING_PHASE);

// Test 8: DocumentFragment constructor
console.log('\nTest 8: DocumentFragment constructor');
const frag = new DocumentFragment();
console.log('  nodeType:', frag.nodeType);
console.log('  nodeName:', frag.nodeName);
console.log('  childNodes.length:', frag.childNodes.length);
console.log('  Pass:', frag.nodeType === 11 && frag.nodeName === '#document-fragment' && frag.childNodes.length === 0);

// Test 9: DocumentFragment appendChild
console.log('\nTest 9: DocumentFragment appendChild');
const doc2 = new Document({ tagName: 'html', attributes: {}, children: [] });
const frag2 = doc2.createDocumentFragment();
const div1 = doc2.createElement('div');
const div2 = doc2.createElement('div');
frag2.appendChild(div1);
frag2.appendChild(div2);
console.log('  Fragment childNodes.length:', frag2.childNodes.length);
console.log('  Pass:', frag2.childNodes.length === 2);

// Test 10: DocumentFragment appendChild to element moves children
console.log('\nTest 10: DocumentFragment appendChild to element moves children');
const doc3 = new Document({ tagName: 'html', attributes: {}, children: [] });
const container = doc3.createElement('div');
const frag3 = doc3.createDocumentFragment();
const div3 = doc3.createElement('div');
const div4 = doc3.createElement('div');
frag3.appendChild(div3);
frag3.appendChild(div4);
console.log('  Before - fragment length:', frag3.childNodes.length);
container.appendChild(frag3);
console.log('  After - fragment length:', frag3.childNodes.length);
console.log('  After - container length:', container.childNodes.length);
console.log('  Pass:', frag3.childNodes.length === 0 && container.childNodes.length === 2);

// Test 11: createDocumentFragment from Document
console.log('\nTest 11: createDocumentFragment from Document');
const doc4 = new Document({ tagName: 'html', attributes: {}, children: [] });
const frag4 = doc4.createDocumentFragment();
console.log('  Is DocumentFragment:', frag4 instanceof DocumentFragment);
console.log('  nodeType:', frag4.nodeType);
console.log('  Pass:', frag4 instanceof DocumentFragment && frag4.nodeType === 11);

// Test 12: Node extends EventTarget
console.log('\nTest 12: Node extends EventTarget');
const doc5 = new Document({ tagName: 'html', attributes: {}, children: [] });
const div = doc5.createElement('div');
console.log('  Has addEventListener:', typeof div.addEventListener === 'function');
console.log('  Has removeEventListener:', typeof div.removeEventListener === 'function');
console.log('  Has dispatchEvent:', typeof div.dispatchEvent === 'function');
console.log('  Pass:', typeof div.addEventListener === 'function' && typeof div.removeEventListener === 'function' && typeof div.dispatchEvent === 'function');

// Test 13: Event static constants
console.log('\nTest 13: Event static constants');
console.log('  Event.NONE:', Event.NONE);
console.log('  Event.CAPTURING_PHASE:', Event.CAPTURING_PHASE);
console.log('  Event.AT_TARGET:', Event.AT_TARGET);
console.log('  Event.BUBBLING_PHASE:', Event.BUBBLING_PHASE);
console.log('  Pass:', Event.NONE === 0 && Event.CAPTURING_PHASE === 1 && Event.AT_TARGET === 2 && Event.BUBBLING_PHASE === 3);

// Test 14: Event stopPropagation
console.log('\nTest 14: Event stopPropagation prevents parent handlers');
const doc6 = new Document({ tagName: 'html', attributes: {}, children: [] });
const parent2 = doc6.createElement('div');
const child2 = doc6.createElement('span');
parent2.appendChild(child2);

let parentCalled = false;
let childCalled = false;

parent2.addEventListener('test', () => { parentCalled = true; });
child2.addEventListener('test', (e) => {
  childCalled = true;
  e.stopPropagation();
});

child2.dispatchEvent(new Event('test', { bubbles: true }));
console.log('  Child called:', childCalled);
console.log('  Parent called:', parentCalled);
console.log('  Pass:', childCalled === true && parentCalled === false);

// Test 15: Event stopImmediatePropagation
console.log('\nTest 15: Event stopImmediatePropagation prevents other listeners');
const target4 = new EventTarget();
const calls = [];

target4.addEventListener('test', (e) => {
  calls.push('first');
  e.stopImmediatePropagation();
});

target4.addEventListener('test', () => {
  calls.push('second');
});

target4.dispatchEvent(new Event('test'));
console.log('  Calls:', calls);
console.log('  Pass:', calls.length === 1 && calls[0] === 'first');

console.log('\n✓ All tests completed');
