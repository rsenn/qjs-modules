// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/perf_hooks.js: performance.now/timeOrigin, User Timing (mark, measure, entries) and PerformanceObserver */
import * as perfHooks from 'perf_hooks';
import 'timers';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* deno's node:perf_hooks lacks some of the classes; it has them as globals */
const { performance } = perfHooks;
const { PerformanceEntry = globalThis.PerformanceEntry, PerformanceMark = globalThis.PerformanceMark, PerformanceMeasure = globalThis.PerformanceMeasure, PerformanceObserver = globalThis.PerformanceObserver } = perfHooks;
/* deno returns entries unsorted and accepts observe({}); the spec and node do neither */
const strict = typeof Deno == 'undefined';
const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const fails = (fn, name) => {
  try {
    fn();
  } catch(e) {
    return e.name == name;
  }

  return false;
};
const clearAll = () => {
  performance.clearMarks();
  performance.clearMeasures();
};

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

  'timeOrigin is the epoch time of the origin'() {
    assert(Math.abs(performance.timeOrigin + performance.now() - Date.now()) < 50);
    assert('timeOrigin' in performance.toJSON());
  },

  'mark records and returns a PerformanceMark'() {
    clearAll();

    const m = performance.mark('a');

    assert(m instanceof PerformanceMark && m instanceof PerformanceEntry);
    eq([m.name, m.entryType, m.duration, m.detail].join(), 'a,mark,0,');
    eq(typeof m.startTime, 'number');
    eq(Object.prototype.toString.call(m), '[object PerformanceMark]');
    eq(performance.getEntriesByName('a').length, 1);
    clearAll();
  },

  'mark takes startTime and detail'() {
    clearAll();

    const m = performance.mark('b', { startTime: 5, detail: { x: 1 } });

    eq(m.startTime, 5);
    eq(m.detail.x, 1);
    eq(JSON.stringify(m.toJSON()), '{"name":"b","entryType":"mark","startTime":5,"duration":0,"detail":{"x":1}}');
    clearAll();
  },

  'mark rejects a missing name, a negative or non-numeric startTime'() {
    assert(fails(() => performance.mark(), 'TypeError'));
    assert(fails(() => performance.mark('n', { startTime: -1 }), 'TypeError'));
    assert(fails(() => performance.mark('n', { startTime: 'x' }), 'TypeError'));
    clearAll();
  },

  'new PerformanceMark does not record'() {
    clearAll();

    const m = new PerformanceMark('c', { startTime: 7 });

    eq([m.name, m.startTime, m.entryType].join(), 'c,7,mark');
    eq(performance.getEntries().length, 0);
  },

  'measure between marks, timestamps and options'() {
    clearAll();
    performance.mark('s', { startTime: 10 });
    performance.mark('e', { startTime: 25 });

    const sum = m => [m.entryType, m.startTime, m.duration].join();

    assert(performance.measure('m1', 's', 'e') instanceof PerformanceMeasure);
    eq(sum(performance.measure('m1', 's', 'e')), 'measure,10,15');
    eq(sum(performance.measure('m2', { start: 's', end: 'e' })), 'measure,10,15');
    eq(sum(performance.measure('m3', { start: 10, duration: 5 })), 'measure,10,5');
    eq(sum(performance.measure('m4', { end: 20, duration: 5 })), 'measure,15,5');
    eq(performance.measure('m5', { start: 's', end: 'e', detail: [1] }).detail[0], 1);
    eq(performance.measure('m6').startTime, 0);
    assert(performance.measure('m7', 's').duration > 0 || performance.now() < 10);
    clearAll();
  },

  'measure rejects unset marks and conflicting options'() {
    clearAll();
    performance.mark('s');
    assert(fails(() => performance.measure('x', 'nope'), 'SyntaxError'));
    assert(fails(() => performance.measure('x', 's', 'nope'), 'SyntaxError'));
    assert(fails(() => performance.measure('x', { start: 1, end: 2, duration: 3 }), 'TypeError'));
    assert(fails(() => performance.measure('x', { start: 1 }, 's'), 'TypeError'));
    clearAll();
  },

  'getEntries are sorted by startTime and filter by name and type'() {
    clearAll();
    performance.mark('late', { startTime: 30 });
    performance.mark('early', { startTime: 1 });
    performance.measure('late', { start: 2, end: 4 });

    if(strict) eq(performance.getEntries().map(e => e.startTime).join(), '1,2,30');
    eq(performance.getEntriesByName('late').length, 2);
    eq(performance.getEntriesByName('late', 'measure').length, 1);
    if(strict) eq(performance.getEntriesByType('mark').map(e => e.name).join(), 'early,late');
    eq(performance.getEntriesByType('bogus').length, 0);
    assert(fails(() => performance.getEntriesByType(), 'TypeError'));
    assert(fails(() => performance.getEntriesByName(), 'TypeError'));
    clearAll();
  },

  'clearMarks and clearMeasures by name or all'() {
    clearAll();
    performance.mark('a');
    performance.mark('b');
    performance.measure('m', { start: 1, end: 2 });
    performance.measure('n', { start: 1, end: 2 });
    performance.clearMarks('a');
    eq(performance.getEntriesByType('mark').map(e => e.name).join(), 'b');
    performance.clearMeasures('m');
    eq(performance.getEntriesByType('measure').map(e => e.name).join(), 'n');
    performance.clearMarks();
    performance.clearMeasures();
    eq(performance.getEntries().length, 0);
  },

  async 'PerformanceObserver batches the entries of its types'() {
    clearAll();

    const got = [];
    const o = new PerformanceObserver((list, observer) => got.push([list.getEntries().map(e => `${e.name}:${e.entryType}`).join('+'), observer === o].join()));

    o.observe({ entryTypes: ['mark'] });
    performance.mark('o1');
    performance.mark('o2');
    performance.measure('om', { start: 1, end: 2 });
    await sleep(20);
    o.disconnect();
    eq(got.join('|'), 'o1:mark+o2:mark,true');
    clearAll();
  },

  async 'PerformanceObserver type, buffered, takeRecords and disconnect'() {
    clearAll();
    performance.mark('before');

    const names = [];
    const o = new PerformanceObserver(list => names.push(...list.getEntries().map(e => e.name)));

    o.observe({ type: 'mark' });
    await sleep(20);
    eq(names.length, 0);
    o.observe({ type: 'mark', buffered: true });
    await sleep(20);
    eq(names.join(), 'before');

    performance.mark('t1');
    eq(o.takeRecords().map(e => e.name).join(), 't1');
    await sleep(20);
    eq(names.join(), 'before');

    performance.mark('t2');
    o.disconnect();
    await sleep(20);
    eq(names.join(), 'before');
    clearAll();
  },

  'PerformanceObserver validates its arguments'() {
    assert(fails(() => new PerformanceObserver(), 'TypeError'));

    const o = new PerformanceObserver(() => {});

    if(strict) {
      assert(fails(() => o.observe({}), 'TypeError'));
      assert(fails(() => o.observe({ entryTypes: ['mark'], type: 'mark' }), 'TypeError'));
    }

    assert(PerformanceObserver.supportedEntryTypes.includes('mark') && PerformanceObserver.supportedEntryTypes.includes('measure'));
  },
});
