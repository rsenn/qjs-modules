import { assert, eq, tests } from './tinytest.js';
import { Document, Window, Range, Selection, Text } from '../lib/dom.js';

export default tests('Range and Selection API', {
  'Range: constructor creates empty range'() {
    const range = new Range();
    eq(range.startContainer, null);
    eq(range.startOffset, 0);
    eq(range.endContainer, null);
    eq(range.endOffset, 0);
    eq(range.collapsed, true);
  },

  'Range: setStart and setEnd'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const text = doc.createTextNode('Hello World');
    div.appendChild(text);

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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 6);
    range.setEnd(text, 11);

    eq(range.toString(), 'World');
  },

  'Range: toString returns empty for collapsed range'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 2);
    range.collapse(true);

    eq(range.toString(), '');
  },

  'Range: selectNodeContents'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const text1 = doc.createTextNode('Hello');
    const text2 = doc.createTextNode('World');
    div.appendChild(text1);
    div.appendChild(text2);

    const range = new Range();
    range.selectNodeContents(div);

    eq(range.startContainer, div);
    eq(range.startOffset, 0);
    eq(range.endContainer, div);
    eq(range.endOffset, 2);
  },

  'Range: cloneRange creates independent copy'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 1);
    range.setEnd(text, 3);

    const clone = range.cloneRange();
    eq(clone.startContainer, text);
    eq(clone.startOffset, 1);
    eq(clone.endContainer, text);
    eq(clone.endOffset, 3);

    // Modify original, clone should be unchanged
    range.setStart(text, 0);
    eq(clone.startOffset, 1);
  },

  'Range: deleteContents removes text'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 5);
    range.setEnd(text, 11);

    range.deleteContents();
    eq(text.data, 'Hello');
    eq(range.collapsed, true);
  },

  'Range: deleteContents removes child nodes'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    const span3 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);
    div.appendChild(span3);

    const range = new Range();
    range.setStart(div, 1);
    range.setEnd(div, 3);

    range.deleteContents();
    eq(div.childNodes.length, 1);
    eq(div.childNodes[0], span1);
  },

  'Range: cloneContents returns DocumentFragment'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const fragment = range.cloneContents();
    assert(fragment !== null);
    eq(fragment.childNodes.length, 1);
    eq(fragment.childNodes[0].data, 'Hello');

    // Original should be unchanged
    eq(text.data, 'Hello World');
  },

  'Range: cloneContents returns empty fragment for collapsed range'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 2);
    range.collapse(true);

    const fragment = range.cloneContents();
    eq(fragment.childNodes.length, 0);
  },

  'Range: extractContents clones and deletes'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 6);
    range.setEnd(text, 11);

    const fragment = range.extractContents();
    eq(fragment.childNodes[0].data, 'World');
    eq(text.data, 'Hello ');
  },

  'Range: insertNode into text node'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const text = doc.createTextNode('HelloWorld');
    div.appendChild(text);

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
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);

    const range = new Range();
    range.setStart(div, 1);
    range.setEnd(div, 1);

    const newSpan = doc.createElement('span');
    newSpan.id = 'inserted';
    range.insertNode(newSpan);

    eq(div.childNodes.length, 3);
    eq(div.childNodes[1], newSpan);
  },

  'Range: selectNode'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const span = doc.createElement('span');
    div.appendChild(span);

    const range = new Range();
    range.selectNode(span);

    eq(range.startContainer, div);
    eq(range.startOffset, 0);
    eq(range.endContainer, div);
    eq(range.endOffset, 1);
  },

  'Range: selectNode throws if node has no parent'() {
    const doc = new Document();
    const span = doc.createElement('span');

    const range = new Range();
    let threw = false;
    try {
      range.selectNode(span);
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },

  'Range: setStartBefore and setStartAfter'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);

    const range = new Range();
    range.setStartBefore(span2);
    eq(range.startOffset, 1);

    range.setStartAfter(span1);
    eq(range.startOffset, 1);
  },

  'Range: setEndBefore and setEndAfter'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);

    const range = new Range();
    range.setEndBefore(span2);
    eq(range.endOffset, 1);

    range.setEndAfter(span1);
    eq(range.endOffset, 1);
  },

  'Range: commonAncestorContainer with same container'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    eq(range.commonAncestorContainer, text);
  },

  'Range: commonAncestorContainer with different containers'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);

    const range = new Range();
    range.setStart(span1, 0);
    range.setEnd(span2, 0);

    eq(range.commonAncestorContainer, div);
  },

  'Range: surroundContents wraps content'() {
    const doc = new Document();
    const div = doc.createElement('div');
    const text = doc.createTextNode('Hello');
    div.appendChild(text);

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
    // Should not throw
    range.detach();
    eq(range.collapsed, true);
  },

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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    let threw = false;
    try {
      selection.getRangeAt(0);
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },

  'Selection: removeRange removes specific range'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    let threw = false;
    try {
      selection.removeRange(range);
    } catch(e) {
      threw = true;
    }
    assert(threw);
  },

  'Selection: removeAllRanges clears selection'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    selection.empty();
    eq(selection.rangeCount, 0);
  },

  'Selection: collapse sets caret at position'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');

    const selection = new Selection();
    selection.collapse(text, 3);

    eq(selection.rangeCount, 1);
    eq(selection.anchorNode, text);
    eq(selection.anchorOffset, 3);
    eq(selection.isCollapsed, true);
  },

  'Selection: collapseToStart moves to start'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const text = doc.createTextNode('Hello');
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
    const doc = new Document();
    const div = doc.createElement('div');
    const span1 = doc.createElement('span');
    const span2 = doc.createElement('span');
    div.appendChild(span1);
    div.appendChild(span2);

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
    const doc = new Document();
    const text = doc.createTextNode('Hello World');

    const selection = new Selection();
    selection.setBaseAndExtent(text, 0, text, 5);

    eq(selection.anchorNode, text);
    eq(selection.anchorOffset, 0);
    eq(selection.focusNode, text);
    eq(selection.focusOffset, 5);
    eq(selection.rangeCount, 1);
  },

  'Selection: extend modifies focus'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');

    const selection = new Selection();
    selection.collapse(text, 0);
    selection.extend(text, 5);

    eq(selection.anchorOffset, 0);
    eq(selection.focusOffset, 5);
  },

  'Selection: toString returns selected text'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.toString(), 'Hello');
  },

  'Selection: deleteFromDocument removes selected content'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello World');
    const range = new Range();
    range.setStart(text, 5);
    range.setEnd(text, 11);

    const selection = new Selection();
    selection.addRange(range);

    selection.deleteFromDocument();
    eq(text.data, 'Hello');
  },

  'Selection: containsNode checks if node is in selection'() {
    const doc = new Document();
    const text = doc.createTextNode('Hello');
    const range = new Range();
    range.setStart(text, 0);
    range.setEnd(text, 5);

    const selection = new Selection();
    selection.addRange(range);

    eq(selection.containsNode(text), true);
  },

  'Document: createRange returns Range instance'() {
    const doc = new Document();
    const range = doc.createRange();
    assert(range instanceof Range);
    eq(range.collapsed, true);
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
    const doc = new Document();
    const text = doc.createTextNode('The quick brown fox');

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
