/* bun/test.js: bun:test, describe/test/expect and the hooks, run when the file's code has finished.
 * depends on: inspect (failure messages).
 * rule: Bun's API and output, the tests of a file run in order after it was evaluated, then the process exits.
 *
 *   import { describe, expect, test } from 'bun:test';
 *   describe('math', () => test('adds', () => expect(1 + 1).toBe(2)));
 *   // qjsm file.test.js   exit status 1 when a test fails
 *
 * | bun:test                                   | here                                         |
 * | ------------------------------------------ | -------------------------------------------- |
 * | describe, test, it                         | .skip .only .todo .if .skipIf .todoIf .each  |
 * | beforeAll, beforeEach, afterEach, afterAll | per describe, nested hooks run outside in    |
 * | setDefaultTimeout                          | 5000 ms, also test(name, fn, ms)             |
 * | expect                                     | not, resolves, rejects, asymmetric matchers  |
 * | mock, spyOn, jest                          | call records and the usual mock methods      |
 *
 * limits:
 *   not here     snapshots, fake timers, setSystemTime, expect.assertions, test.concurrent runs in order
 */
import { inspect as inspectValue } from 'inspect';

const inspect = v => inspectValue(v, { colors: false, depth: 4, breakLength: 100 });
const now = () => globalThis.performance?.now() ?? Date.now();

/* the tree of suites and tests, filled while the file runs */
const root = { name: '', parent: null, items: [], hooks: { beforeAll: [], afterAll: [], beforeEach: [], afterEach: [] }, mode: 'run' };
let current = root;
let timeout = 5000;
let hasOnly = false;
let scheduled = null;
let started = false;
let expectCalls = 0;

const schedule = () => {
  if(started) return;

  clearTimeout(scheduled);
  scheduled = setTimeout(run, 0);
};

export const setDefaultTimeout = ms => {
  timeout = ms;
};

/* printf-style title of an each() row: %s %d %i %f %j %o %p %# %% */
function format(title, row, index) {
  let used = 0;
  const text = title.replace(/%([sdifjop#%])/g, (all, c) => {
    if(c == '%') return '%';
    if(c == '#') return String(index);

    const v = row[used++];

    return c == 's' ? (typeof v == 'string' ? v : inspect(v)) : c == 'd' || c == 'i' ? String(Math.trunc(Number(v))) : c == 'f' ? String(Number(v)) : c == 'j' ? JSON.stringify(v) : inspect(v);
  });

  return text;
}

function makeTest(kind, name, fn, options, mode) {
  const ms = typeof options == 'number' ? options : options?.timeout;
  const item = { kind: 'test', name, fn, parent: current, mode, timeout: ms };

  if(mode == 'only') hasOnly = true;

  current.items.push(item);
  schedule();
}

function makeSuite(name, fn, mode) {
  const suite = { kind: 'suite', name, parent: current, items: [], hooks: { beforeAll: [], afterAll: [], beforeEach: [], afterEach: [] }, mode };

  if(mode == 'only') hasOnly = true;

  current.items.push(suite);

  const saved = current;

  current = suite;

  try {
    fn?.();
  } finally {
    current = saved;
  }

  schedule();
}

/* describe and test with their modifiers: maker(name, fn, options, mode) */
function withModifiers(maker) {
  const make = mode => (name, fn, options) => maker(String(name), fn, options, mode);
  const f = make('run');
  const each = mode => table => (name, fn, options) => table.forEach((row, i) => maker(format(String(name), Array.isArray(row) ? row : [row], i), Array.isArray(row) ? () => fn(...row) : () => fn(row), options, mode));

  f.skip = make('skip');
  f.only = make('only');
  f.todo = make('todo');
  f.if = cond => (cond ? f : f.skip);
  f.skipIf = cond => (cond ? f.skip : f);
  f.todoIf = cond => (cond ? f.todo : f);
  f.each = each('run');
  f.concurrent = f;
  f.serial = f;
  f.failing = f;

  return f;
}

export const test = withModifiers((name, fn, options, mode) => makeTest('test', name, fn, options, mode));
export const it = test;
export const describe = withModifiers((name, fn, options, mode) => makeSuite(name, fn, mode));

const hook = name => fn => {
  current.hooks[name].push(fn);
  schedule();
};

export const beforeAll = hook('beforeAll');
export const beforeEach = hook('beforeEach');
export const afterEach = hook('afterEach');
export const afterAll = hook('afterAll');

/* the equality of toEqual, strict when undefined properties and prototypes count */
const ASYMMETRIC = Symbol('asymmetric');

const isAsymmetric = v => v !== null && typeof v == 'object' && v[ASYMMETRIC] === true;

function equals(a, b, strict = false, seen = []) {
  if(isAsymmetric(a)) return a.asymmetricMatch(b);
  if(isAsymmetric(b)) return b.asymmetricMatch(a);
  if(Object.is(a, b)) return true;
  if(typeof a != 'object' || typeof b != 'object' || a === null || b === null) return false;

  const tag = Object.prototype.toString.call(a);

  if(tag != Object.prototype.toString.call(b)) return false;
  if(strict && Object.getPrototypeOf(a) !== Object.getPrototypeOf(b)) return false;
  if(a instanceof Date) return a.getTime() === b.getTime() || (Number.isNaN(a.getTime()) && Number.isNaN(b.getTime()));
  if(a instanceof RegExp) return String(a) === String(b);
  if(a instanceof Error) return a.name === b.name && a.message === b.message;

  if(seen.some(([x, y]) => x === a && y === b)) return true;

  seen = [...seen, [a, b]];

  if(a instanceof ArrayBuffer) return equals(new Uint8Array(a), new Uint8Array(b), strict, seen);
  if(ArrayBuffer.isView(a)) return a.byteLength === b.byteLength && equals(Array.from(new Uint8Array(a.buffer, a.byteOffset, a.byteLength)), Array.from(new Uint8Array(b.buffer, b.byteOffset, b.byteLength)), strict, seen);

  if(a instanceof Map) return a.size === b.size && [...a].every(([k, v]) => (b.has(k) ? equals(v, b.get(k), strict, seen) : [...b].some(([k2, v2]) => equals(k, k2, strict, seen) && equals(v, v2, strict, seen))));
  if(a instanceof Set) return a.size === b.size && [...a].every(v => b.has(v) || [...b].some(w => equals(v, w, strict, seen)));

  if(Array.isArray(a) && a.length !== b.length) return false;

  const keys = o => Reflect.ownKeys(o).filter(k => Object.prototype.propertyIsEnumerable.call(o, k) && (strict || o[k] !== undefined));
  const ka = keys(a);
  const kb = keys(b);

  if(ka.length !== kb.length) return false;

  return ka.every(k => Object.prototype.hasOwnProperty.call(b, k) && equals(a[k], b[k], strict, seen));
}

/* toMatchObject: every property of subset is in value, arrays match element by element */
function matches(value, subset, seen = []) {
  if(isAsymmetric(subset)) return subset.asymmetricMatch(value);
  if(Object.is(value, subset)) return true;
  if(subset === null || typeof subset != 'object' || value === null || typeof value != 'object') return false;
  if(subset instanceof Date || subset instanceof RegExp || subset instanceof Error || subset instanceof Map || subset instanceof Set || ArrayBuffer.isView(subset)) return equals(value, subset);
  if(Array.isArray(subset)) return Array.isArray(value) && value.length === subset.length && subset.every((s, i) => matches(value[i], s, seen));
  if(seen.some(([x, y]) => x === value && y === subset)) return true;

  seen = [...seen, [value, subset]];

  return Reflect.ownKeys(subset).every(k => k in value && matches(value[k], subset[k], seen));
}

const asymmetric = (describe_, match) => ({ [ASYMMETRIC]: true, asymmetricMatch: match, toString: () => describe_, [Symbol.for('nodejs.util.inspect.custom')]: () => describe_ });

const typeOf = (v, C) => {
  if(C === String) return typeof v == 'string' || v instanceof String;
  if(C === Number) return typeof v == 'number' || v instanceof Number;
  if(C === Boolean) return typeof v == 'boolean' || v instanceof Boolean;
  if(C === BigInt) return typeof v == 'bigint';
  if(C === Symbol) return typeof v == 'symbol';
  if(C === Function) return typeof v == 'function';
  if(C === Object) return typeof v == 'object' && v !== null;

  return v instanceof C;
};

const fail = message => {
  throw Object.assign(new Error(message), { name: 'error', expectFailure: true });
};

/* the lines of a failed comparison */
const compare = (name, received, expected, hasExpected = true) => `expect(received).${name}(${hasExpected ? 'expected' : ''})\n\n${hasExpected ? `Expected: ${inspect(expected)}\n` : ''}Received: ${inspect(received)}`;

function getPath(value, path) {
  const parts = Array.isArray(path) ? path : String(path).replace(/\[(\d+)\]/g, '.$1').split('.').filter(Boolean);
  let v = value;

  for(const p of parts) {
    if(v === null || v === undefined || !(p in Object(v))) return { found: false };

    v = v[p];
  }

  return { found: true, value: v };
}

/* matcher name -> (received, ...args) => { pass, message }; the message is for the case it did not pass */
const matchers = {
  toBe: (r, e) => ({ pass: Object.is(r, e), message: compare('toBe', r, e) }),
  toEqual: (r, e) => ({ pass: equals(r, e), message: compare('toEqual', r, e) }),
  toStrictEqual: (r, e) => ({ pass: equals(r, e, true), message: compare('toStrictEqual', r, e) }),
  toBeNull: r => ({ pass: r === null, message: compare('toBeNull', r, null, false) }),
  toBeUndefined: r => ({ pass: r === undefined, message: compare('toBeUndefined', r, null, false) }),
  toBeDefined: r => ({ pass: r !== undefined, message: compare('toBeDefined', r, null, false) }),
  toBeTruthy: r => ({ pass: !!r, message: compare('toBeTruthy', r, null, false) }),
  toBeFalsy: r => ({ pass: !r, message: compare('toBeFalsy', r, null, false) }),
  toBeNaN: r => ({ pass: Number.isNaN(r), message: compare('toBeNaN', r, null, false) }),
  toBeGreaterThan: (r, e) => ({ pass: r > e, message: compare('toBeGreaterThan', r, e) }),
  toBeGreaterThanOrEqual: (r, e) => ({ pass: r >= e, message: compare('toBeGreaterThanOrEqual', r, e) }),
  toBeLessThan: (r, e) => ({ pass: r < e, message: compare('toBeLessThan', r, e) }),
  toBeLessThanOrEqual: (r, e) => ({ pass: r <= e, message: compare('toBeLessThanOrEqual', r, e) }),
  toBeCloseTo: (r, e, digits = 2) => ({ pass: Math.abs(r - e) < 10 ** -digits / 2, message: compare('toBeCloseTo', r, e) }),
  toBeInstanceOf: (r, C) => ({ pass: r instanceof C, message: compare('toBeInstanceOf', r, C) }),
  toBeTypeOf: (r, t) => ({ pass: typeof r === t, message: compare('toBeTypeOf', r, t) }),
  toHaveLength: (r, n) => ({ pass: r?.length === n, message: compare('toHaveLength', r?.length, n) }),
  toStartWith: (r, s) => ({ pass: typeof r == 'string' && r.startsWith(s), message: compare('toStartWith', r, s) }),
  toEndWith: (r, s) => ({ pass: typeof r == 'string' && r.endsWith(s), message: compare('toEndWith', r, s) }),
  toContain: (r, e) => ({ pass: typeof r == 'string' ? r.includes(e) : r != null && typeof r[Symbol.iterator] == 'function' ? [...r].some(v => Object.is(v, e) || v === e) : false, message: compare('toContain', r, e) }),
  toContainEqual: (r, e) => ({ pass: r != null && [...r].some(v => equals(v, e)), message: compare('toContainEqual', r, e) }),
  toMatch: (r, e) => ({ pass: typeof r == 'string' && (typeof e == 'string' ? r.includes(e) : e.test(r)), message: compare('toMatch', r, e) }),
  toMatchObject: (r, e) => ({ pass: matches(r, e), message: compare('toMatchObject', r, e) }),
  toHaveProperty(r, path, ...rest) {
    const { found, value } = r === null || r === undefined ? { found: false } : getPath(r, path);

    return { pass: found && (!rest.length || equals(value, rest[0])), message: compare('toHaveProperty', r, rest.length ? [path, rest[0]] : path) };
  },
  toSatisfy: (r, fn) => ({ pass: !!fn(r), message: compare('toSatisfy', r, fn) }),
  toThrow(r, expected) {
    let thrown, threw = false;

    try {
      if(this.rejected) throw r;
      else if(typeof r == 'function') r();
      else fail('expect(received).toThrow(): received value must be a function');
    } catch(e) {
      if(e?.expectFailure && !(expected instanceof Error)) throw e;

      threw = true;
      thrown = e;
    }

    let pass = threw;

    if(threw && expected !== undefined) {
      const message = thrown?.message ?? String(thrown);

      pass = typeof expected == 'string' ? message.includes(expected) : expected instanceof RegExp ? expected.test(message) : typeof expected == 'function' ? thrown instanceof expected : expected instanceof Error ? message === expected.message : isAsymmetric(expected) ? expected.asymmetricMatch(thrown) : pass;
    }

    return { pass, message: `expect(received).toThrow(${expected === undefined ? '' : 'expected'})\n\n${expected === undefined ? '' : `Expected: ${inspect(expected)}\n`}${threw ? `Thrown: ${inspect(thrown)}` : 'Received function did not throw'}` };
  },
};

const calls = fn => fn?.mock?.calls ?? fail('expect(received).toHaveBeenCalled(): received value must be a mock or spy function');

Object.assign(matchers, {
  toHaveBeenCalled: r => ({ pass: calls(r).length > 0, message: `expect(received).toHaveBeenCalled()\n\nExpected number of calls: >= 1\nReceived number of calls: ${calls(r).length}` }),
  toHaveBeenCalledTimes: (r, n) => ({ pass: calls(r).length === n, message: `expect(received).toHaveBeenCalledTimes(expected)\n\nExpected number of calls: ${n}\nReceived number of calls: ${calls(r).length}` }),
  toHaveBeenCalledWith: (r, ...args) => ({ pass: calls(r).some(c => equals(c, args)), message: `expect(received).toHaveBeenCalledWith(expected)\n\nExpected: ${inspect(args)}\nReceived: ${inspect(calls(r))}` }),
  toHaveBeenLastCalledWith: (r, ...args) => ({ pass: equals(calls(r).at(-1), args), message: `expect(received).toHaveBeenLastCalledWith(expected)\n\nExpected: ${inspect(args)}\nReceived: ${inspect(calls(r).at(-1))}` }),
});

const custom = {};

function makeExpect(received, negate = false, wrap = v => v, rejected = false) {
  const api = {};
  const all = { ...matchers, ...custom };

  for(const name of Object.keys(all))
    api[name] = (...args) => {
      expectCalls++;

      const run = value => {
        const { pass, message } = all[name].call({ equals, isNot: negate, rejected }, value, ...args);

        if(pass === negate) fail(negate ? message.replace('expect(received).', 'expect(received).not.') : message);
      };

      return wrap(run);
    };

  return api;
}

/* a matcher set that runs at once on value */
const direct = (value, negate = false, rejected = false) => makeExpect(value, negate, run => void run(value), rejected);

export function expect(received) {
  const api = Object.assign(direct(received), {
    not: direct(received, true),
    resolves: new Proxy({}, { get: (_, name) => (...args) => Promise.resolve(received).then(v => direct(v)[name](...args)) }),
    rejects: new Proxy({}, { get: (_, name) => (...args) => Promise.resolve(received).then(() => fail('expect(received).rejects: promise resolved instead of rejecting'), e => direct(e, false, true)[name](...args)) }),
  });

  api.resolves.not = new Proxy({}, { get: (_, name) => (...args) => Promise.resolve(received).then(v => direct(v, true)[name](...args)) });
  api.rejects.not = new Proxy({}, { get: (_, name) => (...args) => Promise.resolve(received).then(() => fail('expect(received).rejects: promise resolved instead of rejecting'), e => direct(e, true, true)[name](...args)) });

  return api;
}

Object.assign(expect, {
  any: C => asymmetric(`Any<${C?.name ?? C}>`, v => typeOf(v, C)),
  anything: () => asymmetric('Anything', v => v !== null && v !== undefined),
  stringMatching: re => asymmetric(`StringMatching ${re}`, v => typeof v == 'string' && (re instanceof RegExp ? re.test(v) : v.includes(re))),
  stringContaining: s => asymmetric(`StringContaining ${s}`, v => typeof v == 'string' && v.includes(s)),
  arrayContaining: a => asymmetric(`ArrayContaining ${inspect(a)}`, v => Array.isArray(v) && a.every(x => v.some(y => equals(y, x)))),
  objectContaining: o => asymmetric(`ObjectContaining ${inspect(o)}`, v => v !== null && typeof v == 'object' && Object.keys(o).every(k => k in v && equals(v[k], o[k]))),
  closeTo: (n, digits = 2) => asymmetric(`NumberCloseTo ${n}`, v => typeof v == 'number' && Math.abs(v - n) < 10 ** -digits / 2),
  extend(more) {
    Object.assign(custom, more);
  },
});

/* mock(fn) and spyOn(obj, name): call records plus mockReturnValue and friends */
export function mock(impl = () => {}) {
  let implementation = impl;
  const once = [];
  const f = function(...args) {
    f.mock.calls.push(args);
    f.mock.contexts.push(this);

    let result;

    try {
      const fn = once.length ? once.shift() : implementation;

      result = { type: 'return', value: new.target ? Reflect.construct(fn, args, new.target) : fn.apply(this, args) };
    } catch(e) {
      result = { type: 'throw', value: e };
    }

    f.mock.results.push(result);

    if(result.type == 'throw') throw result.value;

    return result.value;
  };

  f.mock = { calls: [], results: [], contexts: [] };
  f.mockClear = () => ((f.mock = { calls: [], results: [], contexts: [] }), f);
  f.mockReset = () => (f.mockClear(), (implementation = () => {}), (once.length = 0), f);
  f.mockRestore = () => f.mockReset();
  f.mockImplementation = fn => ((implementation = fn), f);
  f.mockImplementationOnce = fn => (once.push(fn), f);
  f.mockReturnValue = v => f.mockImplementation(() => v);
  f.mockReturnValueOnce = v => f.mockImplementationOnce(() => v);
  f.mockResolvedValue = v => f.mockImplementation(() => Promise.resolve(v));
  f.mockRejectedValue = v => f.mockImplementation(() => Promise.reject(v));
  f.getMockImplementation = () => implementation;

  return f;
}

export function spyOn(object, name) {
  const original = object[name];
  const spy = mock(function(...args) {
    return original.apply(this, args);
  });

  spy.mockRestore = () => {
    object[name] = original;
  };
  object[name] = spy;

  return spy;
}

export const jest = { fn: mock, spyOn };
export const vi = jest;

/* a function with the timeout of its test or hook */
function withTimeout(fn, ms, what) {
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(Object.assign(new Error(`${what} timed out after ${ms}ms`), { name: 'error' })), ms);
    const done = (ok, v) => {
      clearTimeout(timer);
      (ok ? resolve : reject)(v);
    };

    try {
      const r = fn.length ? new Promise((res, rej) => fn(e => (e ? rej(e) : res()))) : fn();

      Promise.resolve(r).then(v => done(true, v), e => done(false, e));
    } catch(e) {
      done(false, e);
    }
  });
}

const titleOf = item => {
  const parts = [];

  for(let s = item; s?.parent; s = s.parent) parts.unshift(s.name);

  return parts.join(' > ');
};

const stats = { pass: 0, fail: 0, skip: 0, todo: 0 };
const out = text => (globalThis.process?.stderr ? process.stderr.write(`${text}\n`) : console.error(text));

/* only one of the items, when any `only` exists, runs: an only item or an item under an only suite */
const wanted = (item, underOnly) => !hasOnly || underOnly || item.mode == 'only' || (item.kind == 'suite' && item.items.some(i => wanted(i, false)));

async function runHooks(list, what) {
  for(const hook of list) await withTimeout(hook, timeout, what);
}

async function runSuite(suite, inherited, underOnly) {
  const items = suite.items.filter(i => wanted(i, underOnly || suite.mode == 'only'));
  let started_ = false;
  const hooks = { before: [...inherited.before, ...suite.hooks.beforeEach], after: [...suite.hooks.afterEach, ...inherited.after] };

  for(const item of items) {
    if(item.kind == 'suite') {
      if(item.mode == 'skip' || suite.mode == 'skip') {
        countAll(item, item.mode == 'todo' ? 'todo' : 'skip');
        continue;
      }

      await runSuite(item, hooks, underOnly || suite.mode == 'only' || item.mode == 'only');
      continue;
    }

    const name = titleOf(item);

    if(item.mode == 'skip' || item.mode == 'todo' || suite.mode == 'skip') {
      stats[item.mode == 'todo' ? 'todo' : 'skip']++;
      out(`(${item.mode == 'todo' ? 'todo' : 'skip'}) ${name}`);
      continue;
    }

    if(!started_) {
      started_ = true;

      try {
        await runHooks(suite.hooks.beforeAll, `beforeAll of ${suite.name || 'file'}`);
      } catch(e) {
        report(name, e, 0);
        continue;
      }
    }

    const t0 = now();
    let error;

    try {
      await runHooks(inherited.before.concat(suite.hooks.beforeEach), `beforeEach of ${name}`);
      await withTimeout(item.fn ?? (() => {}), item.timeout ?? timeout, `test "${name}"`);
    } catch(e) {
      error = e;
    }

    try {
      await runHooks(suite.hooks.afterEach.concat(inherited.after), `afterEach of ${name}`);
    } catch(e) {
      error ??= e;
    }

    report(name, error, now() - t0);
  }

  if(started_)
    try {
      await runHooks(suite.hooks.afterAll, `afterAll of ${suite.name || 'file'}`);
    } catch(e) {
      stats.fail++;
      out(`(fail) afterAll of ${suite.name || 'file'}\n${e?.message ?? e}`);
    }
}

function countAll(suite, kind) {
  for(const item of suite.items) {
    if(item.kind == 'suite') countAll(item, kind);
    else {
      stats[kind]++;
      out(`(${kind}) ${titleOf(item)}`);
    }
  }
}

function report(name, error, ms) {
  const time = ` [${ms.toFixed(2)}ms]`;

  if(error) {
    stats.fail++;
    out(`${error?.message ?? error}${error?.stack && !error.expectFailure ? `\n${error.stack}` : ''}\n(fail) ${name}${time}`);
  } else {
    stats.pass++;
    out(`(pass) ${name}${time}`);
  }
}

async function run() {
  started = true;

  const t0 = now();
  const file = globalThis.process?.argv?.[1];

  if(file) out(`${file}:`);

  await runSuite(root, { before: [], after: [] }, false);

  const total = stats.pass + stats.fail + stats.skip + stats.todo;

  out('');
  if(stats.pass) out(` ${stats.pass} pass`);
  if(stats.skip) out(` ${stats.skip} skip`);
  if(stats.todo) out(` ${stats.todo} todo`);
  out(` ${stats.fail} fail`);
  out(` ${expectCalls} expect() calls`);
  out(`Ran ${total} test${total == 1 ? '' : 's'} across 1 file. [${(now() - t0).toFixed(2)}ms]`);

  globalThis.process?.exit(stats.fail ? 1 : 0);
}

export default { describe, test, it, expect, beforeAll, beforeEach, afterEach, afterAll, setDefaultTimeout, mock, spyOn, jest };
