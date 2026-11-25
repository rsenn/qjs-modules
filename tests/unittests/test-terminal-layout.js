// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js layoutInput(): the rows an edited text takes and the cursor in them.
 * Native in textcode on qjsm, pure JS elsewhere: one table of expectations for both. */
import { layoutInput, stringWidth } from '../../lib/terminal.js';
import { LineEditor } from '../../lib/terminal/lineeditor.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const P = ['> ', '. '];
const show = r => `${r.lines.join('|')} @${r.row},${r.col}`;
const lay = (text, cursor, columns, prefixes = P) => show(layoutInput(prefixes, text, cursor, columns));

tests({
  'a single line and the cursor after it'() {
    eq('> ab @0,4', lay('ab', 2, 80));
  },

  'newlines start rows with the second prefix'() {
    eq('> ab|. cd @1,3', lay('ab\ncd', 4, 80));
    eq('> a|. b|. c @2,3', lay('a\nb\nc', 5, 80));
  },

  'a row is cut at columns and the cursor follows'() {
    eq('> abc|de @1,1', lay('abcde', 4, 5));
    eq('> ab|cdef| @2,0', lay('abcdef', 6, 4));
  },

  'a cursor after a full row sits at the start of the next one'() {
    eq('> abc| @1,0', lay('abc', 3, 5));
  },

  'the cursor at 0 and before the first character'() {
    eq('> ab @0,2', lay('ab', 0, 80));
    eq('> ab|. cd @1,2', lay('ab\ncd', 3, 80));
  },

  'an empty text is one row with the prefix'() {
    eq('>  @0,2', lay('', 0, 80));
  },

  'a trailing newline adds an empty row'() {
    eq('> a|.  @1,2', lay('a\n', 2, 80));
    eq('> a|.  @0,3', lay('a\n', 1, 80));
  },

  'consecutive newlines give empty rows'() {
    eq('> |. |.  @1,2', lay('\n\n', 1, 80));
  },

  'wide characters take two cells'() {
    eq('> 漢|字 @1,0', lay('漢字', 1, 5));
    eq('> 漢|字 @1,2', lay('漢字', 2, 5));
  },

  'combining marks and controls take no cells'() {
    eq('> éx @0,4', lay('éx', 3, 80));
    eq('> a\tb @0,4', lay('a\tb', 3, 80));
  },

  'a surrogate pair is one step and a cursor inside it is not found'() {
    eq('> 😀b @0,5', lay('😀b', 3, 80));
    eq('> 😀b @0,4', lay('😀b', 2, 80));
    eq('> 😀b @0,0', lay('😀b', 1, 80));
  },

  'a cursor past the text or below 0 stays at 0,0'() {
    eq('> ab @0,0', lay('ab', 9, 80));
    eq('> ab @0,0', lay('ab', -1, 80));
  },

  'the prefixes count in cells, colors count 0'() {
    eq('\x1b[32m> \x1b[0mab @0,4', lay('ab', 2, 80, ['\x1b[32m> \x1b[0m', '. ']));
    eq('漢> ab @0,6', lay('ab', 2, 80, ['漢> ', '. ']));
  },

  'a missing continuation prefix is empty'() {
    eq('> a|b @1,1', lay('a\nb', 3, 80, ['> ']));
  },

  'a prefix wider than columns pushes an empty first row'() {
    eq('>>>> |a @1,1', lay('a', 1, 2, ['>>>> ']));
  },

  'columns 1 puts every character on its own row'() {
    const r = layoutInput(['', ''], 'abc', 3, 1);

    eq('a,b,c,', r.lines.join());
    eq('3,0', `${r.row},${r.col}`);
  },

  'the result is plain data: lines, row and col'() {
    const r = layoutInput(P, 'ab', 1, 80);

    eq('lines,row,col', Object.keys(r).join());
    assert(Array.isArray(r.lines));
    eq('number', typeof r.row);
  },

  'every cursor position of a mixed text stays inside the rows'() {
    const text = 'ab 漢字\nñ😀é\n\nlast line here';

    for(let c = 0; c <= text.length; c++) {
      const r = layoutInput(P, text, c, 7);

      assert(r.row >= 0 && r.row < r.lines.length, `row ${r.row} at ${c}`);
      assert(r.col <= Math.max(7, stringWidth(r.lines[r.row] ?? '')), `col ${r.col} at ${c}`);
    }
  },

  'LineEditor.layout is layoutInput of the text and cursor'() {
    const ed = new LineEditor();

    ed.set('ab\ncd');
    eq('> ab|. cd @1,4', show(ed.layout(P, 80)));
    ed.cursor = 1;
    eq('> ab|. cd @0,3', show(ed.layout(P, 80)));
  },
});
