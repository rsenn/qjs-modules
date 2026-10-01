import { Repeater } from 'repeater';
import { assert, eq, tests } from '../../lib/tinytest.js';

function mk(vals) {
  return new Repeater(async (push, stop) => {
    for(const v of vals) await push(v);
    stop();
  });
}

async function collect(r) {
  const out = [];
  for await(const v of r) out.push(v);
  return out;
}

tests({
  async 'constructor executor(push, stop) pushes values consumed by next()'() {
    const r = new Repeater(async (push, stop) => {
      await push(1);
      await push(2);
      stop();
    });

    eq((await r.next()).value, 1);
    eq((await r.next()).value, 2);
    eq((await r.next()).done, true);
  },
  async 'state reflects lifecycle transitions'() {
    const r = new Repeater(async (push, stop) => {
      await push('x');
      stop();
    });
    const initial = r.state;
    await r.next();
    const started = r.state;
    await r.next();
    const done = r.state;
    assert(initial !== started);
    assert(started !== done);
  },
  async 'Symbol.asyncIterator returns itself and supports for-await'() {
    const r = mk([1, 2, 3]);
    eq(r[Symbol.asyncIterator](), r);
    eq(JSON.stringify(await collect(r)), JSON.stringify([1, 2, 3]));
  },
  async 'Repeater.race() yields from whichever repeater settles first'() {
    const result = await collect(Repeater.race([mk([1, 2]), mk([10, 20])]));
    eq(JSON.stringify(result), JSON.stringify([1, 2]));
  },
  async 'Repeater.merge() interleaves values from all inputs'() {
    const result = await collect(Repeater.merge([mk([1, 2]), mk([10, 20])]));
    eq(result.length, 4);
    assert(result.includes(1));
    assert(result.includes(10));
    assert(result.includes(2));
    assert(result.includes(20));
  },
  async 'Repeater.zip() yields tuples combining one value from each input'() {
    const result = await collect(Repeater.zip([mk([1, 2]), mk(['a', 'b'])]));
    eq(JSON.stringify(result), JSON.stringify([[1, 'a'], [2, 'b']]));
  },
  async 'Repeater.latest() yields tuples of the latest value from each input'() {
    const result = await collect(Repeater.latest([mk([1, 2]), mk(['a', 'b'])]));
    eq(JSON.stringify(result), JSON.stringify([[1, 'a'], [2, 'a'], [2, 'b']]));
  },
});
