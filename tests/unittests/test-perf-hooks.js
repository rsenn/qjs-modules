import { performance } from 'perf_hooks';
import 'timers';
import { assert, eq, tests } from '../../lib/tinytest.js';

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));

tests({
  'now() is a number'() {
    eq('number', typeof performance.now());
  },

  'now() never goes backwards'() {
    const a = performance.now();
    const b = performance.now();

    assert(b >= a, `${a} then ${b}`);
  },

  async 'now() advances across a timer'() {
    const start = performance.now();

    await sleep(50);

    const elapsed = performance.now() - start;

    assert(elapsed >= 40 && elapsed < 2000, `elapsed ${elapsed}`);
  },

  'now() is relative to a small origin, not the epoch'() {
    assert(performance.now() < 3.6e6, `${performance.now()}`);
  },
});
