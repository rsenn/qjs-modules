/* bun:test of lib/bun/test.js: the matchers in process, the runner in a child qjsm on tests/fixtures/bun-test.
 * qjsm only: under Bun the real bun:test is used. */
import process from 'process';
import { Blob } from 'blob';
import { assert, eq, tests } from '../../lib/tinytest.js';
import { expect, mock, spyOn } from 'bun:test';

const Bun = globalThis.Bun ?? (await import('../../lib/bun.js')).default;

const fails = fn => {
  try {
    fn();
  } catch(e) {
    return e.message;
  }

  return null;
};

/* the stderr, stdout and exit code of a qjsm running a fixture */
async function run(name) {
  const p = Bun.spawn([process.execPath, `tests/fixtures/bun-test/${name}.js`], { stdout: 'pipe', stderr: 'pipe' });
  const read = async s => {
    const chunks = [];

    for await(const c of s) chunks.push(...c);

    return new TextDecoder().decode(new Uint8Array(chunks));
  };
  const [out, err] = await Promise.all([read(p.stdout), read(p.stderr)]);

  return { out, err, code: await p.exited };
}

tests({
  'equality: toBe, toEqual, toStrictEqual'() {
    expect(1).toBe(1);
    expect({ a: 1, b: [1, 2] }).toEqual({ a: 1, b: [1, 2] });
    expect({ a: 1, b: undefined }).toEqual({ a: 1 });
    expect(fails(() => expect({ a: 1, b: undefined }).toStrictEqual({ a: 1 }))).not.toBeNull();
    expect(new Map([[1, { x: 1 }]])).toEqual(new Map([[1, { x: 1 }]]));
    expect(new Set([1, 2])).toEqual(new Set([2, 1]));
    expect(new Date(5)).toEqual(new Date(5));
    expect(new Uint8Array([1, 2])).toEqual(new Uint8Array([1, 2]));
    expect(fails(() => expect(1).toBe(2))).toMatch('Expected: 2');
    expect(fails(() => expect(1).not.toBe(1))).toMatch('expect(received).not.toBe');
  },

  'comparison and truthiness'() {
    expect(3).toBeGreaterThan(2);
    expect(2).toBeLessThanOrEqual(2);
    expect(0.1 + 0.2).toBeCloseTo(0.3);
    expect(null).toBeNull();
    expect(undefined).toBeUndefined();
    expect(NaN).toBeNaN();
    expect('x').toBeTruthy();
    expect('').toBeFalsy();
    expect(fails(() => expect(0.5).toBeCloseTo(0.3))).not.toBeNull();
  },

  'strings, arrays, objects'() {
    expect('hello world').toContain('lo w');
    expect([1, 2, 3]).toContain(2);
    expect([{ a: 1 }]).toContainEqual({ a: 1 });
    expect('abc').toMatch(/b/);
    expect('abc').toStartWith('ab');
    expect('abc').toEndWith('bc');
    expect([1, 2]).toHaveLength(2);
    expect({ a: { b: [5] } }).toHaveProperty('a.b[0]', 5);
    expect({ a: 1, b: { c: 2, d: 3 } }).toMatchObject({ b: { c: 2 } });
    expect(fails(() => expect({ a: 1 }).toMatchObject({ a: 2 }))).not.toBeNull();
    expect(new Error('x')).toBeInstanceOf(Error);
    expect(5).toBeTypeOf('number');
  },

  'asymmetric matchers'() {
    expect({ a: 1, b: 'x', c: [1, 2] }).toEqual({ a: expect.any(Number), b: expect.stringMatching(/x/), c: expect.arrayContaining([2]) });
    expect({ a: 1 }).toEqual(expect.objectContaining({ a: 1 }));
    expect(0.30000000000000004).toEqual(expect.closeTo(0.3));
    expect(fails(() => expect({ a: 'x' }).toEqual({ a: expect.any(Number) }))).not.toBeNull();
  },

  'toThrow with nothing, a string, a regexp, a class'() {
    const boom = () => {
      throw new TypeError('bad thing');
    };

    expect(boom).toThrow();
    expect(boom).toThrow('bad');
    expect(boom).toThrow(/thing$/);
    expect(boom).toThrow(TypeError);
    expect(() => {}).not.toThrow();
    expect(fails(() => expect(() => {}).toThrow())).toMatch('did not throw');
  },

  async 'resolves and rejects'() {
    await expect(Promise.resolve(5)).resolves.toBe(5);
    await expect(Promise.reject(new Error('no'))).rejects.toThrow('no');
    await expect(Promise.reject(new Error('no'))).rejects.toBeInstanceOf(Error);
  },

  'mock and spyOn'() {
    const f = mock(x => x * 2);

    eq(4, f(2));
    expect(f).toHaveBeenCalledTimes(1);
    expect(f).toHaveBeenCalledWith(2);
    f.mockReturnValue(9);
    eq(9, f(1));
    eq('2,1', f.mock.calls.map(c => c[0]).join());

    const o = { m: () => 'real' };
    const s = spyOn(o, 'm');

    eq('real', o.m());
    expect(s).toHaveBeenCalled();
    s.mockRestore();
    eq('real', o.m());
  },

  async 'the runner: pass, fail, skip, todo, hooks, each and timeout in order'() {
    const { out, err, code } = await run('mixed');

    eq(1, code);
    assert(err.includes('(pass) passes'), 'pass');
    assert(err.includes('(fail) fails'), 'fail');
    assert(err.includes('(skip) skipped'), 'skip');
    assert(err.includes('(todo) todo'), 'todo');
    assert(err.includes('(pass) group > async passes'), 'nested async');
    assert(err.includes('(pass) group > adds 1 and 2'), 'each title');
    assert(err.includes('(pass) group > adds 2 and 3'), 'each title 2');
    assert(/\(fail\) times out/.test(err), 'timeout fails');
    assert(err.includes('timed out after 50ms'), 'timeout message');
    assert(/ 4 pass\n 1 skip\n 1 todo\n 2 fail/.test(err), err);
    eq('LOG beforeAll,be,ae,be,ae,be,inner,ae,be,inner,ae,be,inner,ae,be,ae', out.trim().replace(/^LOG /, 'LOG '));
  },

  async 'only: just the only tests and suites run'() {
    const { err, code } = await run('only');

    eq(0, code);
    assert(!err.includes('not run'), 'not run is absent');
    assert(err.includes('(pass) run') && err.includes('(pass) suite > also run'), err);
  },
});
