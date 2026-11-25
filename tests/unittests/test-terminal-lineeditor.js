// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
//: lib/terminal.js: LineEditor tests
import { LineEditor, decodeKeys, stringWidth } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'LineEditor basic insert'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'abc');
    eq(ed.cursor, 3);
  },

  'LineEditor backspace'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'backspace', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'ab');
    eq(ed.cursor, 2);
  },

  'LineEditor delete right'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'delete', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'ab');
    eq(ed.cursor, 2);
    eq(ed.key({ name: 'delete', ctrl: false, meta: false, shift: false }), null);
  },

  'LineEditor home end'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'home', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 0);
    ed.key({ name: 'end', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 3);
  },

  'LineEditor ctrl-left word jump'() {
    const ed = new LineEditor();

    ed.set('ab cd');
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 3);
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 0);
    eq(ed.key({ name: 'left', ctrl: true, meta: false, shift: false }), null);
    eq(ed.cursor, 0);
  },

  'LineEditor ctrl-right word jump'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 0);
    ed.key({ name: 'right', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 3);

    ed.set('ab cd');
    ed.key({ name: 'home', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'right', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 2);
    ed.key({ name: 'right', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 5);
  },

  'LineEditor kill ring'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'k', ctrl: true });
    eq(ed.killRing, 'c');
    eq(ed.cmd, 'ab');
    eq(ed.cursor, 2);
  },

  'LineEditor kill ring yank'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'k', ctrl: true });
    ed.key({ name: 'y', ctrl: true });
    eq(ed.cmd, 'abc');
  },

  'LineEditor Ctrl-U kill to start'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'u', ctrl: true });
    eq(ed.killRing, 'abc');
    eq(ed.cmd, '');
    eq(ed.cursor, 0);
  },

  'LineEditor history'() {
    const ed = new LineEditor({ history: ['one', 'two'] });
    ed.set('draft');
    ed.key({ name: 'up', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'two');
    ed.key({ name: 'up', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'one');
    ed.key({ name: 'down', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'two');
    ed.key({ name: 'down', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'draft');
    ed.key({ name: 'down', ctrl: false, meta: false, shift: false });
    eq(ed.history.length, 3);
    eq(ed.history[2], 'draft');
    ed.key({ name: 'down', ctrl: false, meta: false, shift: false });
    eq(ed.history.length, 3);
    eq(ed.accept(), '');
    ed.set('x');
    eq(ed.accept(), 'x');
    eq(ed.history[ed.history.length - 1], 'x');
  },

  'LineEditor multi-line'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'return', ctrl: false, meta: true });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'paste', paste: 'c\r\nd' });
    eq(ed.cmd, 'a\nbc\nd');
    const { lines, row, col } = ed.layout(['> ', '. '], 20);
    eq(lines.join('|'), '> a|. bc|. d');
    eq(`${row},${col}`, '2,3');
    ed.key({ name: 'up', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 3);
  },

  'LineEditor history filter'() {
    const ed = new LineEditor();
    // Test historyFilter - only allow entries that don't start with '#'
    ed.historyFilter = (entry) => !entry.startsWith('#');
    ed.set('comment');
    eq(ed.accept(), 'comment');
    eq(ed.history.length, 1);
    eq(ed.history[0], 'comment');
    // Filtered entry should not be added
    ed.set('#filtered');
    eq(ed.accept(), '#filtered');
    eq(ed.history.length, 1); // Still 1, not 2
  },

  'LineEditor onRemember callback'() {
    const ed = new LineEditor();
    const remembered = [];
    ed.onRemember = (entry) => remembered.push(entry);
    ed.set('first');
    eq(ed.accept(), 'first');
    eq(remembered.length, 1);
    eq(remembered[0], 'first');
    ed.set('second');
    eq(ed.accept(), 'second');
    eq(remembered.length, 2);
    eq(remembered[1], 'second');
  },

  'LineEditor loadHistory and saveHistory'() {
    const ed = new LineEditor({ history: ['one', 'two'] });
    // saveHistory returns a copy
    const saved = ed.saveHistory();
    eq(saved.length, 2);
    eq(saved[0], 'one');
    eq(saved[1], 'two');
    eq(ed.history.length, 2); // original unchanged
    eq(ed.history[0], 'one');
    eq(ed.history[1], 'two');
    // Modify returned array should not affect history
    saved.push('three');
    eq(ed.history.length, 2);
    // loadHistory replaces history and resets historyPos
    ed.loadHistory(['new1', 'new2', 'new3']);
    eq(ed.history.length, 3);
    eq(ed.history[0], 'new1');
    eq(ed.history[1], 'new2');
    eq(ed.history[2], 'new3');
    eq(ed.historyPos, -1);
  },

  'LineEditor CJK cursor math'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: '𠮷', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'a𠮷');
    eq(stringWidth(ed.cmd), 3);
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 1);
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 0);
    ed.key({ name: 'right', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'right', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 3);
  },

  'LineEditor Ctrl-C'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'ctrlc', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, '');
    eq(ed.accept(), '');
  },

  'LineEditor Ctrl-D on empty'() {
    const ed = new LineEditor();
    ed.key({ name: 'ctrlD', ctrl: false, meta: false, shift: false });
    eq(ed.accept(), '');
  },

  'LineEditor layout'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    const { lines, row, col } = ed.layout(['> ', '. '], 80);
    eq(lines.join('|'), '> ab');
    eq(`${row},${col}`, '0,4');
  },

  'LineEditor kill word with Ctrl-W'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'd', ctrl: false, meta: false, shift: false });
    // cursor at end (4), then left twice to position 2 (before 'c')
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 2);
    // Ctrl-W kills the word before the cursor, the cursor goes to where it was
    ed.key({ name: 'w', ctrl: true });
    eq(ed.cmd, 'cd');
    eq(ed.killRing, 'ab');
    eq(ed.cursor, 0);
  },

  'LineEditor tab completion'() {
    const ed = new LineEditor();
    ed.set('hello world');
    ed.complete = (text, pos) => ({ suggestion: 'there', range: [5, 10] });
    ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'hello there');
    eq(ed.cursor, 13);
    eq(ed.ghost, ''); // suggestion (5) < original range (5), so no ghost
  },

  'LineEditor tab completion with ghost text'() {
    const ed = new LineEditor();
    ed.set('hello world');
    ed.complete = (text, pos) => ({ suggestion: 'there', range: [3, 8] });
    ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'helthero world'); // 'hel' + 'there' + 'o world'
    eq(ed.cursor, 11);
    eq(ed.ghost, ''); // suggestion length equals range length, no ghost
  },

  'LineEditor tab completion append at end'() {
    const ed = new LineEditor();
    ed.set('hello');
    ed.complete = (text, pos) => ({ suggestion: ' world', range: [5, 5] });
    ed.key({ name: 'tab', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'hello world');
    eq(ed.cursor, 11);
    eq(ed.ghost, ''); // start >= cmd.length, so just appended, no ghost
  },
});