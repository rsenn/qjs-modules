// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* readline with `terminal: true`: keystrokes in, the editing and redraw sequences out, as in Node */
import * as readline from 'readline';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* node, bun and deno need a Readable as input; qjsm takes any async iterable */
let Readable;

try {
  ({ Readable } = await import('node:stream'));
} catch {}

class PushInput {
  q = [];
  w = null;
  done = false;

  push(s) {
    this.q.push(s);
    this.w?.();
  }

  destroy() {
    this.done = true;
    this.w?.();
  }

  [Symbol.asyncIterator]() {
    return {
      next: async () => {
        while(!this.q.length && !this.done) await new Promise(r => (this.w = r));

        return this.q.length ? { value: this.q.shift(), done: false } : { value: undefined, done: true };
      },
      return: async () => ({ done: true }),
    };
  }
}

const newInput = () => (Readable?.from ? new Readable({ read() {} }) : new PushInput());
const tick = (ms = 8) => new Promise(r => setTimeout(r, ms));
const esc = s => s.replace(/\x1b/g, '\\e').replace(/\r/g, '\\r').replace(/\n/g, '\\n').replace(/\t/g, '\\t');
const completer = list => l => [list.filter(c => c.startsWith(l)), l];

/* name: [keys, options, columns, setup] */
const SCENARIOS = {
  typing: [['a', 'b', 'c', '\r']],
  'prompt and enter': [['h', 'i', '\r'], { prompt: '> ' }, undefined, rl => rl.prompt()],
  'insert in the middle': [['a', 'b', 'c', '\x1b[D', 'X', '\x1b[C', 'Y']],
  'backspace and delete': [['a', 'b', 'c', '\x7f', '\x1b[D', '\x7f', '\x1b[3~']],
  'home and end': [['a', 'b', 'c', '\x01', 'X', '\x05', 'Y', '\x1b[H', 'Z', '\x1b[F', 'W']],
  'kill to end and start': [['a', 'b', 'c', 'd', '\x1b[D', '\x1b[D', '\x0b', 'x', '\x15', 'y', 'z', '\x17']],
  'word moves and deletes': [['h', 'e', 'l', ' ', 'w', 'o', ' ', 'x', '\x1bb', '\x1bb', '\x1bf', '\x1b[1;5D', '\x1b[1;5C', '\x1bd', '\x17']],
  'history with arrows': [['a', '\r', 'b', '\r', '\x1b[A', '\x1b[A', '\x1b[B', '\x1b[B', '\x1b[B']],
  'history with ctrl-p and ctrl-n': [['one', '\r', 'two', '\r', '\x10', 'X', '\x0e', '\x10', '\x10', '\x10']],
  'removeHistoryDuplicates': [['a\r', 'a\r', 'b\r', 'a\r', '\x1b[A', '\x1b[A', '\x1b[A'], { removeHistoryDuplicates: true }],
  'ctrl-d deletes, closes on an empty line': [['a', 'b', '\x04', '\x1b[D', '\x04']],
  'ctrl-d on an empty line': [['\x04']],
  'ctrl-c closes without a SIGINT listener': [['a', '\x03']],
  'ctrl-l clears the screen': [['a', 'b', '\x0c']],
  'ctrl-u then ctrl-y yanks': [['a', 'b', '\x15', '\x19']],
  'tab without a completer inserts a tab': [['a', '\t']],
  'completer: one match': [['f', 'o', '\t'], { completer: completer(['foobar']) }],
  'completer: common prefix, then the list': [['f', '\t', '\t'], { completer: completer(['foo', 'fob', 'bar']) }],
  'completer: callback form': [['al', '\t', '\t'], { completer: (l, cb) => cb(null, [['alpha', 'alps'], l]) }],
  'completer: list in columns': [['\t'], { completer: completer(['a1', 'a2', 'a3', 'b1']) }, 30],
  'wrapping at the right edge': [[...'abcdefghijkl', '\x7f', '\r'], { prompt: '> ' }, 10, rl => rl.prompt()],
  'wide characters': [['a', '漢', '字', '\x1b[D', 'X']],
  'emoji take two units': [['é😀x', '\x1b[D', '\x1b[D', '\x1b[D']],
  'several keys in one chunk': [['abc', 'def\rgh']],
  'a pasted newline': [['x\ny\r']],
  'CRLF is one enter': [['x\r\ny\r\n']],
  'escape then b is meta-b': [['a', '\x1b', 'b']],
  'a lone escape is ignored after the timeout': [['a', '\x1b', 'tick:80', 'b'], { escapeCodeTimeout: 30 }],
  'question': [['yes\r'], {}, undefined, (rl, ev) => rl.question('Q? ', a => ev.push('answer:' + a))],
  'prompt, setPrompt and preserveCursor': [['ab', '\x1b[D', 'p:', 'pt:', 's:# ', 'p:'], { prompt: '$ ' }],
  'tab characters in the line': [['a\tb'], { tabSize: 4 }],
  'modifier arrows': [['ab cd', '\x1b[1;2D', '\x1b[1;3D', '\x1b[1;5D', '\x1b[1;5D', '\x1b[3;5~']],
  'alt-backspace and ctrl-w': [['ab cd', '\x1b\x7f', '\x17']],
  'up arrow searches by prefix': [['line1\r', 'lin', '\x1b[A', '\x1b[B']],
  'up at the oldest entry empties the line': [['first\r', '\x1b[A', '\x1b[A']],
  'historySize and the history option': [['z\r'], { history: ['x', 'y'], historySize: 2 }],
};

/* press the keys; per key [what was written, line, cursor]; plus the events */
async function play([keys, rlOptions = {}, columns, setup]) {
  const input = newInput();
  const writes = [];
  const output = {
    columns,
    on() {
      return this;
    },
    removeListener() {
      return this;
    },
    write(s, cb) {
      writes.push(s);
      cb?.();

      return true;
    },
  };

  if(columns === undefined) delete output.columns;

  const rl = readline.createInterface({ input, output, terminal: true, ...rlOptions });
  const events = [];

  for(const e of ['line', 'close', 'pause', 'resume', 'history']) rl.on(e, v => events.push(e == 'line' ? 'line:' + v : e == 'history' ? 'history:' + v.join('|') : e));

  setup?.(rl, events);
  await tick();

  const rows = [];

  for(const k of keys) {
    writes.length = 0;

    if(k.startsWith('tick:')) await tick(+k.slice(5));
    else if(k.startsWith('p:')) rl.prompt();
    else if(k.startsWith('pt:')) rl.prompt(true);
    else if(k.startsWith('s:')) rl.setPrompt(k.slice(2));
    else {
      input.push(k);
      await tick();
    }

    rows.push([esc(k), esc(writes.join('')), rl.line, rl.cursor]);
  }

  rl.close();
  input.destroy();

  return { events, rows };
}

/* recorded from node (RECORD_READLINE=1 node this-file prints it) */
const EXPECTED = {
  "typing": {"events":["history:abc","line:abc","pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["\\r","\\r\\n","",0]]},
  "prompt and enter": {"events":["history:hi","line:hi","pause","close"],"rows":[["h","h","h",1],["i","i","hi",2],["\\r","\\r\\n","",0]]},
  "insert in the middle": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["\\e[D","\\e[1D","abc",2],["X","\\e[1G\\e[0J> abXc\\e[6G","abXc",3],["\\e[C","\\e[1C","abXc",4],["Y","Y","abXcY",5]]},
  "backspace and delete": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["","\\e[1G\\e[0J> ab\\e[5G","ab",2],["\\e[D","\\e[1D","ab",1],["","\\e[1G\\e[0J> b\\e[3G","b",0],["\\e[3~","\\e[1G\\e[0J> \\e[3G","",0]]},
  "home and end": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["\u0001","\\e[3D","abc",0],["X","\\e[1G\\e[0J> Xabc\\e[4G","Xabc",1],["\u0005","\\e[3C","Xabc",4],["Y","Y","XabcY",5],["\\e[H","\\e[5D","XabcY",0],["Z","\\e[1G\\e[0J> ZXabcY\\e[4G","ZXabcY",1],["\\e[F","\\e[5C","ZXabcY",6],["W","W","ZXabcYW",7]]},
  "kill to end and start": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["d","d","abcd",4],["\\e[D","\\e[1D","abcd",3],["\\e[D","\\e[1D","abcd",2],["\u000b","\\e[1G\\e[0J> ab\\e[5G","ab",2],["x","x","abx",3],["\u0015","\\e[1G\\e[0J> \\e[3G","",0],["y","y","y",1],["z","z","yz",2],["\u0017","\\e[1G\\e[0J> \\e[3G","",0]]},
  "word moves and deletes": {"events":["pause","close"],"rows":[["h","h","h",1],["e","e","he",2],["l","l","hel",3],[" "," ","hel ",4],["w","w","hel w",5],["o","o","hel wo",6],[" "," ","hel wo ",7],["x","x","hel wo x",8],["\\eb","\\e[1D","hel wo x",7],["\\eb","\\e[3D","hel wo x",4],["\\ef","\\e[3C","hel wo x",7],["\\e[1;5D","\\e[3D","hel wo x",4],["\\e[1;5C","\\e[3C","hel wo x",7],["\\ed","\\e[1G\\e[0J> hel wo \\e[10G","hel wo ",7],["\u0017","\\e[1G\\e[0J> hel \\e[7G","hel ",4]]},
  "history with arrows": {"events":["history:a","line:a","history:b|a","line:b","pause","close"],"rows":[["a","a","a",1],["\\r","\\r\\n","",0],["b","b","b",1],["\\r","\\r\\n","",0],["\\e[A","\\e[1G\\e[0J> b\\e[4G","b",1],["\\e[A","\\e[1G\\e[0J> a\\e[4G","a",1],["\\e[B","\\e[1G\\e[0J> b\\e[4G","b",1],["\\e[B","\\e[1G\\e[0J> \\e[3G","",0],["\\e[B","","",0]]},
  "history with ctrl-p and ctrl-n": {"events":["history:one","line:one","history:two|one","line:two","pause","close"],"rows":[["one","one","one",3],["\\r","\\r\\n","",0],["two","two","two",3],["\\r","\\r\\n","",0],["\u0010","\\e[1G\\e[0J> two\\e[6G","two",3],["X","X","twoX",4],["\u000e","\\e[1G\\e[0J> \\e[3G","",0],["\u0010","\\e[1G\\e[0J> two\\e[6G","two",3],["\u0010","\\e[1G\\e[0J> one\\e[6G","one",3],["\u0010","\\e[1G\\e[0J> \\e[3G","",0]]},
  "removeHistoryDuplicates": {"events":["history:a","line:a","history:a","line:a","history:b|a","line:b","history:a|b","line:a","pause","close"],"rows":[["a\\r","a\\r\\n","",0],["a\\r","a\\r\\n","",0],["b\\r","b\\r\\n","",0],["a\\r","a\\r\\n","",0],["\\e[A","\\e[1G\\e[0J> a\\e[4G","a",1],["\\e[A","\\e[1G\\e[0J> b\\e[4G","b",1],["\\e[A","\\e[1G\\e[0J> \\e[3G","",0]]},
  "ctrl-d deletes, closes on an empty line": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["\u0004","","ab",2],["\\e[D","\\e[1D","ab",1],["\u0004","\\e[1G\\e[0J> a\\e[4G","a",1]]},
  "ctrl-d on an empty line": {"events":["pause","close"],"rows":[["\u0004","","",0]]},
  "ctrl-c closes without a SIGINT listener": {"events":["pause","close"],"rows":[["a","a","a",1],["\u0003","","a",1]]},
  "ctrl-l clears the screen": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["\f","\\e[1;1H\\e[0J\\e[1G\\e[0J> ab\\e[5G","ab",2]]},
  "ctrl-u then ctrl-y yanks": {"events":["pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["\u0015","\\e[1G\\e[0J> \\e[3G","",0],["\u0019","ab","ab",2]]},
  "tab without a completer inserts a tab": {"events":["pause","close"],"rows":[["a","a","a",1],["\\t","\\t","a\t",2]]},
  "completer: one match": {"events":["pause","resume","pause","close"],"rows":[["f","f","f",1],["o","o","fo",2],["\\t","obar","foobar",6]]},
  "completer: common prefix, then the list": {"events":["pause","resume","pause","resume","pause","close"],"rows":[["f","f","f",1],["\\t","o","fo",2],["\\t","\\r\\nfoo\\r\\nfob\\r\\n\\r\\n\\e[1G\\e[0J> fo\\e[5G","fo",2]]},
  "completer: callback form": {"events":["pause","resume","pause","resume","pause","close"],"rows":[["al","al","al",2],["\\t","p","alp",3],["\\t","\\r\\nalpha\\r\\nalps\\r\\n\\r\\n\\e[1G\\e[0J> alp\\e[6G","alp",3]]},
  "completer: list in columns": {"events":["pause","resume","pause","close"],"rows":[["\\t","","",0]]},
  "wrapping at the right edge": {"events":["history:abcdefghijk","line:abcdefghijk","pause","close"],"rows":[["a","a","a",1],["b","b","ab",2],["c","c","abc",3],["d","d","abcd",4],["e","e","abcde",5],["f","f","abcdef",6],["g","g","abcdefg",7],["h","\\e[1G\\e[0J> abcdefgh \\e[1G","abcdefgh",8],["i","i","abcdefghi",9],["j","j","abcdefghij",10],["k","k","abcdefghijk",11],["l","l","abcdefghijkl",12],["","\\e[1A\\e[1G\\e[0J> abcdefghijk\\e[4G","abcdefghijk",11],["\\r","\\r\\n","",0]]},
  "wide characters": {"events":["pause","close"],"rows":[["a","a","a",1],["漢","漢","a漢",2],["字","字","a漢字",3],["\\e[D","\\e[2D","a漢字",2],["X","\\e[1G\\e[0J> a漢X字\\e[7G","a漢X字",3]]},
  "emoji take two units": {"events":["pause","close"],"rows":[["é😀x","é😀x","é😀x",4],["\\e[D","\\e[1D","é😀x",3],["\\e[D","\\e[2D","é😀x",1],["\\e[D","\\e[1D","é😀x",0]]},
  "several keys in one chunk": {"events":["history:abcdef","line:abcdef","pause","close"],"rows":[["abc","abc","abc",3],["def\\rgh","def\\r\\ngh","gh",2]]},
  "a pasted newline": {"events":["history:x","line:x","history:y|x","line:y","pause","close"],"rows":[["x\\ny\\r","x\\r\\ny\\r\\n","",0]]},
  "CRLF is one enter": {"events":["history:x","line:x","history:y|x","line:y","pause","close"],"rows":[["x\\r\\ny\\r\\n","x\\r\\ny\\r\\n","",0]]},
  "escape then b is meta-b": {"events":["pause","close"],"rows":[["a","a","a",1],["\\e","","a",1],["b","\\e[1D","a",0]]},
  "a lone escape is ignored after the timeout": {"events":["pause","close"],"rows":[["a","a","a",1],["\\e","","a",1],["tick:80","","a",1],["b","b","ab",2]]},
  "question": {"events":["history:yes","answer:yes","pause","close"],"rows":[["yes\\r","yes\\r\\n","",0]]},
  "prompt, setPrompt and preserveCursor": {"events":["pause","close"],"rows":[["ab","ab","ab",2],["\\e[D","\\e[1D","ab",1],["p:","\\e[1G\\e[0J$ ab\\e[3G","ab",0],["pt:","\\e[1G\\e[0J$ ab\\e[3G","ab",0],["s:# ","","ab",0],["p:","\\e[1G\\e[0J# ab\\e[3G","ab",0]]},
  "tab characters in the line": {"events":["pause","close"],"rows":[["a\\tb","a\\tb","a\tb",3]]},
  "modifier arrows": {"events":["pause","close"],"rows":[["ab cd","ab cd","ab cd",5],["\\e[1;2D","\\e[1D","ab cd",4],["\\e[1;3D","","ab cd",4],["\\e[1;5D","\\e[1D","ab cd",3],["\\e[1;5D","\\e[3D","ab cd",0],["\\e[3;5~","\\e[1G\\e[0J> cd\\e[3G","cd",0]]},
  "alt-backspace and ctrl-w": {"events":["pause","close"],"rows":[["ab cd","ab cd","ab cd",5],["\\e","\\e[1G\\e[0J> ab \\e[6G","ab ",3],["\u0017","\\e[1G\\e[0J> \\e[3G","",0]]},
  "up arrow searches by prefix": {"events":["history:line1","line:line1","pause","close"],"rows":[["line1\\r","line1\\r\\n","",0],["lin","lin","lin",3],["\\e[A","\\e[1G\\e[0J> line1\\e[8G","line1",5],["\\e[B","\\e[1G\\e[0J> lin\\e[6G","lin",3]]},
  "up at the oldest entry empties the line": {"events":["history:first","line:first","pause","close"],"rows":[["first\\r","first\\r\\n","",0],["\\e[A","\\e[1G\\e[0J> first\\e[8G","first",5],["\\e[A","\\e[1G\\e[0J> \\e[3G","",0]]},
  "historySize and the history option": {"events":["history:z|x","line:z","pause","close"],"rows":[["z\\r","z\\r\\n","",0]]},
};

tests({
  ...Object.fromEntries(
    Object.entries(SCENARIOS).map(([name, scenario]) => [
      name,
      async () => {
        const got = await play(scenario);

        if(globalThis.process?.env?.RECORD_READLINE) EXPECTED[name] = got;
        else eq(JSON.stringify(got), JSON.stringify(EXPECTED[name]));
      },
    ]),
  ),
  async 'the history option is kept and an Interface without a tty is not in terminal mode'() {
    const rl = readline.createInterface({ input: newInput(), output: { write: () => true } });

    eq(rl.terminal, false);
    rl.close();
  },
  async 'getCursorPos counts the prompt and wraps at the columns'() {
    const input = newInput();
    const rl = readline.createInterface({ input, output: { columns: 10, on() {}, removeListener() {}, write: () => true }, terminal: true, prompt: '> ' });

    input.push('abcdefghijkl');
    await tick();
    eq(JSON.stringify(rl.getCursorPos()), '{"cols":4,"rows":1}');
    rl.close();
    input.destroy();
  },
  async 'ctrl-c emits SIGINT when there is a listener'() {
    const input = newInput();
    const rl = readline.createInterface({ input, output: { on() {}, removeListener() {}, write: () => true }, terminal: true });
    let n = 0;

    rl.on('SIGINT', () => n++);
    input.push('\x03');
    await tick();
    eq(n, 1);
    assert(!rl.closed);
    rl.close();
    input.destroy();
  },
  async 'write(data, key) presses keys'() {
    const input = newInput();
    const writes = [];
    const rl = readline.createInterface({ input, output: { on() {}, removeListener() {}, write: s => writes.push(s) && true }, terminal: true });

    rl.write('abc');
    rl.write(null, { ctrl: true, name: 'a' });
    rl.write('X');
    eq(esc(writes.join('')), 'abc\\e[3D\\e[1G\\e[0J> Xabc\\e[4G');
    eq([rl.line, rl.cursor].join(), 'Xabc,1');
    rl.close();
    input.destroy();
  },
});

if(globalThis.process?.env?.RECORD_READLINE) {
  // recording: the tests above fill EXPECTED; print it after they ran
  setTimeout(() => console.log('EXPECTED=' + JSON.stringify(EXPECTED)), 4000);
}
