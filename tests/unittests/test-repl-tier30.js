/* lib/repl.js: the editing and UX features of TODO.md Tier 30 (30.0 to 30.10) */
import { describeValue, formatTable, isValidSyntax, REPL } from '../../lib/repl.js';
import { assert, eq, tests } from '../../lib/tinytest.js';
import { popen, loadFile, open as fopen } from 'std';
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

const repl = new REPL('unit', false);

tests({
  /* 30.0: bracket aid */

  '30.0 piped input continues an open bracket on the next line'() {
    const out = session('[1,\n2]\n');

    assert(!out.includes('Uncaught'), out);
    assert(/1,\s*2/.test(out), out);
  },

  '30.0 colorizeJs finds the first bracket mismatch'() {
    eq(2, repl.colorizeJs('a(]b')[3].mismatch);
    eq(1, repl.colorizeJs('a)(]')[3].mismatch);
    eq(-1, repl.colorizeJs('f([1], "(")')[3].mismatch);
  },

  '30.0 colorizeJs finds the innermost open bracket at the cursor'() {
    eq(5, repl.colorizeJs('f(a, [1', 7)[3].enclosing);
    eq(1, repl.colorizeJs('f(a, [1]', 8)[3].enclosing);
    eq(-1, repl.colorizeJs('f(a)', 4)[3].enclosing);
  },

  '30.0 colorizeJs finds the opener of a closer before the cursor'() {
    eq(1, repl.colorizeJs('f(a)', 4)[3].match);
    eq(-1, repl.colorizeJs('f(a)', 3)[3].match);
  },

  '30.0 bracketStyles marks the mismatch to the end, underlines the opener'() {
    const [, , kinds, brackets] = repl.colorizeJs('a(]b');

    eq('mismatch,mismatch', repl.bracketStyles(kinds, brackets, 4).slice(2).join());

    const open = repl.colorizeJs('f(a', 3);

    assert(repl.bracketStyles(open[2], open[3], 3)[1].endsWith('+u'));
  },

  '30.0 continuation indents an open bracket and submits a mismatch'() {
    const r = new REPL('c', false);
    const at = cmd => ((r.cmd = cmd), (r.cursorPos = cmd.length), r.continuation());

    eq('  ', at('f('));
    eq('    ', at('f({'));
    eq('', at('`a'));
    eq(null, at('f()'));
    eq(null, at('f(]'));
    eq(null, at('"abc'));
  },

  '30.0 Alt-Backspace kills from the first mismatch to the end'() {
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

  '30.0 a bracketed paste is inserted as is'() {
    const r = new REPL('p', false);

    eq(r.pasteStart, r.commands['\x1b[200~']);
    r.pasteStart();
    for(const c of 'a\r\n\tb\x1b[201~') r.pasteChar(c);

    eq('a\n\tb', r.cmd);
    eq(null, r.pasting);
  },
  'suggestion is the rest of the newest history entry with the prefix'() {
    const r = new REPL('s', false);

    r.showColors = true;
    r.historyAdd('foo(1)');
    r.historyAdd('foo(22)');
    r.historyAdd('bar');
    r.cmd = 'fo';
    r.cursorPos = 2;
    eq('o(22)', r.suggestion());

    r.cmd = 'zz';
    r.cursorPos = 2;
    eq('', r.suggestion());

    r.cmd = 'fo';
    r.cursorPos = 1;
    eq('', r.suggestion());

    r.cmd = 'foo(22)';
    r.cursorPos = 7;
    eq('', r.suggestion());

    r.suggest = false;
    r.cmd = 'fo';
    r.cursorPos = 2;
    eq('', r.suggestion());
  },

  /* 30.1: multi-line editing */

  '30.1 Up and Down move between the lines of the buffer'() {
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

  '30.1 Home, End and ^K work on the buffer line'() {
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

  /* 30.2: Node REPL semantics */

  '30.2 a thrown value is printed as Uncaught'() {
    const out = session('throw 7\n');

    assert(out.includes('Uncaught 7'), out);
  },

  '30.2 an Error is printed as Uncaught without the REPL frame'() {
    const out = session('throw new Error("x")\n');

    assert(out.includes('Uncaught Error: x'), out);
    assert(!out.includes('<input>'), out);
  },

  '30.2 _ holds the last result'() {
    const out = session('1+1\n_\n');

    eq(2, (out.match(/t> 2\n/g) ?? []).length, out);
  },

  '30.2 assigning _ disables it'() {
    const out = session('_ = 5\n3\n_\n');

    assert(out.includes('Expression assignment to _ now disabled.'), out);
    assert(/t> 3\n[^]*t> 5\n/.test(out), out);
  },

  '30.2 _error holds the last error'() {
    const out = session('foo\n_error.message\n');

    assert(out.includes("Uncaught ReferenceError: 'foo' is not defined") || out.includes('Uncaught ReferenceError: foo is not defined'), out);
    assert(/t> '.*foo.* is not defined'/.test(out), out);
  },

  '30.2 \\break drops the unfinished lines'() {
    const out = session('(1,\n\\\\break\n40+2\n');

    assert(!out.includes('Uncaught'), out);
    assert(out.includes('42'), out);
  },

  '30.2 an open quote does not continue, it is a SyntaxError'() {
    const out = session('"abc\n7*6\n');

    assert(out.includes('Uncaught SyntaxError'), out);
    assert(out.includes('42'), out);
  },

  '30.2 a SyntaxError is printed as an error'() {
    const out = session('let q = 1; let q = 2\n');

    assert(/redeclaration|redefinition/.test(out), out);
    assert(!out.includes('SyntaxError {'), out);
  },

  '30.2 an await command prints the value, not { value }'() {
    const out = session('await Promise.resolve(42)\n');

    assert(out.includes('42'), out);
    assert(!out.includes('value'), out);
  },

  /* 30.4: history suggestion */

  '30.4 a multi-line history entry is not suggested'() {
    const r = new REPL('s', false);

    r.showColors = true;
    r.historyAdd('[1,\n2]');
    r.cmd = '[1';
    r.cursorPos = 2;
    eq('', r.suggestion());
  },

  '30.4 Right and ^E accept the suggestion at the end of the buffer'() {
    const r = new REPL('s', false);

    r.showColors = true;
    r.historyAdd('console.log(1)');
    r.cmd = 'con';
    r.cursorPos = 3;
    r.forwardChar();
    eq('console.log(1)', r.cmd);
    eq(14, r.cursorPos);

    r.cmd = 'con';
    r.cursorPos = 3;
    r.endOfLine();
    eq('console.log(1)', r.cmd);

    r.cmd = 'con';
    r.cursorPos = 1;
    r.forwardChar();
    eq('con', r.cmd);
    eq(2, r.cursorPos);
  },

  '30.4 render draws the suggestion in grey after the buffer'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('s', false);
    const f = fopen(file, 'w');

    r.showColors = true;
    r.out = f;
    r.historyAdd('hello(1)');
    r.cmd = 'hel';
    r.cursorPos = 3;
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(out.includes('lo(1)'), JSON.stringify(out));
    assert(out.indexOf('lo(1)') > out.indexOf('hel'), JSON.stringify(out));
    assert(!out.includes('hello(1)hel'), JSON.stringify(out));
  },
  'completionPreview is the rest of the only completion'() {
    const r = new REPL('c', false);

    r.showColors = true;
    r.cmd = 'Obj';
    r.cursorPos = 3;
    eq('ect', r.completionPreview());

    r.cmd = 'Math.fl';
    r.cursorPos = 7;
    eq('oor', r.completionPreview());

    r.cmd = 'Math.m';
    r.cursorPos = 6;
    eq('', r.completionPreview());

    r.cmd = 'Obj';
    r.cursorPos = 1;
    eq('', r.completionPreview());

    r.cmd = 'Obj ';
    r.cursorPos = 4;
    eq('', r.completionPreview());
  },

  '30.4 the history suggestion wins over the completion preview'() {
    const r = new REPL('c', false);

    r.showColors = true;
    r.historyAdd('Object.keys(x)');
    r.cmd = 'Obj';
    r.cursorPos = 3;
    eq('ect.keys(x)', r.ghost());
  },

  /* 30.5: completion preview and list */

  '30.5 Right accepts the completion preview'() {
    const r = new REPL('c', false);

    r.showColors = true;
    r.cmd = 'Obj';
    r.cursorPos = 3;
    r.forwardChar();
    eq('Object', r.cmd);
  },

  '30.5 completionLabel shows arity and kind without calling getters'() {
    const r = new REPL('c', false);
    let called = false;
    const ctx = {
      f(a, b) {},
      g() {},
      o: {},
      n: 1,
      get x() {
        called = true;
        return 1;
      },
    };

    eq('f(2)', r.completionLabel(ctx, 'f'));
    eq('g()', r.completionLabel(ctx, 'g'));
    eq('o.', r.completionLabel(ctx, 'o'));
    eq('n', r.completionLabel(ctx, 'n'));
    eq('x', r.completionLabel(ctx, 'x'));
    eq(false, called);
    eq('toString()', r.completionLabel(ctx, 'toString'));
  },
  'previewText shows the value of a literal or a property chain'() {
    const r = new REPL('v', false);
    const at = cmd => ((r.cmd = cmd), (r.cursorPos = cmd.length), r.previewText());

    r.showColors = true;
    r.columns = 80;
    eq('16', at('0x10'));
    eq('3.141592653589793', at('Math.PI'));
    eq('1000', at('1_000'));
    eq('', at('5'));
    eq("'abc'", at('"abc"'));
    eq('', at("'abc'"));
    eq('', at('true'));
    eq('', at('Math.nope'));
    eq('', at('1 + 1'));
    eq('', at(''));
  },

  /* 30.6: live result preview */

  '30.6 previewText never calls a getter'() {
    const r = new REPL('v', false);
    let called = false;

    globalThis.previewProbe = {
      get g() {
        called = true;
        return 1;
      },
      v: 7,
    };
    r.showColors = true;
    r.columns = 80;

    const at = cmd => ((r.cmd = cmd), (r.cursorPos = cmd.length), r.previewText());

    eq('7', at('previewProbe.v'));
    eq('', at('previewProbe.g'));
    eq(false, called);
    delete globalThis.previewProbe;
  },

  '30.6 previewText is off while the cursor is inside, on several lines and with preview = false'() {
    const r = new REPL('v', false);

    r.showColors = true;
    r.columns = 80;
    r.cmd = '0x10';
    r.cursorPos = 2;
    eq('', r.previewText());

    r.cmd = '0x10\n';
    r.cursorPos = 5;
    eq('', r.previewText());

    r.cmd = '0x10';
    r.cursorPos = 4;
    r.preview = false;
    eq('', r.previewText());
  },

  '30.6 a long preview is cut to the width'() {
    const r = new REPL('v', false);

    r.showColors = true;
    r.columns = 20;
    r.cmd = 'Object';
    r.cursorPos = 6;

    const text = r.previewText();

    assert(text.length <= 16, text);
  },

  '30.6 render draws the preview on the row below and returns to the cursor'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('v', false);
    const f = fopen(file, 'w');

    r.showColors = true;
    r.columns = 80;
    r.out = f;
    r.cmd = '0x10';
    r.cursorPos = 4;
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(/\n\r[^\n]*16[^\n]*\x1b\[1A\r/.test(out), JSON.stringify(out));
    eq(0, r.renderRow);
  },
  'isValidSyntax tells a parseable input from a broken one'() {
    eq(true, isValidSyntax('1 + 1'));
    eq(true, isValidSyntax('let a = 1; a'));
    eq(true, isValidSyntax('{a: 1}'));
    eq(true, isValidSyntax('await Promise.resolve(1)'));
    eq(true, isValidSyntax('for(const x of [1]) x'));
    eq(false, isValidSyntax('1 +'));
    eq(false, isValidSyntax('f(]'));
    eq(false, isValidSyntax('let let = 1'));
    eq(false, isValidSyntax('a b'));
  },

  /* 30.7: red prompt on a syntax error */

  '30.7 isValidSyntax runs nothing'() {
    globalThis.syntaxProbe = 0;
    isValidSyntax('syntaxProbe = 1');
    eq(0, globalThis.syntaxProbe);
    delete globalThis.syntaxProbe;
  },

  '30.7 render paints the prompt red while the input cannot parse'() {
    const render = cmd => {
      const file = '/tmp/test-repl-render-' + Date.now();
      const r = new REPL('x', false);
      const f = fopen(file, 'w');

      r.showColors = true;
      r.columns = 80;
      r.out = f;
      r.prompt = 'P> ';
      r.cmd = cmd;
      r.cursorPos = cmd.length;
      r.render();
      f.close();

      const out = loadFile(file);

      remove(file);
      return out;
    };
    const red = '\x1b[31mP> ';

    /* without a tty on stdout the color table is empty, the prompt stays plain */
    if(!render('1 +').includes(red)) return assert(render('1 +').includes('P> '));

    assert(render('1 +').includes(red));
    assert(!render('1 + 1').includes(red));
    assert(!render('f(').includes(red));
    assert(!render('\\h').includes(red));
    assert(!render('foo?').includes(red));
  },

  '30.7 the syntax check can be turned off'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('x', false);
    const f = fopen(file, 'w');

    r.showColors = true;
    r.syntaxCheck = false;
    r.columns = 80;
    r.out = f;
    r.prompt = 'P> ';
    r.cmd = '1 +';
    r.cursorPos = 3;
    r.render();
    f.close();

    assert(!loadFile(file).includes('\x1b[31mP> '));
    remove(file);
  },
  'describeValue names type, class and prototype chain'() {
    const lines = describeValue([1, 2]);

    eq('type    object', lines[0]);
    eq('class   Array', lines[1]);
    eq('proto   Array.prototype -> Object.prototype', lines[2]);
    assert(lines.includes('length  2'), lines.join('\n'));
  },

  /* 30.8: introspection */

  '30.8 describeValue shows the source and arity of a function'() {
    const lines = describeValue(function add(a, b) { return a + b; });

    eq('type    function', lines[0]);
    assert(lines.includes('name    "add"'), lines.join('\n'));
    assert(lines.includes('args    2'), lines.join('\n'));
    assert(lines.some(l => /^  function add\(a, b\)/.test(l)), lines.join('\n'));
  },

  '30.8 describeValue handles primitives and null'() {
    eq('type    null', describeValue(null).join());
    eq('type    number,value   5', describeValue(5).join());
    eq('type    string,length  3', describeValue('abc').join());
    eq('type    undefined', describeValue(undefined).join());
  },

  '30.8 describeValue follows a class chain'() {
    class A {}
    class B extends A {}

    eq('proto   B.prototype -> A.prototype -> Object.prototype', describeValue(new B())[2]);
    eq('proto   null', describeValue(Object.create(null))[2]);
  },

  '30.8 a trailing ? describes the value'() {
    const out = session('[1,2]?\n');

    assert(out.includes('class   Array'), out);
    assert(out.includes('proto   Array.prototype -> Object.prototype'), out);
    assert(!out.includes('Uncaught'), out);
  },

  '30.8 \\type describes the value'() {
    const out = session('\\\\type Math.max\n');

    assert(out.includes('type    function'), out);
    assert(out.includes('args    2'), out);
  },

  '30.8 a failing ? prints the error'() {
    const out = session('nope?\n');

    assert(out.includes('Uncaught ReferenceError'), out);
  },
  'formatTable draws a box table of objects'() {
    const out = formatTable([{ a: 1 }, { a: 2, b: 'x' }]);

    eq(['┌─────────┬───┬─────┐', '│ (index) │ a │ b   │', '├─────────┼───┼─────┤', '│ 0       │ 1 │     │', "│ 1       │ 2 │ 'x' │", '└─────────┴───┴─────┘'].join('\n'), out);
  },

  /* 30.9: helpers and \time */

  '30.9 formatTable puts a primitive row in a Values column'() {
    const out = formatTable({ x: 1, y: { k: 2 } });

    assert(out.includes('Values'), out);
    assert(/│ x\s+│\s+│ 1\s+│/.test(out), out);
    assert(/│ y\s+│ 2\s+│\s+│/.test(out), out);
  },

  '30.9 formatTable takes the columns to show'() {
    const out = formatTable([{ a: 1, b: 2 }], ['b']);

    assert(out.includes(' b '), out);
    assert(!out.includes(' a '), out);
  },

  '30.9 formatTable pads wide characters by their cells'() {
    const lines = formatTable([{ a: '漢' }]).split('\n');

    eq(1, new Set(lines.map(l => l.length - (l.includes('漢') ? 1 : 0))).size);
  },

  '30.9 the console helpers are defined once the REPL starts'() {
    const r = new REPL('h', false);

    r.defineHelpers();
    eq('a,b', keys({ a: 1, b: 2 }).join());
    eq('1,2', values({ a: 1, b: 2 }).join());
    eq('function', typeof copy);
    eq('function', typeof table);
  },

  '30.9 copy puts the text in the yank buffer'() {
    const r = new REPL('h', false);

    r.defineHelpers();
    copy('hello');
    r.cmd = '';
    r.cursorPos = 0;
    r.yank();
    eq('hello', r.cmd);

    copy({ a: 1 });
    r.cmd = '';
    r.cursorPos = 0;
    r.yank();
    eq('{\n  "a": 1\n}', r.cmd);
  },

  '30.9 a helper does not replace an existing global'() {
    globalThis.values = 'mine';

    const r = new REPL('h', false);

    r.defineHelpers();
    eq('mine', globalThis.values);
    delete globalThis.values;
    r.defineHelpers();
    eq('function', typeof values);
  },

  '30.9 \\time prints the result and the time'() {
    const out = session('\\\\time 6*7\n');

    assert(out.includes('42'), out);
    assert(/time: \d+ ms/.test(out), out);
  },

  '30.9 \\time times an await'() {
    const out = session('\\\\time await new Promise(r => setTimeout(r, 30))\n');

    assert(/time: (\d+) ms/.test(out) && +/time: (\d+) ms/.exec(out)[1] >= 25, out);
  },

  '30.9 \\time prints the time after an error'() {
    const out = session('\\\\time nope\n');

    assert(out.includes('Uncaught ReferenceError'), out);
    assert(/time: \d+ ms/.test(out), out);
  },

  '30.9 the time is not printed for the next input'() {
    const out = session('\\\\time 1\n2\n');

    eq(1, (out.match(/time: \d+ ms/g) ?? []).length, out);
  },

  '30.9 table prints a table in a session'() {
    const out = session('table([{a:1}])\n');

    assert(out.includes('│ (index) │ a │'), out);
  },

  /* 30.10: auto-close, Alt-Enter, numbered results, vi mode, wide characters */

  '30.10 auto-close inserts the closer and types over it'() {
    const r = new REPL('a', false);
    const type = text => {
      for(const c of text) r.insertTyped(c);
    };

    r.autoClose = true;
    type('f(');
    eq('f()', r.cmd);
    eq(2, r.cursorPos);
    type(')');
    eq('f()', r.cmd);
    eq(3, r.cursorPos);

    r.cmd = '';
    r.cursorPos = 0;
    type('[{"a');
    eq('[{"a"}]', r.cmd);
    type('"}]');
    eq('[{"a"}]', r.cmd);
    eq(7, r.cursorPos);
  },

  '30.10 auto-close is off by default and skips a quote after a word'() {
    const r = new REPL('a', false);

    r.insertTyped('(');
    eq('(', r.cmd);

    const q = new REPL('a', false);

    q.autoClose = true;
    q.cmd = "it";
    q.cursorPos = 2;
    q.insertTyped("'");
    eq("it'", q.cmd);
  },

  '30.10 auto-close does not wrap an opener before a word'() {
    const r = new REPL('a', false);

    r.autoClose = true;
    r.cmd = 'abc';
    r.cursorPos = 0;
    r.insertTyped('(');
    eq('(abc', r.cmd);
  },

  '30.10 Backspace between an auto-closed pair removes both'() {
    const r = new REPL('a', false);

    r.autoClose = true;
    r.cmd = 'f()';
    r.cursorPos = 2;
    r.backwardDeleteChar();
    eq('f', r.cmd);
    eq(1, r.cursorPos);

    r.cmd = 'f(1)';
    r.cursorPos = 3;
    r.backwardDeleteChar();
    eq('f()', r.cmd);
  },

  '30.10 a closer at the start of an indented line steps back'() {
    const r = new REPL('a', false);

    r.cmd = 'f(\n  ';
    r.cursorPos = 5;
    r.insertTyped(')');
    eq('f(\n)', r.cmd);
    eq(4, r.cursorPos);

    r.cmd = '   ';
    r.cursorPos = 3;
    r.insertTyped(')');
    eq('   )', r.cmd);
  },

  '30.10 Alt-Enter inserts a newline and never submits'() {
    const r = new REPL('n', false);

    eq(r.insertNewline, r.commands['\x1b\r']);
    eq(r.insertNewline, r.commands['\x1b\n']);

    r.cmd = 'f(1)';
    r.cursorPos = 4;
    r.insertNewline();
    eq('f(1)\n', r.cmd);

    r.cmd = 'f(';
    r.cursorPos = 2;
    r.insertNewline();
    eq('f(\n  ', r.cmd);
  },

  '30.10 a result is kept as $1, $2'() {
    const out = session('40+2\n$1+1\n$2\n');

    assert(out.includes('t> 42'), out);
    assert(out.includes('t> 43'), out);
    eq(2, (out.match(/t> 43/g) ?? []).length, out);
  },

  '30.10 undefined takes no number'() {
    const out = session('undefined\n7\n$1\n');

    /* $1 is the 7, not the undefined: 7 is printed twice */
    eq(2, (out.match(/t> 7\n/g) ?? []).length, out);
    assert(!out.includes('ReferenceError'), out);
  },

  '30.10 vi: Esc enters normal mode, the motions move'() {
    const r = new REPL('v', false);

    r.viMode = true;
    r.cmd = 'foo bar baz';
    r.cursorPos = 11;
    eq(true, r.viKey('\x1b'));
    eq(true, r.viNormal);
    eq(10, r.cursorPos);

    r.viKey('0');
    eq(0, r.cursorPos);
    r.viKey('w');
    eq(4, r.cursorPos);
    r.viKey('b');
    eq(0, r.cursorPos);
    r.viKey('$');
    eq(10, r.cursorPos);
    r.viKey('h');
    eq(9, r.cursorPos);
    r.viKey('l');
    eq(10, r.cursorPos);
    r.viKey('l');
    eq(10, r.cursorPos);
  },

  '30.10 vi: insert mode passes keys through'() {
    const r = new REPL('v', false);

    r.viMode = true;
    eq(false, r.viKey('a'));
    eq(false, r.viKey('\x1b[A'));
    eq(false, r.viKey('\x1b\r'));
    eq(false, r.viNormal);
  },

  '30.10 vi: Esc and a command key in one go'() {
    const r = new REPL('v', false);

    r.viMode = true;
    r.cmd = 'abc';
    r.cursorPos = 3;
    r.viKey('\x1b0');
    eq(true, r.viNormal);
    eq(0, r.cursorPos);
  },

  '30.10 vi: x, X, D, dd, dw, cw'() {
    const r = new REPL('v', false);
    const at = (cmd, pos) => ((r.cmd = cmd), (r.cursorPos = pos), (r.viNormal = true), (r.viPending = ''));

    r.viMode = true;

    at('abc', 1);
    r.viKey('x');
    eq('ac', r.cmd);

    at('abc', 1);
    r.viKey('X');
    eq('bc', r.cmd);

    at('foo bar', 2);
    r.viKey('D');
    eq('fo', r.cmd);

    at('foo bar', 1);
    r.viKey('d');
    r.viKey('d');
    eq('', r.cmd);

    at('foo bar', 0);
    r.viKey('d');
    r.viKey('w');
    eq('bar', r.cmd);

    at('foo bar', 0);
    r.viKey('c');
    r.viKey('w');
    eq(' bar', r.cmd);
    eq(false, r.viNormal);
  },

  '30.10 vi: i, a, A, I enter insert mode at the right place'() {
    const r = new REPL('v', false);
    const at = pos => ((r.cmd = 'abc'), (r.cursorPos = pos), (r.viNormal = true), r.viKey);

    r.viMode = true;
    at(1);
    r.viKey('i');
    eq(1, r.cursorPos);
    eq(false, r.viNormal);

    at(1);
    r.viKey('a');
    eq(2, r.cursorPos);

    at(1);
    r.viKey('A');
    eq(3, r.cursorPos);

    at(1);
    r.viKey('I');
    eq(0, r.cursorPos);
  },

  '30.10 vi: p pastes what d killed'() {
    const r = new REPL('v', false);

    r.viMode = true;
    r.cmd = 'foo bar';
    r.cursorPos = 0;
    r.viNormal = true;
    r.viKey('d');
    r.viKey('w');
    r.cursorPos = 2;
    r.viKey('p');
    eq('barfoo ', r.cmd);
  },

  '30.10 vi: control keys and Enter keep their meaning in normal mode'() {
    const r = new REPL('v', false);

    r.viMode = true;
    r.viNormal = true;
    eq(false, r.viKey('\r'));
    eq(false, r.viKey('\x01'));
    eq(false, r.viKey('\x1b[A'));
  },

  '30.10 vi: a new prompt starts in insert mode'() {
    const r = new REPL('v', false);

    r.viMode = true;
    r.viNormal = true;
    r.readlineStart('', () => {});
    eq(false, r.viNormal);
  },

  '30.10 the \\vi and \\autoclose directives toggle the modes'() {
    const out = session('\\\\vi\n\\\\autoclose\n\\\\vi\n');

    assert(out.includes('vi mode on'), out);
    assert(out.includes('autoclose on'), out);
    assert(out.includes('vi mode off'), out);
  },

  '30.10 a wide character takes two cells in the cursor column'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('w', false);
    const f = fopen(file, 'w');

    r.showColors = false;
    r.columns = 80;
    r.out = f;
    r.prompt = '> ';
    r.cmd = '漢字';
    r.cursorPos = 2;
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(out.endsWith('\r\x1b[6C'), JSON.stringify(out));
  },

  '30.10 a wide character wraps by cells'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('w', false);
    const f = fopen(file, 'w');

    r.showColors = false;
    r.columns = 10;
    r.out = f;
    r.prompt = '> ';
    r.cmd = '漢漢漢漢漢';
    r.cursorPos = 5;
    r.render();
    f.close();
    remove(file);
    eq(1, r.renderRow);
  },

  /* leftovers of 30.0 to 30.3 */

  '30.x ^C goes to a controlC set on the REPL after construction'() {
    const r = new REPL('c', false);
    const calls = [];

    r.out = null;
    r.controlC = function(...args) {
      calls.push(this === r);
    };

    r.handleKey('\x03');
    eq('true', calls.join());
    eq(r.controlC, r.thisFun);
  },

  '30.x an override that calls the original keeps its behavior'() {
    const r = new REPL('c', false);
    const { controlC } = r;
    let seen = 0;

    r.out = null;
    r.cmd = 'abc';
    r.cursorPos = 3;
    r.controlC = function(...args) {
      seen++;

      return controlC.call(this, ...args);
    };
    r.moveBelow = () => {};
    r.cmdReadlineStart = () => {};

    r.handleKey('\x03');
    eq(1, seen);
    eq('', r.cmd);
  },

  '30.x other keys still use the key table'() {
    const r = new REPL('c', false);

    r.out = null;
    r.cmd = 'ab';
    r.cursorPos = 2;
    r.handleKey('\x01');
    eq(0, r.cursorPos);
  },

  '30.x a buffer taller than the screen draws the lines around the cursor'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('t', false);
    const f = fopen(file, 'w');

    r.showColors = false;
    r.columns = 20;
    r.lines = 4;
    r.out = f;
    r.prompt = '> ';
    r.cmd = ['l0', 'l1', 'l2', 'l3', 'l4', 'l5', 'l6', 'l7'].join('\n');
    r.cursorPos = r.cmd.length;
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    eq(3, (out.match(/\n/g) ?? []).length, JSON.stringify(out));
    assert(out.includes('l7') && out.includes('l4') && !out.includes('l3'), JSON.stringify(out));
    eq(3, r.renderRow);
  },

  '30.x the window follows the cursor up'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('t', false);
    const f = fopen(file, 'w');

    r.showColors = false;
    r.columns = 20;
    r.lines = 3;
    r.out = f;
    r.prompt = '> ';
    r.cmd = ['l0', 'l1', 'l2', 'l3', 'l4', 'l5'].join('\n');
    r.cursorPos = 0;
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(out.includes('l0') && out.includes('l2') && !out.includes('l3'), JSON.stringify(out));
    eq(0, r.renderRow);
  },

  '30.x a buffer that fits is drawn whole, and so is one without a known height'() {
    for(const lines of [10, undefined]) {
      const file = '/tmp/test-repl-render-' + Date.now();
      const r = new REPL('t', false);
      const f = fopen(file, 'w');

      r.showColors = false;
      r.columns = 20;
      r.lines = lines;
      r.out = f;
      r.prompt = '> ';
      r.cmd = 'a\nb\nc\nd\ne';
      r.cursorPos = r.cmd.length;
      r.render();
      f.close();

      const out = loadFile(file);

      remove(file);
      eq(4, (out.match(/\n/g) ?? []).length, JSON.stringify(out));
    }
  },

  '30.x the redraw of a tall buffer starts at the top of what was drawn'() {
    const file = '/tmp/test-repl-render-' + Date.now();
    const r = new REPL('t', false);
    const f = fopen(file, 'w');

    r.showColors = false;
    r.columns = 20;
    r.lines = 3;
    r.out = f;
    r.prompt = '> ';
    r.cmd = 'a\nb\nc\nd\ne';
    r.cursorPos = r.cmd.length;
    r.render();
    r.render();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(out.split('\r\x1b[J')[2].length > 0 && out.includes('\x1b[2A\r\x1b[J'), JSON.stringify(out));
  },

  '30.x accepting a ^R match with a key clears every row of the wrapped search line'() {
    const file = '/tmp/test-repl-search-' + Date.now();
    const r = new REPL('s', false);
    const f = fopen(file, 'w');

    r.piped = false;
    r.showColors = false;
    r.columns = 20;
    r.out = f;
    r.historyAdd('x'.repeat(45));
    r.readlineStart('', () => {});
    r.cmd = 'xx';
    r.cursorPos = 2;
    r.reverseSearch();
    f.flush();

    const painted = r.renderRow;

    eq(true, r.searchRowsBelow > 0 || painted > 0);

    const before = loadFile(file).length;

    r.handleKey('\x14');
    f.close();

    const after = loadFile(file).slice(before);

    remove(file);
    /* the first thing written moves up to the first row of the search line and clears from there */
    assert(painted > 0 ? after.startsWith(`\x1b[${painted}A\r\x1b[J`) : after.startsWith('\r\x1b[J'), JSON.stringify(after));
    eq(0, r.renderRow >= 0 ? 0 : 1);
  },

  '30.x Enter in a search moves below the whole painted line first'() {
    const file = '/tmp/test-repl-search-' + Date.now();
    const r = new REPL('s', false);
    const f = fopen(file, 'w');

    r.piped = false;
    r.showColors = false;
    r.columns = 20;
    r.out = f;
    r.historyAdd('x'.repeat(45));
    r.readlineStart('', () => {});
    r.cmd = 'xx';
    r.cursorPos = 2;
    r.reverseSearch();
    f.flush();

    const below = r.searchRowsBelow;
    const before = loadFile(file).length;

    assert(below > 0, 'the search line wraps: ' + below);

    r.acceptLine();
    f.close();

    const after = loadFile(file).slice(before);

    remove(file);
    eq(`\x1b[${below}B\n`, after);
    eq(0, r.searchRowsBelow);
    eq(0, r.renderRow);

    r.handleKey('\x14');
  },

  '30.x Enter outside a search writes only the newline'() {
    const file = '/tmp/test-repl-search-' + Date.now();
    const r = new REPL('s', false);
    const f = fopen(file, 'w');

    r.piped = false;
    r.showColors = false;
    r.out = f;
    r.cmd = '1+1';
    r.cursorPos = 3;
    r.acceptLine();
    f.close();

    const out = loadFile(file);

    remove(file);
    assert(out.endsWith('\n') && !out.includes('B'), JSON.stringify(out));
  },
});
