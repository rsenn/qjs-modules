// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* the Readline class of readline/promises: collected cursor and erase actions, commit and rollback */
import * as promises from 'readline/promises';
import { assert, eq, tests } from '../../lib/tinytest.js';

const { Readline } = promises;
const esc = s => s.replace(/\x1b/g, '\\e');
const tick = () => new Promise(r => setTimeout(r, 5));

/* node, bun and deno want a Writable, qjsm takes any object with write() */
let Writable;

try {
  ({ Writable } = await import('node:stream'));
} catch {}

/* a stream that records what it is given */
const sink = () => {
  const writes = [];

  if(Writable)
    return Object.assign(
      new Writable({
        decodeStrings: false,
        write(chunk, enc, cb) {
          writes.push(esc(String(chunk)));
          cb();
        },
      }),
      { writes },
    );

  return {
    writes,
    write(s, cb) {
      writes.push(esc(s));
      cb?.();

      return true;
    },
  };
};

const code = fn => {
  try {
    fn();
  } catch(e) {
    return `${e.name} ${e.code}`;
  }

  return 'no error';
};

tests({
  'Readline is exported by readline/promises'() {
    eq('function', typeof Readline);
    eq('function', typeof promises.default.Readline);
  },

  async 'actions are collected and sent in one write by commit'() {
    const out = sink();
    const rl = new Readline(out);

    eq(rl, rl.cursorTo(3));
    rl.cursorTo(1, 2).moveCursor(-2, 3).moveCursor(4, -1).clearLine(-1).clearLine(0).clearLine(1).clearScreenDown();
    eq(0, out.writes.length);

    await rl.commit();
    eq(['\\e[4G\\e[3;2H\\e[2D\\e[3B\\e[4C\\e[1A\\e[1K\\e[2K\\e[0K\\e[0J'].join(), out.writes.join());
  },

  async 'commit with nothing collected writes an empty string and resolves'() {
    const out = sink();

    await new Readline(out).commit();
    eq([''].join(), out.writes.join());
  },

  async 'commit clears the collected actions'() {
    const out = sink();
    const rl = new Readline(out);

    rl.clearLine(0);
    await rl.commit();
    await rl.commit();
    eq(['\\e[2K', ''].join(), out.writes.join());
  },

  async 'rollback drops the collected actions'() {
    const out = sink();
    const rl = new Readline(out);

    eq(rl, rl.cursorTo(0).clearLine(0).rollback());
    await rl.commit();
    eq([''].join(), out.writes.join());
  },

  async 'autoCommit writes each action on the next tick'() {
    const out = sink();
    const rl = new Readline(out, { autoCommit: true });

    eq(rl, rl.cursorTo(2));
    rl.clearLine(1).clearScreenDown().moveCursor(1, 0);
    eq(0, out.writes.length);

    await tick();
    eq(['\\e[3G', '\\e[0K', '\\e[0J', '\\e[1C'].join(), out.writes.join());
  },

  async 'moveCursor with nothing to move adds nothing'() {
    const out = sink();
    const rl = new Readline(out);

    rl.moveCursor(0, 0).moveCursor(undefined, undefined);
    await rl.commit();
    eq([''].join(), out.writes.join());
  },

  'a stream that is not writable is rejected'() {
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => new Readline({})));
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => new Readline(null)));
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => new Readline('x')));
  },

  'autoCommit must be a boolean'() {
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => new Readline(sink(), { autoCommit: 1 })));
    eq('no error', code(() => new Readline(sink(), { autoCommit: undefined })));
    eq('no error', code(() => new Readline(sink(), {})));
  },

  'arguments are validated as integers'() {
    const rl = new Readline(sink());

    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => rl.cursorTo('1')));
    eq('RangeError ERR_OUT_OF_RANGE', code(() => rl.cursorTo(1.5)));
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => rl.cursorTo(1, 'a')));
    eq('no error', code(() => rl.cursorTo(1, null)));
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => rl.moveCursor(1)));
    eq('RangeError ERR_OUT_OF_RANGE', code(() => rl.moveCursor(1, 0.5)));
    eq('RangeError ERR_OUT_OF_RANGE', code(() => rl.clearLine(2)));
    eq('RangeError ERR_OUT_OF_RANGE', code(() => rl.clearLine(-2)));
    eq('TypeError ERR_INVALID_ARG_TYPE', code(() => rl.clearLine()));
  },

  async 'cursorTo without y moves in the column, with y to a position'() {
    const out = sink();
    const rl = new Readline(out);

    rl.cursorTo(0).cursorTo(0, 0).cursorTo(9, 4);
    await rl.commit();
    eq(['\\e[1G\\e[1;1H\\e[5;10H'].join(), out.writes.join());
  },
});
