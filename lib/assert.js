/* assert.js: Node's `assert` (AssertionError, assert.*, assert.strict,
 * assert.Assert, assert/strict).
 *
 * ```js
 * import assert from 'assert';
 * assert.strictEqual(1, 2);   // AssertionError [ERR_ASSERTION]
 * ```
 *
 * not implemented: CallTracker (deprecated in Node).
 */
import { inspect as inspect_ } from 'inspect';

const inspect = (v, o) => inspect_(v, { colors: false, ...o });
const { getPrototypeOf, keys, getOwnPropertySymbols, prototype: ObjectProto } = Object;
const toStr = v => Object.prototype.toString.call(v);

/* diffLines: '+ actual - expected' view of two multi-line inspections */
const diffLines = (actual, expected) => {
  const a = actual.split('\n'),
    b = expected.split('\n');
  const out = [];
  const n = Math.max(a.length, b.length);
  for(let i = 0; i < n; i++) {
    if(a[i] === b[i]) out.push(`  ${a[i]}`);
    else {
      if(i < a.length) out.push(`+ ${a[i]}`);
      if(i < b.length) out.push(`- ${b[i]}`);
    }
  }
  return `+ actual - expected\n\n${out.join('\n')}`;
};

const compare = (header, actual, expected, op) => {
  const a = inspect(actual, { compact: false, depth: 1000 }),
    b = inspect(expected, { compact: false, depth: 1000 });
  if(!a.includes('\n') && !b.includes('\n') && (typeof actual != 'object' || actual === null) && (typeof expected != 'object' || expected === null)) return `${header}\n\n${a} ${op} ${b}\n`;
  return `${header}\n${diffLines(a, b)}\n`;
};

/* AssertionError(options): options = { message, actual, expected, operator } */
export class AssertionError extends Error {
  constructor(options) {
    if(options === null || typeof options != 'object') {
      const e = new TypeError(`The "options" argument must be of type object. Received ${options === null ? 'null' : `type ${typeof options} (${inspect(options)})`}`);
      e.code = 'ERR_INVALID_ARG_TYPE';
      throw e;
    }
    const { message, actual, expected, operator } = options;
    super(message !== undefined ? String(message) : `${inspect(actual)} ${operator} ${inspect(expected)}`);
    Object.defineProperty(this, 'name', { value: 'AssertionError', writable: true, configurable: true });
    this.generatedMessage = message === undefined;
    this.code = 'ERR_ASSERTION';
    this.actual = actual;
    this.expected = expected;
    this.operator = operator;
    if(Error.captureStackTrace) Error.captureStackTrace(this, options.stackStartFn ?? AssertionError);
  }

  toString() {
    return `${this.name} [${this.code}]: ${this.message}`;
  }
}

Object.defineProperty(AssertionError.prototype, Symbol.toStringTag, { value: 'AssertionError', configurable: true });

/* innerFail: throws message as is when it is an Error, else a new AssertionError */
const innerFail = obj => {
  if(obj.message instanceof Error) throw obj.message;
  const hasMsg = obj.message !== undefined;
  const e = new AssertionError({ ...obj, message: hasMsg ? obj.message : obj.generated });
  e.generatedMessage = !hasMsg;
  throw e;
};

const argError = (name, expectedType, value) => {
  const e = new TypeError(`The "${name}" argument must be ${expectedType}. Received ${value === null ? 'null' : value === undefined ? 'undefined' : typeof value == 'object' ? `an instance of ${value.constructor?.name ?? 'Object'}` : `type ${typeof value} (${inspect(value)})`}`);
  e.code = 'ERR_INVALID_ARG_TYPE';
  return e;
};

const missingArgs = (...names) => {
  const e = new TypeError(`The ${names.map(n => `"${n}"`).join(' and ')} argument${names.length > 1 ? 's' : ''} must be specified`);
  e.code = 'ERR_MISSING_ARGS';
  return e;
};

/* deep comparison: strict = Object.is + prototypes + symbols, else == */
const isObj = v => v !== null && typeof v == 'object';

const sameBytes = (a, b) => {
  const x = new Uint8Array(a.buffer ?? a, a.byteOffset ?? 0, a.byteLength),
    y = new Uint8Array(b.buffer ?? b, b.byteOffset ?? 0, b.byteLength);
  if(x.length != y.length) return false;
  for(let i = 0; i < x.length; i++) if(x[i] !== y[i]) return false;
  return true;
};

const ownKeys = (o, strict) => {
  const k = keys(o);
  if(strict) for(const s of getOwnPropertySymbols(o)) if(Object.prototype.propertyIsEnumerable.call(o, s)) k.push(s);
  return k;
};

function isDeepEqual(a, b, strict, memo = new Map(), skipProto = false) {
  if(strict ? Object.is(a, b) : a == b || (a !== a && b !== b)) return true;
  if(!isObj(a) || !isObj(b)) return !strict && !isObj(a) && !isObj(b) ? a == b || (a !== a && b !== b) : false;

  if(strict) {
    if(!skipProto && getPrototypeOf(a) !== getPrototypeOf(b)) return false;
    if(toStr(a) !== toStr(b)) return false;
  } else if(toStr(a) !== toStr(b)) return false;

  if(memo.get(a)?.has(b)) return true;
  (memo.get(a) ?? memo.set(a, new Set()).get(a)).add(b);

  if(Array.isArray(a)) {
    if(a.length !== b.length) return false;
  } else if(a instanceof Date) {
    if(a.getTime() !== b.getTime() && !(isNaN(a) && isNaN(b))) return false;
  } else if(a instanceof RegExp) {
    if(a.source !== b.source || a.flags !== b.flags || a.lastIndex !== b.lastIndex) return false;
  } else if(a instanceof Error) {
    if(a.message !== b.message || a.name !== b.name) return false;
  } else if(ArrayBuffer.isView(a)) {
    if(strict) {
      if(!sameBytes(a, b)) return false;
    } else {
      if(a.length !== b.length) return false;
      for(let i = 0; i < a.length; i++) if(!isDeepEqual(a[i], b[i], false, memo)) return false;
    }
  } else if(a instanceof ArrayBuffer || (typeof SharedArrayBuffer != 'undefined' && a instanceof SharedArrayBuffer)) {
    if(!sameBytes(a, b)) return false;
  } else if(a instanceof Number || a instanceof String || a instanceof Boolean || a instanceof BigInt || a instanceof Symbol) {
    if(!Object.is(a.valueOf(), b.valueOf())) return false;
  } else if(a instanceof Set) {
    if(a.size !== b.size) return false;
    const rest = [...b];
    for(const v of a) {
      const i = rest.findIndex(w => isDeepEqual(v, w, strict, memo, skipProto));
      if(i == -1) return false;
      rest.splice(i, 1);
    }
  } else if(a instanceof Map) {
    if(a.size !== b.size) return false;
    const rest = [...b];
    for(const [k, v] of a) {
      const i = rest.findIndex(([k2, v2]) => isDeepEqual(k, k2, strict, memo, skipProto) && isDeepEqual(v, v2, strict, memo, skipProto));
      if(i == -1) return false;
      rest.splice(i, 1);
    }
  }

  if(ArrayBuffer.isView(a) && !strict) return true;
  const ka = ownKeys(a, strict),
    kb = ownKeys(b, strict);
  if(ka.length !== kb.length) return false;
  for(const k of ka) {
    if(!Object.prototype.hasOwnProperty.call(b, k)) return false;
    if(!isDeepEqual(a[k], b[k], strict, memo, skipProto)) return false;
  }
  return true;
}

function partialDeepEqual(actual, expected, memo = new Set()) {
  if(Object.is(actual, expected)) return true;
  if(!isObj(expected) || !isObj(actual)) return false;
  if(getPrototypeOf(actual) !== getPrototypeOf(expected)) return false;
  if(memo.has(expected)) return true;
  memo.add(expected);

  if(Array.isArray(expected)) {
    if(!Array.isArray(actual) || actual.length < expected.length) return false;
    // expected must be a subsequence in order
    let j = 0;
    for(const v of expected) {
      while(j < actual.length && !partialDeepEqual(actual[j], v, memo)) j++;
      if(j++ >= actual.length) return false;
    }
    return true;
  }
  if(expected instanceof Set) {
    const rest = [...actual];
    for(const v of expected) {
      const i = rest.findIndex(w => partialDeepEqual(w, v, memo));
      if(i == -1) return false;
      rest.splice(i, 1);
    }
    return true;
  }
  if(expected instanceof Map) {
    for(const [k, v] of expected) if(!actual.has(k) || !partialDeepEqual(actual.get(k), v, memo)) return false;
    return true;
  }
  if(expected instanceof Date || expected instanceof RegExp || expected instanceof Error || ArrayBuffer.isView(expected)) return isDeepEqual(actual, expected, true);

  for(const key of Reflect.ownKeys(expected)) if(!(key in actual) || !partialDeepEqual(actual[key], expected[key], memo)) return false;
  return true;
}

/* expected-error validation for throws()/rejects() */
const compareExceptionKey = (actual, expected, key, message, keys, fn) => {
  if(!(key in actual) || !isDeepEqual(actual[key], expected[key], true)) {
    if(!message) {
      const a = {},
        b = {};
      for(const k of keys) {
        if(k in actual) a[k] = actual[k];
        b[k] = expected[k];
      }
      innerFail({ actual: a, expected: b, operator: fn.name, generated: compare('Expected values to be strictly deep-equal:', a, b, '!==') });
    }
    innerFail({ actual, expected, message, operator: fn.name });
  }
};

function expectedException(actual, expected, message, fn) {
  let generatedMessage = false;
  let throwError = false;

  if(typeof expected != 'function') {
    if(expected instanceof RegExp) {
      const str = String(actual);
      if(expected.exec(str) !== null) return;
      if(!message) {
        generatedMessage = true;
        message = `The input did not match the regular expression ${inspect(expected)}. Input:\n\n${inspect(str)}\n`;
      }
      throwError = true;
    } else if(isObj(actual) || typeof actual == 'function') {
      const ks = keys(expected);
      if(expected instanceof Error) ks.push('name', 'message');
      else if(ks.length === 0) throw argError('error', 'of type function or an instance of Error, RegExp, or Object', expected);
      for(const key of ks) {
        if(typeof actual[key] == 'string' && expected[key] instanceof RegExp && expected[key].exec(actual[key]) !== null) continue;
        compareExceptionKey(actual, expected, key, message, ks, fn);
      }
      return;
    } else {
      throw argError('error', 'of type function or an instance of Error, RegExp, or Object', expected);
    }
  } else if(expected.prototype !== undefined && actual instanceof expected) {
    return;
  } else if(Error.isPrototypeOf(expected)) {
    if(!message) {
      generatedMessage = true;
      message = `The error is expected to be an instance of "${expected.name}". Received `;
      message += actual instanceof Error ? `"${actual.name}"` : inspect(actual);
      if (actual?.message) message += `\n\nError message:\n\n${actual.message}`;
    }
    throwError = true;
  } else {
    const res = expected.call({}, actual);
    if(res !== true) {
      if(!message) {
        generatedMessage = true;
        message = `The ${expected.name ? `"${expected.name}" ` : ''}validation function is expected to return "true". Received ${inspect(res)}\n\nCaught error:\n\n${actual}`;
      }
      throwError = true;
    }
  }

  if(throwError) {
    const e = new AssertionError({ actual, expected, message, operator: fn.name, stackStartFn: fn });
    e.generatedMessage = generatedMessage;
    throw e;
  }
}

const NO_EXCEPTION = Symbol('no exception');

function getActual(fn) {
  if(typeof fn != 'function') throw argError('fn', 'of type function', fn);
  try {
    fn();
  } catch(e) {
    return e;
  }
  return NO_EXCEPTION;
}

async function waitForActual(promiseFn) {
  let resultPromise;
  if(typeof promiseFn == 'function') {
    resultPromise = promiseFn();
    if(!resultPromise || typeof resultPromise.then != 'function') {
      const e = new TypeError(`Expected instance of Promise to be returned from the "promiseFn" function but got ${resultPromise === undefined ? 'undefined' : `type ${typeof resultPromise}`}.`);
      e.code = 'ERR_INVALID_RETURN_VALUE';
      throw e;
    }
  } else if(promiseFn && typeof promiseFn.then == 'function') {
    resultPromise = promiseFn;
  } else {
    throw argError('promiseFn', 'of type function or an instance of Promise', promiseFn);
  }
  try {
    await resultPromise;
  } catch(e) {
    return e;
  }
  return NO_EXCEPTION;
}

function expectsError(stackStartFn, actual, error, message) {
  if(typeof error == 'string') {
    if(arguments.length === 4) throw argError('error', 'of type function or an instance of Error, RegExp, or Object', error);
    if(isObj(actual) && actual.message === error) {
      const e = new TypeError(`The "error/message" argument is ambiguous. The error message "${actual.message}" is identical to the message.`);
      e.code = 'ERR_AMBIGUOUS_ARGUMENT';
      throw e;
    }
    message = error;
    error = undefined;
  } else if(error != null && typeof error != 'object' && typeof error != 'function') {
    throw argError('error', 'of type function or an instance of Error, RegExp, or Object', error);
  }

  if(actual === NO_EXCEPTION) {
    let details = '';
    if(error?.name) details += ` (${error.name})`;
    details += message ? `: ${message}` : '.';
    const fnType = stackStartFn === assertRejects ? 'rejection' : 'exception';
    innerFail({ actual: undefined, expected: error, operator: stackStartFn.name, message: undefined, generated: `Missing expected ${fnType}${details}`, ...(message && { message: undefined }) });
  }

  if(!error) return;
  expectedException(actual, error, message, stackStartFn);
}

function expectsNoError(stackStartFn, actual, error, message) {
  if(actual === NO_EXCEPTION) return;

  if(typeof error == 'string') {
    message = error;
    error = undefined;
  }

  if(!error || (actual != null && typeof error == 'function' ? actual instanceof error : error instanceof RegExp ? error.test(String(actual)) : false)) {
    const details = message ? `: ${message}` : '.';
    const fnType = stackStartFn === assertDoesNotReject ? 'rejection' : 'exception';
    innerFail({ actual, expected: error, operator: stackStartFn.name, generated: `Got unwanted ${fnType}${details}\nActual message: "${actual?.message}"` });
  }
  throw actual;
}

/* the assertion functions, built per mode (loose / strict) */
function assertOk(...args) {
  if(args.length === 0) {
    const e = new AssertionError({ message: 'No value argument passed to `assert.ok()`', operator: '==', actual: undefined, expected: true });
    e.generatedMessage = true;
    throw e;
  }
  if(!args[0]) innerFail({ actual: args[0], expected: true, message: args[1], operator: '==', generated: `The expression evaluated to a falsy value:\n\n  assert.ok(${inspect(args[0])})\n` });
}

export function assert(...args) {
  if(args.length === 0) return assertOk();
  if(!args[0]) innerFail({ actual: args[0], expected: true, message: args[1], operator: '==', generated: `The expression evaluated to a falsy value:\n\n  assert(${inspect(args[0])})\n` });
}

export const ok = assertOk;

export function fail(message) {
  if(message instanceof Error) throw message;
  const e = new AssertionError({ message: message ?? 'Failed', operator: 'fail' });
  e.generatedMessage = message === undefined;
  throw e;
}

export function equal(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(!(actual == expected || (actual !== actual && expected !== expected))) innerFail({ actual, expected, message, operator: '==', generated: `${inspect(actual)} == ${inspect(expected)}` });
}

export function notEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(actual == expected || (actual !== actual && expected !== expected)) innerFail({ actual, expected, message, operator: '!=', generated: `${inspect(actual)} != ${inspect(expected)}` });
}

export function strictEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(!Object.is(actual, expected)) innerFail({ actual, expected, message, operator: 'strictEqual', generated: compare('Expected values to be strictly equal:', actual, expected, '!==') });
}

export function notStrictEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(Object.is(actual, expected)) innerFail({ actual, expected, message, operator: 'notStrictEqual', generated: `Expected "actual" to be strictly unequal to: ${inspect(expected)}` });
}

export function deepEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(!isDeepEqual(actual, expected, false)) innerFail({ actual, expected, message, operator: 'deepEqual', generated: compare('Expected values to be loosely deep-equal:', actual, expected, '!=') });
}

export function notDeepEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(isDeepEqual(actual, expected, false)) innerFail({ actual, expected, message, operator: 'notDeepEqual', generated: `Expected "actual" not to be loosely deep-equal to: ${inspect(expected)}` });
}

export function deepStrictEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(!isDeepEqual(actual, expected, true)) innerFail({ actual, expected, message, operator: 'deepStrictEqual', generated: compare('Expected values to be strictly deep-equal:', actual, expected, '!==') });
}

export function notDeepStrictEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(isDeepEqual(actual, expected, true)) innerFail({ actual, expected, message, operator: 'notDeepStrictEqual', generated: `Expected "actual" not to be strictly deep-equal to: ${inspect(expected)}` });
}

export function partialDeepStrictEqual(actual, expected, message) {
  if(arguments.length < 2) throw missingArgs('actual', 'expected');
  if(!partialDeepEqual(actual, expected)) innerFail({ actual, expected, message, operator: 'partialDeepStrictEqual', generated: compare('Expected values to be partially and strictly deep-equal:', actual, expected, '!==') });
}

export function ifError(err) {
  if(err === null || err === undefined) return;
  let message = 'ifError got unwanted exception: ';
  message += typeof err == 'object' && typeof err.message == 'string' ? (err.message.length === 0 && err.constructor ? err.constructor.name : err.message) : inspect(err);
  const e = new AssertionError({ actual: err, expected: null, operator: 'ifError', message });
  e.generatedMessage = false;
  throw e;
}

function internalMatch(string, regexp, message, fn) {
  if(!(regexp instanceof RegExp)) throw argError('regexp', 'an instance of RegExp', regexp);
  const match = fn === match_;
  if(typeof string !== 'string' || (regexp.exec(string) !== null) !== match) {
    if(message instanceof Error) throw message;
    const generatedMessage = !message;
    message ||= typeof string !== 'string' ? `The "string" argument must be of type string. Received type ${typeof string} (${inspect(string)})` : `${match ? 'The input did not match the regular expression ' : 'The input was expected to not match the regular expression '}${inspect(regexp)}. Input:\n\n${inspect(string)}\n`;
    const e = new AssertionError({ actual: string, expected: regexp, message, operator: fn.name });
    e.generatedMessage = generatedMessage;
    throw e;
  }
}

function match_(string, regexp, message) {
  internalMatch(string, regexp, message, match_);
}
function doesNotMatch_(string, regexp, message) {
  internalMatch(string, regexp, message, doesNotMatch_);
}
Object.defineProperty(match_, 'name', { value: 'match' });
Object.defineProperty(doesNotMatch_, 'name', { value: 'doesNotMatch' });
export { match_ as match, doesNotMatch_ as doesNotMatch };

function assertThrows(fn, ...args) {
  expectsError(assertThrows, getActual(fn), ...args);
}
export { assertThrows as throws };

function assertDoesNotThrow(fn, ...args) {
  expectsNoError(assertDoesNotThrow, getActual(fn), ...args);
}
export { assertDoesNotThrow as doesNotThrow };

async function assertRejects(promiseFn, ...args) {
  expectsError(assertRejects, await waitForActual(promiseFn), ...args);
}
export { assertRejects as rejects };

async function assertDoesNotReject(promiseFn, ...args) {
  expectsNoError(assertDoesNotReject, await waitForActual(promiseFn), ...args);
}
export { assertDoesNotReject as doesNotReject };

const members = { AssertionError, deepEqual, deepStrictEqual, doesNotMatch: doesNotMatch_, doesNotReject: assertDoesNotReject, doesNotThrow: assertDoesNotThrow, equal, fail, ifError, match: match_, notDeepEqual, notDeepStrictEqual, notEqual, notStrictEqual, ok, partialDeepStrictEqual, rejects: assertRejects, strictEqual, throws: assertThrows };

Object.assign(assert, members);

/* strict mode: equal/deepEqual/notEqual/notDeepEqual alias the strict versions */
const strictMembers = { ...members, equal: strictEqual, notEqual: notStrictEqual, deepEqual: deepStrictEqual, notDeepEqual: notDeepStrictEqual };

export const strict = Object.assign((...args) => assert(...args), strictMembers);
strict.strict = strict;
assert.strict = strict;

/* Assert: class form with options, `new Assert({ diff: 'full' })` */
export class Assert {
  constructor(options = {}) {
    if(options === null || typeof options != 'object') throw argError('options', 'of type object', options);
    const { diff = 'simple', strict: isStrict = false } = options;
    if(diff !== 'simple' && diff !== 'full') {
      const e = new TypeError(`The property 'options.diff' must be one of: 'simple', 'full'. Received ${inspect(diff)}`);
      e.code = 'ERR_INVALID_ARG_VALUE';
      throw e;
    }
    this.options = { diff, strict: isStrict };
    const src = isStrict ? strictMembers : members;
    for(const [name, fn] of Object.entries(src)) if(name != 'AssertionError') Object.defineProperty(this, name, { value: fn, writable: true, configurable: true });
  }
}

assert.Assert = Assert;
strict.Assert = Assert;

export { isDeepEqual, partialDeepEqual };

export function noop() {}
export const assert_default = assert;

export default assert;
