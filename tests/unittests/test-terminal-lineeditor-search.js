// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js LineEditor reverse search feature */
import { LineEditor } from '../../lib/terminal.js';
import { eq, tests } from '../../lib/tinytest.js';

tests({
  'Reverse search: constructor sets searchMode false, searchBuffer empty, searchIndex -1'() {
    const ed = new LineEditor();

    eq(ed.searchMode, false);
    eq(ed.searchBuffer, '');
    eq(ed.searchIndex, -1);
  },

  'Reverse search: escape exits search mode when active'() {
    const ed = new LineEditor();

    ed.searchMode = true;
    ed.searchBuffer = 'hel';
    ed.searchIndex = 0;

    const result = ed.key({ name: 'escape' });
    eq(result, 'change');
    eq(ed.searchMode, false);
    eq(ed.searchBuffer, '');
    eq(ed.searchIndex, -1);
  },

  'Reverse search: ctrlC exits search mode'() {
    const ed = new LineEditor();

    ed.searchMode = true;
    ed.searchBuffer = 'hel';
    ed.searchIndex = 0;

    const result = ed.key({ name: 'ctrlc', ctrl: true });
    eq(result, 'change');
    eq(ed.searchMode, false);
    eq(ed.searchBuffer, '');
    eq(ed.searchIndex, -1);
  },

  'Reverse search: enter accepts match and exits'() {
    const ed = new LineEditor({ history: ['hello world', 'hello there', 'goodbye'] });

    ed.searchMode = true;
    ed.searchBuffer = 'hello';
    ed.searchIndex = 1;  /* pointing to 'hello there' */

    /* Test enter key */
    const result = ed.key({ name: 'enter' });
    eq(result, 'submit');
    eq(ed.cmd, 'hello there');
    eq(ed.cursor, 11);
    eq(ed.searchMode, false);
  },

  'Reverse search: backspace removes char and re-searches'() {
    const ed = new LineEditor({ history: ['hello world', 'hello there', 'goodbye'] });

    ed.searchMode = true;
    ed.searchBuffer = 'hell';
    ed.searchIndex = 1;

    /* Test backspace key */
    const result = ed.key({ name: 'backspace' });
    eq(result, 'change');
    eq(ed.searchBuffer, 'hel');
    /* Should find a match starting with 'hel' */
    eq(ed.searchIndex, 0 || ed.searchIndex === 1);
  },

  'Reverse search: layout shows prompt when in search mode'() {
    const ed = new LineEditor({ history: ['hello world'] });

    ed.searchMode = true;
    ed.searchBuffer = 'hel';

    const layoutResult = ed.layout([], 80);

    /* first line should be the search prompt */
    eq(layoutResult.lines[0], `(reverse-i-search)'hel': `);
    eq(layoutResult.row, 0);
  },

  'Reverse search: printable chars append to searchBuffer'() {
    const ed = new LineEditor();

    ed.searchMode = true;
    ed.searchBuffer = '';

    /* Test with a printable char */
    const result = ed.key({ name: 'a' });
    eq(result, 'change');
    eq(ed.searchBuffer, 'a');
  },
});