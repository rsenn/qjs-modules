/* lib/repl.js: the parts of REPL that work without a terminal */
import { REPL } from '../../lib/repl.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* the constructor prints the first prompt; nothing else here writes to the terminal */
const repl = new REPL('unit', false);

tests({
  'the name becomes the prompt prefix'() {
    eq('unit> ', repl.ps1);
  },

  'a new REPL starts with an empty command line'() {
    eq('', repl.cmd);
    eq(0, repl.cursorPos);
    eq(0, repl.level);
  },

  'dupstr repeats a string'() {
    eq('ababab', repl.dupstr('ab', 3));
    eq('', repl.dupstr('ab', 0));
  },

  'getCompletions completes a global name'() {
    const { tab, pos } = repl.getCompletions('Obj', 3);

    eq('Object', tab.join());
    eq(3, pos);
  },

  'getCompletions completes a property of an object'() {
    const { tab, pos } = repl.getCompletions('Math.fl', 7);

    eq('floor', tab.join());
    eq(2, pos);
  },

  'getCompletions offers several candidates'() {
    const { tab } = repl.getCompletions('Math.ma', 7);

    assert(tab.includes('max'), tab.join());
  },

  'extractDirective strips the backslash'() {
    eq('i', repl.extractDirective('\\i path'));
  },

  'historyAdd appends to the history'() {
    repl.historyAdd('first');
    repl.historyAdd('second');

    eq('first,second', repl.history.slice(-2).join());
  },

  'makeFilename builds a dotfile path per kind'() {
    assert(/\.[^/]*_history$/.test(repl.makeFilename('history')), repl.makeFilename('history'));
    assert(/\.[^/]*_config$/.test(repl.makeFilename('config')), repl.makeFilename('config'));
  },

  'colorizeJs tokenizes keywords and numbers'() {
    const kinds = repl.colorizeJs('let x = 1; // c')[2];

    assert(kinds.includes('keyword'), kinds.join());
    assert(kinds.includes('number'), kinds.join());
    assert(kinds.includes('identifier'), kinds.join());
  },
});
