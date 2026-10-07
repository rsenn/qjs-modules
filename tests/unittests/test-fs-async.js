/* Node's callback fs API, fs.promises, fs/promises, timers/promises, setImmediate */
import fs, { mkdtempSync, promises, rmSync } from 'fs';
import * as fsp from 'fs/promises';
import * as tp from 'timers/promises';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* qjsm lacks AbortController and setImmediate until lib/globals.js is imported */
if(typeof AbortController == 'undefined') await import('../../lib/globals.js');

const dir = mkdtempSync('/tmp/fs-async-');
const call = (fn, ...args) => new Promise(resolve => fn(...args, (err, ...rest) => resolve([err, ...rest])));

tests({
  async 'writeFile/readFile callbacks'() {
    const [e1] = await call(fs.writeFile, `${dir}/a.txt`, 'hello');
    const [e2, text] = await call(fs.readFile, `${dir}/a.txt`, 'utf8');

    assert(e1 === null);
    assert(e2 === null);
    eq(text, 'hello');
  },
  async 'a failing call passes the error with a code'() {
    const [err] = await call(fs.stat, `${dir}/missing`);

    eq(err.code, 'ENOENT');
  },
  async 'stat, readdir, rename, unlink'() {
    const [, st] = await call(fs.stat, `${dir}/a.txt`);
    const [, names] = await call(fs.readdir, dir);

    eq(st.size, 5);
    eq(names.join(), 'a.txt');
    await call(fs.rename, `${dir}/a.txt`, `${dir}/b.txt`);
    await call(fs.unlink, `${dir}/b.txt`);
    eq((await call(fs.readdir, dir))[1].length, 0);
  },
  async 'exists calls back with a boolean'() {
    eq(await new Promise(r => fs.exists(dir, r)), true);
    eq(await new Promise(r => fs.exists(`${dir}/nope`, r)), false);
  },
  async 'open, write, read, close'() {
    const [, fd] = await call(fs.open, `${dir}/c.bin`, fs.constants.O_RDWR | fs.constants.O_CREAT, 0o644);
    const out = new Uint8Array([1, 2, 3]);
    const [, written] = await call(fs.write, fd, out, 0, 3, 0);
    const back = new Uint8Array(3);
    const [, n] = await call(fs.read, fd, back, 0, 3, 0);

    eq(written, 3);
    eq(n, 3);
    eq(back.join(), '1,2,3');
    await call(fs.close, fd);
  },
  async 'fs.promises and fs/promises are the same functions'() {
    await promises.writeFile(`${dir}/p.txt`, 'promise');
    eq(await fsp.readFile(`${dir}/p.txt`, 'utf8'), 'promise');
    eq((await promises.stat(`${dir}/p.txt`)).size, 7);
    assert(typeof promises.mkdir == 'function');
  },
  async 'fs.promises rejects with a code'() {
    let code;

    try {
      await promises.readFile(`${dir}/missing`);
    } catch(e) {
      code = e.code;
    }
    eq(code, 'ENOENT');
  },
  async 'timers/promises setTimeout resolves its value'() {
    const t = Date.now();

    eq(await tp.setTimeout(20, 'v'), 'v');
    assert(Date.now() - t >= 15);
  },
  async 'timers/promises setTimeout honours an aborted signal'() {
    const ac = new AbortController();
    let name;

    setTimeout(() => ac.abort(), 1);
    try {
      await tp.setTimeout(1000, 'v', { signal: ac.signal });
    } catch(e) {
      name = e.name;
    }
    eq(name, 'AbortError');
  },
  async 'timers/promises setInterval iterates'() {
    let n = 0;

    for await(const v of tp.setInterval(2, 'tick')) {
      eq(v, 'tick');
      if(++n == 3) break;
    }
    eq(n, 3);
  },
  async 'setImmediate runs, clearImmediate cancels'() {
    const order = [];

    setImmediate(a => order.push(a), 'ran');
    clearImmediate(setImmediate(() => order.push('cancelled')));
    await tp.setImmediate();
    eq(order.join(), 'ran');
  },
  'teardown'() {
    rmSync(dir, { recursive: true, force: true });
  },
});
