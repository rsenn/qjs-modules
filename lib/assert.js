import { equals } from 'deep';
import { inspect } from 'inspect';

function assert(cond, message) {
  if(!cond) throw new AssertionError(undefined, message);
}

class AssertionError extends Error {
  constructor(fn, message) {
    super('assert' + (fn ? '.' : '') + fn + '()' + (message ? ': ' : '') + (message ?? ''));
    this.fn = fn;
  }
}

Object.defineProperty(AssertionError.prototype, Symbol.toStringTag, {
  value: 'AssertionError',
});

Object.assign(assert, {
  AssertionError,
  deepEqual(actual, expected, message) {
    if(!equals(actual, expected))
      throw new AssertionError(
        `deepEqual`,
        message ?? `Expected values to be loosely deep-equal:\x1b[0m\n\n${inspect(actual, { colors: false })}\n\nshould loosely deep-equal\n\n${inspect(expected, { colors: false })}`,
      );
  },
  deepStrictEqual(actual, expected, message) {
    if(!equals(actual, expected))
      throw new AssertionError(
        `deepEqual`,
        message ?? `Expected values to be strictly deep-equal:\x1b[0m\n\n${inspect(actual, { colors: false })}\n\nshould loosely deep-equal\n\n${inspect(expected, { colors: false })}`,
      );
  },
  doesNotMatch(string, regexp, message) {
    if(regexp.test(string))
      throw new AssertionError(`doesNotMatch`, message ?? `The input was expected to not match the regular expression ${regexp}. Input:\n\n${inspect(string, { colors: false })}\n\n`);
  },
  doesNotReject(asyncFn, error, message) {
    return asyncFn.then(
      r => r,
      e => {
        throw error ?? new AssertionError(`doesNotReject`, message ?? `Got unwanted rejection.\nActual message: "${e.message}"\n`);
      },
    );
  },
  doesNotThrow(fn, error, message) {
    try {
      fn();
    } catch(e) {
      throw error ?? new AssertionError(`doesNotReject`, message ?? `Got unwanted exception.\nActual message: "${e.message}"\n`);
    }
  },
  equal(actual, expected, message) {
    if(actual != expected) throw new AssertionError(`equal`, message ?? inspect(actual, { colors: false }) + ' == ' + inspect(expected, { colors: false }));
  },
  fail(message) {
    if(typeof message == 'object') throw message;
    else throw new AssertionError(`fail`, message ?? 'Failed');
  },
  ifError(value) {
    if(value === undefined || value === null) return;

    throw new AssertionError(`ifError`, `got unwanted exception: ` + (value?.message ?? value));
  },
  match(string, regexp, message) {
    if(!regexp.test(string)) throw new AssertionError(`match`, message ?? `The input did not match the regular expression ${regexp}. Input:\n\n${inspect(string, { colors: false })}\n\n`);
  },
  notDeepEqual(actual, expected, message) {
    if(equals(actual, expected)) throw new AssertionError(`notDeepEqual`, message ?? `Expected "actual" not to be loosely deep-equal to:\n\n${inspect(expected, { colors: false })}\n`);
  },
  notDeepStrictEqual(actual, expected, message) {
    if(equals(actual, expected)) throw new AssertionError(`notDeepStrictEqual`, message ?? `Expected "actual" not to be strictly deep-equal to:\n\n${inspect(expected, { colors: false })}\n`);
  },
  notEqual(actual, expected, message) {
    if(actual == expected) throw new AssertionError(`notEqual`, message ?? inspect(actual, { colors: false }) + ' != ' + inspect(expected, { colors: false }));
  },
  notStrictEqual(actual, expected, message) {
    if(actual === expected) throw new AssertionError(`notStrictEqual`, message ?? `Expected "actual" to be strictly unequal to: ${inspect(expected, { colors: false })}`);
  },
  ok(value, message) {
    if(!value) throw new AssertionError(`ok`, message ?? `${inspect(value, { colors: false })} == true`);
  },
  rejects(asyncFn, error, message) {
    return asyncFn.then(
      r => {
        throw error ?? new AssertionError(`rejects`, message ?? `Missing expected rejection.`);
      },
      e => e,
    );
  },
  strictEqual(actual, expected, message) {
    if(actual !== expected)
      throw new AssertionError(`strictEqual`, message ?? `Expected values to be strictly equal: ${inspect(actual, { colors: false })} === ${inspect(expected, { colors: false })}`);
  },
  throws(fn, error, message) {
    try {
      fn();
    } catch(e) {
      return;
    }
    throw error ?? new AssertionError(`throws`, message ?? `Missing expected exception.`);
  },
  partialDeepStrictEqual(actual, expected, message) {
    /* XXX: implement */
    throw new AssertionError(`partialDeepStrictEqual`, message);
  },
});

export default assert;
