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
    ed.key({ name: 'delete', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'ab');
    eq(ed.cursor, 2);
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
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'd', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 3);
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 1);
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 1);
  },

  'LineEditor ctrl-right word jump'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 1);
    ed.key({ name: 'right', ctrl: true, meta: false, shift: false });
    eq(ed.cursor, 3);
  },

  'LineEditor kill ring'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
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
    eq(ed.cmd, 'draft');
    ed.key({ name: 'down', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, '');
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

  'LineEditor CJK cursor math'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: '𠮷', ctrl: false, meta: false, shift: false });
    eq(ed.cmd, 'a𠮷');
    eq(stringWidth(ed.cmd), 3);
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 2);
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 1);
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
    eq(`${row},${col}`, '1,2');
  },

  'LineEditor kill word with Ctrl-Q'() {
    const ed = new LineEditor();
    ed.key({ name: 'a', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'b', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'c', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'd', ctrl: false, meta: false, shift: false });
    // cursor at end (4), then left twice to position 2 (before 'c')
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    ed.key({ name: 'left', ctrl: false, meta: false, shift: false });
    eq(ed.cursor, 2);
    // Ctrl-Q (or 'w' with ctrl) kills word at cursor
    ed.key({ name: 'w', ctrl: true });
    eq(ed.cmd, 'ab');  // 'c' killed
    eq(ed.killRing, 'cd');  // 'cd' killed (from pos 2 to end)
  },
});
