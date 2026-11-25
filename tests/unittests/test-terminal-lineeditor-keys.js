// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js LineEditor fed with raw key bytes through decodeKeys(): Tier 32.10 (Ctrl-C, Ctrl-D and
 * Return are the keys decodeKeys() reports) and 32.11 (the editing and history the portable test checks) */
import { decodeKeys } from '../../lib/terminal/keys.js';
import { LineEditor } from '../../lib/terminal/lineeditor.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* the editor, and feed(bytes) giving what key() returned for each decoded key */
const make = (options, text) => {
  const ed = new LineEditor(options);

  if(text !== undefined) ed.set(text);

  const feed = bytes => decodeKeys(bytes, true).keys.map(k => ed.key(k.key));

  return { ed, feed };
};

tests({
  /* 32.10: Ctrl-C, Ctrl-D, Return */

  'Ctrl-C cancels and clears the line, it does not insert C'() {
    const { ed, feed } = make({}, 'ab');

    eq(feed('\x03').join(), 'cancel');
    eq(ed.cmd, '');
    eq(ed.cursor, 0);
  },

  'Ctrl-D on an empty line is eof'() {
    const { ed, feed } = make();

    eq(feed('\x04').join(), 'eof');
    eq(ed.cmd, '');
  },

  'Ctrl-D in a line deletes right and does not insert D'() {
    const { ed, feed } = make({}, 'abc');

    eq(feed('\x04').join(), '');
    eq(ed.cmd, 'abc');

    feed('\x01\x04');
    eq(ed.cmd, 'bc');
    eq(ed.cursor, 0);
  },

  'Return submits, so does a line feed'() {
    const a = make({}, 'x');
    const b = make({}, 'y');

    eq(a.feed('\r').join(), 'submit');
    eq(b.feed('\n').join(), 'submit');
    eq(a.ed.accept(), 'x');
    eq(b.ed.accept(), 'y');
  },

  'Return in multiline mode, Alt-Enter and Shift-Enter insert a newline'() {
    const m = make({}, 'a');

    m.ed.multiline = true;
    eq(m.feed('\r').join(), 'change');
    eq(m.ed.cmd, 'a\n');

    const alt = make({}, 'a');

    eq(alt.feed('\x1b\r').join(), 'change');
    eq(alt.ed.cmd, 'a\n');

    const shift = new LineEditor();

    shift.set('a');
    eq(shift.key({ name: 'return', shift: true }), 'change');
    eq(shift.cmd, 'a\n');
  },

  'Escape cancels'() {
    const { ed } = make({}, 'abc');

    eq(ed.key({ name: 'escape' }), 'cancel');
    eq(ed.cmd, '');
  },

  'the names ctrlc, ctrlD and ctrlJ of older callers still work'() {
    const a = make({}, 'ab');
    const b = make();
    const c = make({}, 'ab');

    eq(a.ed.key({ name: 'ctrlc' }), 'cancel');
    eq(a.ed.cmd, '');
    eq(b.ed.key({ name: 'ctrlD' }), 'eof');
    eq(c.ed.key({ name: 'ctrlJ' }), 'change');
    eq(c.ed.cmd, 'ab\n');
  },

  'a Ctrl chord that is not an editing key inserts nothing'() {
    const { ed, feed } = make({}, 'ab');

    eq(feed('\x18\x1a').join(), ',');
    eq(ed.cmd, 'ab');
  },

  'the repro of the bug: Ctrl-C then Ctrl-D leave no letters behind'() {
    const { ed, feed } = make();

    feed('x');
    eq(feed('\x03')[0], 'cancel');
    eq(feed('\x04')[0], 'eof');
    eq(ed.cmd, '');
  },

  /* 32.11: editing and history */

  'Ctrl-W kills the word before the cursor, Ctrl-Y yanks it back'() {
    const { ed, feed } = make({}, 'hello world');

    feed('\x1b[D\x1b[D');
    eq(ed.cursor, 9);
    feed('\x17');
    eq(ed.cmd, 'hello ld');
    eq(ed.killRing, 'wor');
    eq(ed.cursor, 6);
    feed('\x19');
    eq(ed.cmd, 'hello world');
    eq(ed.cursor, 9);
  },

  'Ctrl-W skips spaces before the word'() {
    const { ed, feed } = make({}, 'one two   ');

    feed('\x17');
    eq(ed.cmd, 'one ');
    eq(ed.killRing, 'two   ');
  },

  'Ctrl-K and Ctrl-U kill within the line and leave the cursor at the cut'() {
    const { ed, feed } = make({}, 'abcd');

    feed('\x1b[D\x1b[D\x0b');
    eq(ed.cmd, 'ab');
    eq(ed.killRing, 'cd');
    eq(ed.cursor, 2);

    feed('\x1b[D\x15');
    eq(ed.cmd, 'b');
    eq(ed.killRing, 'a');
    eq(ed.cursor, 0);
  },

  'Ctrl-K and Ctrl-U in a text with newlines stay on the line'() {
    const { ed } = make({}, 'ab\ncd\nef');

    ed.cursor = 4;
    ed.key({ name: 'k', ctrl: true });
    eq(ed.cmd, 'ab\nc\nef');
    ed.key({ name: 'u', ctrl: true });
    eq(ed.cmd, 'ab\n\nef');
    eq(ed.cursor, 3);
    ed.key({ name: 'k', ctrl: true });
    eq(ed.cmd, 'ab\nef');
  },

  'word kills and moves: Alt-Backspace, Ctrl-Delete, Alt-D, Alt-B, Alt-F, Ctrl-arrows'() {
    const { ed, feed } = make({}, 'one two three');

    feed('\x1b\x7f');
    eq(ed.cmd, 'one two ');
    feed('\x1b[1;5D');
    eq(ed.cursor, 4);
    feed('\x1bf');
    eq(ed.cursor, 7);
    feed('\x1bb\x1bb');
    eq(ed.cursor, 0);
    feed('\x1b[3;5~');
    eq(ed.cmd, ' two ');
    feed('\x1bd');
    eq(ed.cmd, ' ');
    feed('\x1b[1;5C');
    eq(ed.cursor, 1);
  },

  'Home, End and Ctrl-A, Ctrl-E move on the line'() {
    const { ed, feed } = make({}, 'ab\ncd');

    feed('\x01');
    eq(ed.cursor, 3);
    feed('\x05');
    eq(ed.cursor, 5);
    feed('\x1b[H');
    eq(ed.cursor, 3);
    feed('\x1b[F');
    eq(ed.cursor, 5);
  },

  'Backspace, Delete and the arrows step over a surrogate pair'() {
    const { ed, feed } = make();

    feed('a😀');
    eq(ed.cmd, 'a😀');
    feed('\x1b[D');
    eq(ed.cursor, 1);
    feed('\x1b[C');
    eq(ed.cursor, 3);
    feed('\x7f');
    eq(ed.cmd, 'a');
    feed('😀\x1b[D\x1b[3~');
    eq(ed.cmd, 'a');
  },

  'a left arrow at the start and a delete at the end change nothing'() {
    const { ed, feed } = make({}, 'ab');

    eq(feed('\x1b[C').join(), '');
    eq(feed('\x1b[3~').join(), '');
    eq(feed('\x01\x1b[D').join(), 'change,');
    eq(ed.cmd, 'ab');
  },

  'a paste is inserted with CRLF as LF'() {
    const { ed, feed } = make({}, 'x');

    eq(feed('\x1b[200~a\r\nb\rc\x1b[201~').join(), 'change');
    eq(ed.cmd, 'xa\nb\nc');
    eq(ed.cursor, 6);
  },

  'Up and Down move between the lines of a text, then use the history'() {
    const { ed } = make({ history: ['old'] });

    ed.set('ab\ncde\nf');
    ed.cursor = 5;
    ed.key({ name: 'up' });
    eq(ed.cursor, 2);
    eq(ed.cmd, 'ab\ncde\nf');
    ed.key({ name: 'up' });
    eq(ed.cmd, 'old');
    eq(ed.history.join('|'), 'old|ab\ncde\nf');
  },

  'Down on a new entry does nothing and keeps the text'() {
    const { ed } = make({ history: ['old'] }, 'draft');

    eq(ed.key({ name: 'down' }), null);
    eq(ed.cmd, 'draft');
    eq(ed.cursor, 5);
    eq(ed.history.join(), 'old');
  },

  'Down moves to the line below at the same column and clamps to its end'() {
    const { ed } = make({}, 'abcd\nx');

    ed.cursor = 3;
    ed.key({ name: 'down' });
    eq(ed.cursor, 6);
    eq(ed.key({ name: 'down' }), null);
  },

  'history: Up keeps the draft, Down walks back, past the newest is one new empty entry'() {
    const { ed } = make({ history: ['one', 'two'] }, 'draft');
    const up = () => ed.key({ name: 'up' });
    const down = () => ed.key({ name: 'down' });

    eq(up(), 'change');
    eq(ed.cmd, 'two');
    up();
    eq(ed.cmd, 'one');
    eq(up(), null);
    eq(ed.cmd, 'one');
    down();
    eq(ed.cmd, 'two');
    down();
    eq(ed.cmd, 'draft');
    down();
    eq(ed.cmd, '');
    eq(ed.history.join(), 'one,two,draft');
    eq(down(), null);
    eq(ed.history.length, 3);
    eq(ed.cursor, 0);
  },

  'history: Up on an empty line shows the newest entry and Down returns to the empty line'() {
    const { ed } = make({ history: ['a', 'b'] });

    ed.key({ name: 'up' });
    eq(ed.cmd, 'b');
    ed.key({ name: 'down' });
    eq(ed.cmd, '');
    eq(ed.history.join(), 'a,b');
  },

  'history: accept adds the text once and an empty or repeated line is not added'() {
    const { ed } = make({ history: ['a'] });

    ed.set('');
    eq(ed.accept(), '');
    eq(ed.history.join(), 'a');

    ed.set('b');
    eq(ed.accept(), 'b');
    ed.set('b');
    eq(ed.accept(), 'b');
    eq(ed.history.join(), 'a,b');
    eq(ed.cmd, '');
    eq(ed.cursor, 0);
    eq(ed.historyPos, -1);
  },

  'history: a line taken from the history and accepted again is not added twice'() {
    const { ed } = make({ history: ['one', 'two'] });

    ed.key({ name: 'up' });
    eq(ed.accept(), 'two');
    eq(ed.history.join(), 'one,two');
  },

  'history: maxHistory drops the oldest'() {
    const { ed } = make({ history: ['a', 'b'], maxHistory: 2 });

    ed.set('c');
    ed.accept();
    eq(ed.history.join(), 'b,c');
  },

  'the cursor stays a valid index after every key'() {
    const { ed, feed } = make({ history: ['abc', 'de'] });

    feed('xy\x1b[A\x1b[A\x1b[B\x17\x0b\x15\x19\x1b[D\x7f\x1b[3~\x1b[200~p\nq\x1b[201~\x1b[A\x1b[B');
    assert(ed.cursor >= 0 && ed.cursor <= ed.cmd.length, `${ed.cursor} of ${ed.cmd.length}`);
  },
});
