// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/readline.js and lib/readlinePromises.js: Node's readline, non-terminal mode */
import * as readline from 'readline';
import * as rlp from 'readline/promises';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* node, bun and deno need a Readable as input; qjsm takes any async iterable */
let Readable;
const AbortController = globalThis.AbortController ?? (await import('abort')).AbortController;

try {
  ({ Readable } = await import('node:stream'));
} catch {}

const input = chunks => {
  const gen = (async function* () {
    for(const c of chunks) yield c;
  })();

  return Readable?.from ? Readable.from(gen) : gen;
};

const sink = () => ({
  w: [],
  write(c, cb) {
    this.w.push(String(c));
    cb?.();

    return true;
  },
});

const tick = (ms = 20) => new Promise(r => setTimeout(r, ms));
const closed = rl => new Promise(r => rl.on('close', r));
const lines = async (chunks, options) => {
  const rl = readline.createInterface({ input: input(chunks), ...options });
  const got = [];

  rl.on('line', l => got.push(l));
  await closed(rl);

  return JSON.stringify(got);
};

tests({
  async 'lines end at \\n, \\r\\n and \\r, however the input is chunked'() {
    eq(await lines(['a\nb\r\nc\rd', '\ne\n', 'f']), '["a","b","c","d","e","f"]');
    eq(await lines(['x\n\n\ny']), '["x","","","y"]');
    eq(await lines(['']), '[]');
  },
  async 'for await iterates the lines'() {
    const rl = readline.createInterface({ input: input(['€\n', 'é']) });
    const got = [];

    for await(const l of rl) got.push(l);

    eq(got.join('|'), '€|é');
  },
  async 'question takes the next line; the others stay line events'() {
    const rl = readline.createInterface({ input: input(['one\ntwo\nthree\n']) });
    const got = [];

    rl.question('Q1? ', a => got.push(['a1', a].join(':')));
    rl.on('line', l => got.push('line:' + l));
    await closed(rl);

    eq(got.join(), 'a1:one,line:two,line:three');
  },
  async 'question and prompt write to output'() {
    const out = sink();
    const rl = readline.createInterface({ input: input(['yes\n']), output: out, prompt: '$ ' });
    const a = await new Promise(r => rl.question('Name? ', r));

    eq(a, 'yes');
    eq(rl.getPrompt(), '$ ');
    rl.setPrompt('> ');
    rl.prompt();
    eq(out.w.join('|'), 'Name? |> ');
    eq(rl.terminal, false);
    eq(rl.line, '');
    rl.close();
    eq(rl.closed, true);
  },
  async 'question on a closed interface throws ERR_USE_AFTER_CLOSE'() {
    const rl = readline.createInterface({ input: input([]) });

    rl.close();

    let code;

    try {
      rl.question('x', () => {});
    } catch(e) {
      code = e.code;
    }

    eq(code, 'ERR_USE_AFTER_CLOSE');
  },
  async 'close emits once; pause then close at the end of input'() {
    const rl = readline.createInterface({ input: input(['a\n']) });
    let n = 0;

    rl.on('close', () => n++);
    rl.close();
    rl.close();
    await tick();
    eq(n, 1);

    const rl2 = readline.createInterface({ input: input(['a\nb\n']) });
    const ev = [];

    for(const e of ['pause', 'resume', 'close']) rl2.on(e, () => ev.push(e));
    rl2.on('line', l => ev.push(l));
    await closed(rl2);
    eq(ev.join(), 'a,b,pause,close');
  },
  async 'close inside a line handler still delivers the rest of the chunk'() {
    const rl = readline.createInterface({ input: input(['a\nb\nc\n']) });
    const got = [];

    rl.on('line', l => {
      got.push(l);
      if(l == 'a') rl.close();
    });
    await tick();
    eq(got.join(), 'a,b,c');
  },
  async 'write feeds the line splitter'() {
    const rl = readline.createInterface({ input: input([]) });
    const got = [];

    rl.on('line', l => got.push(l));
    rl.write('hi\nthere');
    rl.write('\n');
    await tick();
    eq(got.join(), 'hi,there');
    rl.close();
  },
  async 'an AbortSignal closes the interface'() {
    const ac = new AbortController();
    const rl = readline.createInterface({ input: input([]), signal: ac.signal });
    let done = false;

    rl.on('close', () => (done = true));
    ac.abort();
    await tick();
    assert(done);
  },
  async 'createInterface without input throws a TypeError'() {
    let name;

    try {
      readline.createInterface({});
    } catch(e) {
      name = e.name;
    }

    eq(name, 'TypeError');
  },
  async 'readline/promises question resolves with the answer'() {
    const slow = (async function* () {
      yield 'alpha\n';
      await tick();
      yield 'beta\n';
    })();
    const rl = rlp.createInterface({ input: Readable?.from ? Readable.from(slow) : slow });
    const a = await rl.question('?');
    const b = await rl.question('?');

    eq([a, b].join(), 'alpha,beta');
    rl.close();
  },
  async 'readline/promises: a pending question stays pending on close'() {
    const rl = rlp.createInterface({ input: input([]) });
    const p = rl.question('?').then(v => 'resolved ' + v, e => 'rejected ' + e.name);

    rl.close();
    eq(await Promise.race([p, tick().then(() => 'pending')]), 'pending');
  },
  async 'cursorTo, moveCursor, clearLine and clearScreenDown write ANSI sequences'() {
    const out = sink();

    readline.cursorTo(out, 3);
    readline.cursorTo(out, 3, 4);
    readline.moveCursor(out, 2, -1);
    readline.moveCursor(out, -2, 3);
    readline.clearLine(out, 0);
    readline.clearLine(out, -1);
    readline.clearLine(out, 1);
    readline.clearScreenDown(out);
    eq(JSON.stringify(out.w), JSON.stringify(['\x1b[4G', '\x1b[5;4H', '\x1b[2C\x1b[1A', '\x1b[2D\x1b[3B', '\x1b[2K', '\x1b[1K', '\x1b[0K', '\x1b[0J']));
    eq(readline.cursorTo(null, 1), true);
  },
});
