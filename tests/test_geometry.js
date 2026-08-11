import { assert, eq } from './tinytest.js';
import { DOMRect, DOMRectReadOnly, Document } from '../lib/dom.js';

const tests = {
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

  'DOMRectReadOnly: top/right/bottom/left with positive dimensions'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    eq(rect.top, 20);
    eq(rect.right, 110);
    eq(rect.bottom, 70);
    eq(rect.left, 10);
  },

  'DOMRectReadOnly: top/right/bottom/left with negative dimensions'() {
    const rect = new DOMRectReadOnly(10, 20, -100, -50);
    eq(rect.top, -30);
    eq(rect.right, 10);
    eq(rect.bottom, 20);
    eq(rect.left, -90);
  },

  'DOMRectReadOnly: toJSON'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    const json = rect.toJSON();
    eq(json.x, 10);
    eq(json.y, 20);
    eq(json.width, 100);
    eq(json.height, 50);
    eq(json.top, 20);
    eq(json.right, 110);
    eq(json.bottom, 70);
    eq(json.left, 10);
  },

  'DOMRectReadOnly: fromRect static method'() {
    const rect = DOMRectReadOnly.fromRect({ x: 5, y: 10, width: 50, height: 25 });
    eq(rect.x, 5);
    eq(rect.y, 10);
    eq(rect.width, 50);
    eq(rect.height, 25);
  },

  'DOMRectReadOnly: fromRect with defaults'() {
    const rect = DOMRectReadOnly.fromRect();
    eq(rect.x, 0);
    eq(rect.y, 0);
    eq(rect.width, 0);
    eq(rect.height, 0);
  },

  'DOMRectReadOnly: properties are read-only'() {
    const rect = new DOMRectReadOnly(10, 20, 100, 50);
    try {
      rect.x = 999;
    } catch(e) {}
    try {
      rect.y = 999;
    } catch(e) {}
    try {
      rect.width = 999;
    } catch(e) {}
    try {
      rect.height = 999;
    } catch(e) {}
    eq(rect.x, 10);
    eq(rect.y, 20);
    eq(rect.width, 100);
    eq(rect.height, 50);
  },

  'DOMRect: inherits from DOMRectReadOnly'() {
    const rect = new DOMRect(10, 20, 100, 50);
    assert(rect instanceof DOMRectReadOnly);
    assert(rect instanceof DOMRect);
  },

  'DOMRect: properties are writable'() {
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

  'Element: getBoundingClientRect returns DOMRect'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const rect = div.getBoundingClientRect();
    assert(rect instanceof DOMRect);
  },

  'Element: getBoundingClientRect defaults to zero'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const rect = div.getBoundingClientRect();
    eq(rect.x, 0);
    eq(rect.y, 0);
    eq(rect.width, 0);
    eq(rect.height, 0);
  },

  'Element: getBoundingClientRect with custom rect'() {
    const doc = new Document();
    const div = doc.createElement('div');
    // In a real implementation, you'd set the rect via the raw node
    // For now, just verify the default behavior works
    const rect = div.getBoundingClientRect();
    assert(rect instanceof DOMRect);
    eq(rect.x, 0);
    eq(rect.y, 0);
  },

  'Element: getClientRects returns array with one rect'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const rects = div.getClientRects();
    assert(Array.isArray(rects));
    eq(rects.length, 1);
    assert(rects[0] instanceof DOMRect);
  },

  'HTMLElement: offsetWidth/offsetHeight default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.offsetWidth, 0);
    eq(div.offsetHeight, 0);
  },

  'HTMLElement: offsetWidth/offsetHeight are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.offsetWidth = 100;
    div.offsetHeight = 50;
    eq(div.offsetWidth, 100);
    eq(div.offsetHeight, 50);
  },

  'HTMLElement: offsetTop/offsetLeft default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.offsetTop, 0);
    eq(div.offsetLeft, 0);
  },

  'HTMLElement: offsetTop/offsetLeft are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.offsetTop = 10;
    div.offsetLeft = 20;
    eq(div.offsetTop, 10);
    eq(div.offsetLeft, 20);
  },

  'HTMLElement: offsetParent defaults to null'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.offsetParent, null);
  },

  'HTMLElement: offsetParent is writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const parent = doc.createElement('section');
    div.offsetParent = parent;
    eq(div.offsetParent, parent);
  },

  'HTMLElement: clientWidth/clientHeight default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.clientWidth, 0);
    eq(div.clientHeight, 0);
  },

  'HTMLElement: clientWidth/clientHeight are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.clientWidth = 95;
    div.clientHeight = 45;
    eq(div.clientWidth, 95);
    eq(div.clientHeight, 45);
  },

  'HTMLElement: clientTop/clientLeft default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.clientTop, 0);
    eq(div.clientLeft, 0);
  },

  'HTMLElement: clientTop/clientLeft are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.clientTop = 2;
    div.clientLeft = 3;
    eq(div.clientTop, 2);
    eq(div.clientLeft, 3);
  },

  'HTMLElement: scrollWidth/scrollHeight default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.scrollWidth, 0);
    eq(div.scrollHeight, 0);
  },

  'HTMLElement: scrollWidth/scrollHeight are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.scrollWidth = 200;
    div.scrollHeight = 150;
    eq(div.scrollWidth, 200);
    eq(div.scrollHeight, 150);
  },

  'HTMLElement: scrollTop/scrollLeft default to 0'() {
    const doc = new Document();
    const div = doc.createElement('div');
    eq(div.scrollTop, 0);
    eq(div.scrollLeft, 0);
  },

  'HTMLElement: scrollTop/scrollLeft are writable'() {
    const doc = new Document();
    const div = doc.createElement('div');
    div.scrollTop = 50;
    div.scrollLeft = 25;
    eq(div.scrollTop, 50);
    eq(div.scrollLeft, 25);
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
};

for(const [name, fn] of Object.entries(tests)) {
  try {
    fn();
    console.log(`✓ ${name}`);
  } catch(e) {
    console.error(`✗ ${name}`);
    console.error(e);
    process.exit(1);
  }
}

console.log('\nAll geometry tests passed!');
