// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
// REPL bracketed paste test - verifies bracketedPaste property
import { REPL } from '../../lib/repl.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

// Create REPL instance at module level like other tests
const repl = new REPL('unit', false);

tests({
  async 'bracketedPaste defaults to true'() {
    eq(repl.bracketedPaste, true);
  },

  async 'bracketedPaste can be set to false'() {
    repl.bracketedPaste = false;
    eq(repl.bracketedPaste, false);
    // Reset for other tests
    repl.bracketedPaste = true;
  },
});
