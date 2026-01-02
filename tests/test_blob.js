import { ReadableStream } from 'stream';
import { assert_equals } from '../lib/testharnessreport.js';
import { assert_throws_js } from '../lib/testharnessreport.js';
import { assert_true } from '../lib/testharnessreport.js';
import { promise_test } from '../lib/testharnessreport.js';
import { test } from '../lib/testharnessreport.js';
import { Blob } from 'blob';
import { TextEncoder } from 'textcode';

test(() => {
  const blob = new Blob();

  assert_true(blob instanceof Blob);
}, 'Constructor creates a new Blob when called without arguments');

test(() => {
  const blob = new Blob();

  assert_equals(blob.size, 0);
}, 'Empty Blob returned by Blob constructor has the size of 0');

test(() => {
  const blob = new Blob();

  try {
    // @ts-expect-error expected for tests
    blob.size = 42;
  } catch {
    /* noop */
  }

  assert_equals(blob.size, 0);
}, 'The size property is read-only');

test(() => {
  const blob = new Blob();

  try {
    // @ts-expect-error expected for tests
    // biome-ignore lint/performance/noDelete: expected for tests
    delete blob.size;
  } catch {
    /* noop */
  }

  assert_true('size' in blob);
}, 'The size property cannot be removed');

test(() => {
  const blob = new Blob();

  assert_equals(blob.type, '');
}, 'Blob type is an empty string by default');

test(() => {
  const expected = 'text/plain';
  const blob = new Blob([], { type: expected });

  try {
    // @ts-expect-error expected for tests
    blob.type = 'application/json';
  } catch {
    /* noop */
  }

  assert_equals(blob.type, expected);
}, 'The type property is read-only');

test(() => {
  const blob = new Blob();

  try {
    // @ts-expect-error expected for tests
    // biome-ignore lint/performance/noDelete: expected for tests
    delete blob.type;
  } catch {
    /* noop */
  }

  assert_true('type' in blob);
}, 'The type property cannot be removed');

test(() => {
  const rounds = [null, true, false, 0, 1, 1.5, 'FAIL'];

  rounds.forEach(round => {
    // @ts-expect-error
    const trap = () => new Blob(round);

    assert_throws_js(TypeError, trap);

    /*t.throws(trap, {
      instanceOf: TypeError,
      message:
        "Failed to construct 'Blob': " +
        "The provided value cannot be converted to a sequence."
    })*/
  });
}, 'Constructor throws an error when first argument is not an object');

test(() => {
  // eslint-disable-next-line prefer-regex-literals
  const rounds = [new Date(), /(?:)/, {}, { 0: 'FAIL', length: 1 }];

  rounds.forEach(round => {
    // @ts-expect-error
    const trap = () => new Blob(round);

    assert_throws_js(TypeError, trap);

    /*t.throws(trap, {
      instanceOf: TypeError,
      message:
        "Failed to construct 'Blob': " +
        "The object must have a callable @@iterator property."
    })*/
  });
}, 'Constructor throws an error when first argument is not an iterable object');

promise_test(async () => {
  const source = ['one', 'two', 'three'];
  const blob = new Blob(source);

  assert_equals(await blob.text(), source.join(''));
}, 'Creates a new Blob from an array of strings');

promise_test(async () => {
  const encoder = new TextEncoder();
  const source = ['one', 'two', 'three'];

  const blob = new Blob(source.map(part => encoder.encode(part)));

  assert_equals(await blob.text(), source.join(''));
}, 'Creates a new Blob from an array of Uint8Array');

promise_test(async () => {
  const encoder = new TextEncoder();
  const source = ['one', 'two', 'three'];

  const blob = new Blob(source.map(part => encoder.encode(part).buffer));

  assert_equals(await blob.text(), source.join(''));
}, 'Creates a new Blob from an array of ArrayBuffer');

promise_test(async () => {
  const source = ['one', 'two', 'three'];

  const blob = new Blob(source.map(part => new Blob([part])));

  assert_equals(await blob.text(), source.join(''));
}, 'Creates a new Blob from an array of Blob');

promise_test(async () => {
  const expected = 'abc';

  // eslint-disable-next-line no-new-wrappers
  const blob = new Blob(new String(expected));

  assert_equals(await blob.text(), expected);
}, 'Accepts a String object as a sequence');

promise_test(async () => {
  const expected = [1, 2, 3];
  const blob = new Blob(new Uint8Array(expected));

  assert_equals(await blob.text(), expected.join(''));
}, 'Accepts Uint8Array as a sequence');

promise_test(async () => {
  const blob = new Blob({ [Symbol.iterator]: Array.prototype[Symbol.iterator] });

  assert_equals(blob.size, 0);
  assert_equals(await blob.text(), '');
}, 'Accepts iterable object as a sequence');

promise_test(async () => {
  const source = ['one', 'two', 'three'];
  const expected = source.join('');

  const blob = new Blob({
    *[Symbol.iterator]() {
      yield* source;
    },
  });

  assert_equals(blob.size, new TextEncoder().encode(expected).byteLength);
  assert_equals(await blob.text(), expected);
}, 'Constructor reads blobParts from iterable object');

test(() => {
  const source = ['one', 'two', 'three'];
  const expected = new TextEncoder().encode(source.join('')).byteLength;

  const blob = new Blob(source);

  assert_equals(blob.size, expected);
}, 'Blob has the size measured from the blobParts');

test(() => {
  const expected = 'text/markdown';

  const blob = new Blob(['Some *Markdown* content'], { type: expected });

  assert_equals(blob.type, expected);
}, 'Accepts type for Blob as an option in the second argument');

promise_test(async () => {
  const source = [
    null,
    undefined,
    true,
    false,
    0,
    1,

    // eslint-disable-next-line no-new-wrappers
    new String('string object'),

    [],
    { 0: 'FAIL', length: 1 },
    {
      toString() {
        return 'stringA';
      },
    },
    {
      toString: undefined,
      valueOf() {
        return 'stringB';
      },
    },
  ];

  const expected = source.map(element => String(element)).join('');

  const blob = new Blob(source);

  assert_equals(await blob.text(), expected);
}, 'Casts elements of the blobPart array to a string');

test(() => {
  const blob = new Blob([], undefined);

  assert_equals(blob.type, '');
}, 'undefined value has no affect on property bag argument');

test(() => {
  // @ts-expect-error Ignored, because that is what we are testing for
  const blob = new Blob([], null);

  assert_equals(blob.type, '');
}, 'null value has no affect on property bag argument');

test(() => {
  const blob = new Blob([], { type: '\u001Ftext/plain' });

  assert_equals(blob.type, '');
}, 'Invalid type in property bag will result in an empty string');

test(() => {
  const rounds = [123, 123.4, true, false, 'FAIL'];

  rounds.forEach(round => {
    // @ts-expect-error
    const trap = () => new Blob([], round);

    assert_throws_js(TypeError, trap);

    /*t.throws(trap, {
      instanceOf: TypeError,
      message: "Failed to construct 'Blob': " + 'parameter 2 cannot convert to dictionary.',
    });*/
  });
}, 'Throws an error if invalid property bag passed');

promise_test(async () => {
  const blob = new Blob(['a', 'b', 'c']);
  const sliced = blob.slice();

  assert_equals(sliced.size, blob.size);
  assert_equals(await sliced.text(), await blob.text());
}, '.slice() a new blob when called without arguments');

promise_test(async () => {
  const blob = new Blob(['a', 'b', 'c']);
  const sliced = blob.slice(0, 0);

  assert_equals(sliced.size, 0);
  assert_equals(await sliced.text(), '');
}, '.slice() an empty blob with the start and the end set to 0');

promise_test(async () => {
  const text = 'The MIT License';
  const blob = new Blob([text]).slice(0, 3);

  assert_equals(await blob.text(), 'The');
}, '.slice() slices the Blob within given range');

promise_test(async () => {
  const text = 'The MIT License';
  const blob = new Blob([text]).slice(4, 15);

  assert_equals(await blob.text(), 'MIT License');
}, '.slice() slices the Blob from arbitary start');

promise_test(async () => {
  const text = 'The MIT License';
  const blob = new Blob([text]).slice(-7);

  assert_equals(await blob.text(), 'License');
}, '.slice() slices the Blob from the end when start argument is negative');

promise_test(async () => {
  const text = 'The MIT License';
  const blob = new Blob([text]).slice(0, -8);

  assert_equals(await blob.text(), 'The MIT');
}, '.slice() slices the Blob from the start when end argument is negative');

promise_test(async () => {
  const text = 'The MIT License';
  const blob = new Blob([new Blob([text]), new Blob([text])]).slice(8, 18);

  assert_equals(await blob.text(), 'LicenseThe');
}, '.slice() slices Blob in blob parts');

promise_test(async () => {
  const blob = new Blob(['Hello', 'world']).slice(4, 7);

  assert_equals(await blob.text(), 'owo');
}, '.slice() slices within multiple parts');

promise_test(async () => {
  const blob = new Blob(['a', 'b', 'c']).slice(1, 2);

  assert_equals(await blob.text(), 'b');
}, '.slice() throws away unwanted parts');

test(() => {
  const expected = 'text/plain';
  const blob = new Blob([], { type: 'text/html' }).slice(0, 0, expected);

  assert_equals(blob.type, expected);
}, '.slice() takes type as the 3rd argument');

promise_test(async () => {
  const blob = new Blob(['a', new TextEncoder().encode('b'), new Blob(['c']), new TextEncoder().encode('d').buffer]);

  assert_equals(await blob.text(), 'abcd');
}, '.text() returns a the Blob content as string when awaited');

promise_test(async () => {
  const source = new TextEncoder().encode('abc');
  const blob = new Blob([source]);

  assert_equals(new Uint8Array(await blob.arrayBuffer()) + '', source + '');
}, '.arrayBuffer() returns the Blob content as ArrayBuffer when awaited');

/* TODO: stream() */

/*test(() => {
  const stream = new Blob().stream();

  assert_true(stream instanceof ReadableStream);
}, '.stream() returns ReadableStream');

promise_test(async () => {
  const source = Buffer.from("Some content")

  // ! Blob.stream() return type falls back to TypeScript typings for web which lacks Symbol.asyncIterator method, so we read stream with out readStream helper
  const actual = await buffer(readStream(new Blob([source]).stream()))

  assert_true(actual.equals(source))
}, ".stream() allows to read Blob as a stream")

promise_test(async () => {
  const stream = new Blob(['Some content']).stream();

  // Cancel the stream before start reading, or this will throw an error
  await stream.cancel();

  const reader = stream.getReader();

  const { done, value: chunk } = await reader.read();

  assert_true(done);
  assert_equals(chunk, undefined);
}, '.stream() returned ReadableStream can be cancelled');
*/