/* lib/repl.js: the parts of REPL that work without a terminal */
import { REPL } from '../../lib/repl.js';
import { assert, eq, tests } from '../../lib/tinytest.js';
import { popen, loadFile } from 'std';
import { readlink, remove } from 'os';

const fixture = import.meta.dirname + '/../fixtures/repl/piped.js';
const qjsm = readlink('/proc/self/exe')[0];

/* output of a piped REPL session that reads input */
function session(input) {
  const f = popen(`printf '${input}' | '${qjsm}' '${fixture}'`, 'r');
  const out = f.readAsString();

  f.close();
  return out;
}

/* the constructor writes nothing, run() prints the first prompt */
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

  'colorizeJs keeps a multi-line template literal valid'() {
    const [state, level, kinds] = repl.colorizeJs('`a\nb`');

    eq('', state);
    eq(0, level);
    assert(!kinds.includes('error'), kinds.join());
  },

  'colorizeJs counts braces inside ${} as code'() {
    eq('', repl.colorizeJs('`${ {a: 1}.a }`')[0]);
    eq('`$', repl.colorizeJs('`x${ f(')[0].slice(0, 2));
    eq('`', repl.colorizeJs('`x${ 1 } y')[0]);
  },

  'historySave keeps the lines of a multi-line entry'() {
    const file = '/tmp/test-repl-history-' + Date.now();
    const r = new REPL('h', false);

    r.historyAdd('[1, // one\n2]');
    r.historySave(file);
    r.historyClear();
    r.historyLoad(file);

    eq('[1, // one\n2]', r.history[r.history.length - 1]);
    remove(file);
  },

  'a thrown value is printed as Uncaught'() {
    const out = session('throw 7\n');

    assert(out.includes('Uncaught 7'), out);
  },

  'an Error is printed as Uncaught without the REPL frame'() {
    const out = session('throw new Error("x")\n');

    assert(out.includes('Uncaught Error: x'), out);
    assert(!out.includes('<input>'), out);
  },

  '_ holds the last result'() {
    const out = session('1+1\n_\n');

    eq(2, (out.match(/t> 2\n/g) ?? []).length, out);
  },

  'assigning _ disables it'() {
    const out = session('_ = 5\n3\n_\n');

    assert(out.includes('Expression assignment to _ now disabled.'), out);
    assert(/t> 3\n[^]*t> 5\n/.test(out), out);
  },

  '_error holds the last error'() {
    const out = session('foo\n_error.message\n');

    assert(out.includes("Uncaught ReferenceError: 'foo' is not defined") || out.includes('Uncaught ReferenceError: foo is not defined'), out);
    assert(/t> '.*foo.* is not defined'/.test(out), out);
  },

  'piped input continues an open bracket on the next line'() {
    const out = session('[1,\n2]\n');

    assert(!out.includes('Uncaught'), out);
    assert(/1,\s*2/.test(out), out);
  },

  '\\break drops the unfinished lines'() {
    const out = session('(1,\n\\\\break\n40+2\n');

    assert(!out.includes('Uncaught'), out);
    assert(out.includes('42'), out);
  },

  'an open quote does not continue, it is a SyntaxError'() {
    const out = session('"abc\n7*6\n');

    assert(out.includes('Uncaught SyntaxError'), out);
    assert(out.includes('42'), out);
  },

  'a SyntaxError is printed as an error'() {
    const out = session('let q = 1; let q = 2\n');

    assert(/redeclaration|redefinition/.test(out), out);
    assert(!out.includes('SyntaxError {'), out);
  },

  'an await command prints the value, not { value }'() {
    const out = session('await Promise.resolve(42)\n');

    assert(out.includes('42'), out);
    assert(!out.includes('value'), out);
  },

  'colorizeJs finds the first bracket mismatch'() {
    eq(2, repl.colorizeJs('a(]b')[3].mismatch);
    eq(1, repl.colorizeJs('a)(]')[3].mismatch);
    eq(-1, repl.colorizeJs('f([1], "(")')[3].mismatch);
  },

  'colorizeJs finds the innermost open bracket at the cursor'() {
    eq(5, repl.colorizeJs('f(a, [1', 7)[3].enclosing);
    eq(1, repl.colorizeJs('f(a, [1]', 8)[3].enclosing);
    eq(-1, repl.colorizeJs('f(a)', 4)[3].enclosing);
  },

  'colorizeJs finds the opener of a closer before the cursor'() {
    eq(1, repl.colorizeJs('f(a)', 4)[3].match);
    eq(-1, repl.colorizeJs('f(a)', 3)[3].match);
  },

  'bracketStyles marks the mismatch to the end, underlines the opener'() {
    const [, , kinds, brackets] = repl.colorizeJs('a(]b');

    eq('mismatch,mismatch', repl.bracketStyles(kinds, brackets, 4).slice(2).join());

    const open = repl.colorizeJs('f(a', 3);

    assert(repl.bracketStyles(open[2], open[3], 3)[1].endsWith('+u'));
  },

  'continuation indents an open bracket and submits a mismatch'() {
    const r = new REPL('c', false);
    const at = cmd => ((r.cmd = cmd), (r.cursorPos = cmd.length), r.continuation());

    eq('  ', at('f('));
    eq('    ', at('f({'));
    eq('', at('`a'));
    eq(null, at('f()'));
    eq(null, at('f(]'));
    eq(null, at('"abc'));
  },

  'Up and Down move between the lines of the buffer'() {
    const r = new REPL('l', false);

    r.cmd = 'abc\nde';
    r.cursorPos = 6;
    r.historyPrevious();
    eq(2, r.cursorPos);
    r.historyNext();
    eq(6, r.cursorPos);
    r.cursorPos = 3;
    r.historyNext();
    eq(6, r.cursorPos);
  },

  'Home, End and ^K work on the buffer line'() {
    const r = new REPL('k', false);

    r.cmd = 'ab\ncd';
    r.cursorPos = 4;
    r.beginningOfLine();
    eq(3, r.cursorPos);
    r.endOfLine();
    eq(5, r.cursorPos);
    r.cursorPos = 2;
    r.killLine();
    eq('abcd', r.cmd);
  },

  'Alt-Backspace kills from the first mismatch to the end'() {
    const r = new REPL('m', false);

    r.cmd = 'f(1]2)';
    r.cursorPos = 6;
    r.backwardKillMismatch();
    eq('f(1', r.cmd);
    eq(3, r.cursorPos);

    r.cmd = 'foo bar';
    r.cursorPos = 7;
    r.backwardKillMismatch();
    eq('foo ', r.cmd);
  },

  'a bracketed paste is inserted as is'() {
    const r = new REPL('p', false);

    eq(r.pasteStart, r.commands['\x1b[200~']);
    r.pasteStart();
    for(const c of 'a\r\n\tb\x1b[201~') r.pasteChar(c);

    eq('a\n\tb', r.cmd);
    eq(null, r.pasting);
  },
});
