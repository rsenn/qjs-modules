import { ReadableStream } from 'stream';
import { Blob } from 'blob';
import { TextEncoder } from 'textcode';
import { assert, assertStrictEquals, tests } from '../../lib/tinytest.js';

const equals = (actual, expected) => assertStrictEquals(expected, actual);
const isTrue = value => assert(value === true, `expected true, got ${value}`);

function throwsJs(Ctor, fn) {
  try {
    fn();
  } catch(e) {
    if(e instanceof Ctor) return;

    throw new Error(`expected ${Ctor.name}, got ${e?.constructor?.name}: ${e?.message}`);
  }

  throw new Error(`expected ${Ctor.name}, nothing thrown`);
}

tests({
  'Constructor creates a new Blob when called without arguments'() {
    const blob = new Blob();

    isTrue(blob instanceof Blob);
  },

  'Empty Blob returned by Blob constructor has the size of 0'() {
    const blob = new Blob();

    equals(blob.size, 0);
  },

  'The size property is read-only'() {
    const blob = new Blob();

    try {
      // @ts-expect-error expected for tests
      blob.size = 42;
    } catch {
      /* noop */
    }

    equals(blob.size, 0);
  },

  'The size property cannot be removed'() {
    const blob = new Blob();

    try {
      // @ts-expect-error expected for tests
      // biome-ignore lint/performance/noDelete: expected for tests
      delete blob.size;
    } catch {
      /* noop */
    }

    isTrue('size' in blob);
  },

  'Blob type is an empty string by default'() {
    const blob = new Blob();

    equals(blob.type, '');
  },

  'The type property is read-only'() {
    const expected = 'text/plain';
    const blob = new Blob([], { type: expected });

    try {
      // @ts-expect-error expected for tests
      blob.type = 'application/json';
    } catch {
      /* noop */
    }

    equals(blob.type, expected);
  },

  'The type property cannot be removed'() {
    const blob = new Blob();

    try {
      // @ts-expect-error expected for tests
      // biome-ignore lint/performance/noDelete: expected for tests
      delete blob.type;
    } catch {
      /* noop */
    }

    isTrue('type' in blob);
  },

  'Constructor throws an error when first argument is not an object'() {
    const rounds = [null, true, false, 0, 1, 1.5, 'FAIL'];

    rounds.forEach(round => {
      // @ts-expect-error
      const trap = () => new Blob(round);

      throwsJs(TypeError, trap);

      /*t.throws(trap, {
        instanceOf: TypeError,
        message:
          "Failed to construct 'Blob': " +
          "The provided value cannot be converted to a sequence."
      })*/
    });
  },

  'Constructor throws an error when first argument is not an iterable object'() {
    // eslint-disable-next-line prefer-regex-literals
    const rounds = [new Date(), /(?:)/, {}, { 0: 'FAIL', length: 1 }];

    rounds.forEach(round => {
      // @ts-expect-error
      const trap = () => new Blob(round);

      throwsJs(TypeError, trap);

      /*t.throws(trap, {
        instanceOf: TypeError,
        message:
          "Failed to construct 'Blob': " +
          "The object must have a callable @@iterator property."
      })*/
    });
  },

  async 'Creates a new Blob from an array of strings'() {
    const source = ['one', 'two', 'three'];
    const blob = new Blob(source);

    equals(await blob.text(), source.join(''));
  },

  async 'Creates a new Blob from an array of Uint8Array'() {
    const encoder = new TextEncoder();
    const source = ['one', 'two', 'three'];

    const blob = new Blob(source.map(part => encoder.encode(part)));

    equals(await blob.text(), source.join(''));
  },

  async 'Creates a new Blob from an array of ArrayBuffer'() {
    const encoder = new TextEncoder();
    const source = ['one', 'two', 'three'];

    const blob = new Blob(source.map(part => encoder.encode(part).buffer));

    equals(await blob.text(), source.join(''));
  },

  async 'Creates a new Blob from an array of Blob'() {
    const source = ['one', 'two', 'three'];

    const blob = new Blob(source.map(part => new Blob([part])));

    equals(await blob.text(), source.join(''));
  },

  async 'Accepts a String object as a sequence'() {
    const expected = 'abc';

    // eslint-disable-next-line no-new-wrappers
    const blob = new Blob(new String(expected));

    equals(await blob.text(), expected);
  },

  async 'Accepts Uint8Array as a sequence'() {
    const expected = [1, 2, 3];
    const blob = new Blob(new Uint8Array(expected));

    equals(await blob.text(), expected.join(''));
  },

  async 'Accepts iterable object as a sequence'() {
    const blob = new Blob({ [Symbol.iterator]: Array.prototype[Symbol.iterator] });

    equals(blob.size, 0);
    equals(await blob.text(), '');
  },

  async 'Constructor reads blobParts from iterable object'() {
    const source = ['one', 'two', 'three'];
    const expected = source.join('');

    const blob = new Blob({
      *[Symbol.iterator]() {
        yield* source;
      },
    });

    equals(blob.size, new TextEncoder().encode(expected).byteLength);
    equals(await blob.text(), expected);
  },

  'Blob has the size measured from the blobParts'() {
    const source = ['one', 'two', 'three'];
    const expected = new TextEncoder().encode(source.join('')).byteLength;

    const blob = new Blob(source);

    equals(blob.size, expected);
  },

  'Accepts type for Blob as an option in the second argument'() {
    const expected = 'text/markdown';

    const blob = new Blob(['Some *Markdown* content'], { type: expected });

    equals(blob.type, expected);
  },

  async 'Casts elements of the blobPart array to a string'() {
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

    equals(await blob.text(), expected);
  },

  'undefined value has no affect on property bag argument'() {
    const blob = new Blob([], undefined);

    equals(blob.type, '');
  },

  'null value has no affect on property bag argument'() {
    // @ts-expect-error Ignored, because that is what we are testing for
    const blob = new Blob([], null);

    equals(blob.type, '');
  },

  'Invalid type in property bag will result in an empty string'() {
    const blob = new Blob([], { type: '\u001Ftext/plain' });

    equals(blob.type, '');
  },

  'Throws an error if invalid property bag passed'() {
    const rounds = [123, 123.4, true, false, 'FAIL'];

    rounds.forEach(round => {
      // @ts-expect-error
      const trap = () => new Blob([], round);

      throwsJs(TypeError, trap);

      /*t.throws(trap, {
        instanceOf: TypeError,
        message: "Failed to construct 'Blob': " + 'parameter 2 cannot convert to dictionary.',
      });*/
    });
  },

  async '.slice() a new blob when called without arguments'() {
    const blob = new Blob(['a', 'b', 'c']);
    const sliced = blob.slice();

    equals(sliced.size, blob.size);
    equals(await sliced.text(), await blob.text());
  },

  async '.slice() an empty blob with the start and the end set to 0'() {
    const blob = new Blob(['a', 'b', 'c']);
    const sliced = blob.slice(0, 0);

    equals(sliced.size, 0);
    equals(await sliced.text(), '');
  },

  async '.slice() slices the Blob within given range'() {
    const text = 'The MIT License';
    const blob = new Blob([text]).slice(0, 3);

    equals(await blob.text(), 'The');
  },

  async '.slice() slices the Blob from arbitary start'() {
    const text = 'The MIT License';
    const blob = new Blob([text]).slice(4, 15);

    equals(await blob.text(), 'MIT License');
  },

  async '.slice() slices the Blob from the end when start argument is negative'() {
    const text = 'The MIT License';
    const blob = new Blob([text]).slice(-7);

    equals(await blob.text(), 'License');
  },

  async '.slice() slices the Blob from the start when end argument is negative'() {
    const text = 'The MIT License';
    const blob = new Blob([text]).slice(0, -8);

    equals(await blob.text(), 'The MIT');
  },

  async '.slice() slices Blob in blob parts'() {
    const text = 'The MIT License';
    const blob = new Blob([new Blob([text]), new Blob([text])]).slice(8, 18);

    equals(await blob.text(), 'LicenseThe');
  },

  async '.slice() slices within multiple parts'() {
    const blob = new Blob(['Hello', 'world']).slice(4, 7);

    equals(await blob.text(), 'owo');
  },

  async '.slice() throws away unwanted parts'() {
    const blob = new Blob(['a', 'b', 'c']).slice(1, 2);

    equals(await blob.text(), 'b');
  },

  '.slice() takes type as the 3rd argument'() {
    const expected = 'text/plain';
    const blob = new Blob([], { type: 'text/html' }).slice(0, 0, expected);

    equals(blob.type, expected);
  },

  async '.text() returns a the Blob content as string when awaited'() {
    const blob = new Blob(['a', new TextEncoder().encode('b'), new Blob(['c']), new TextEncoder().encode('d').buffer]);

    equals(await blob.text(), 'abcd');
  },

  async '.arrayBuffer() returns the Blob content as ArrayBuffer when awaited'() {
    const source = new TextEncoder().encode('abc');
    const blob = new Blob([source]);

    equals(new Uint8Array(await blob.arrayBuffer()) + '', source + '');
  },

  '.stream() returns ReadableStream'() {
    const stream = new Blob().stream();

    isTrue(stream instanceof ReadableStream);
  },

  async '.stream() allows to read Blob as a stream'() {
    const source = new TextEncoder().encode("Some content");
    const blob = new Blob([source]);
    const stream = blob.stream();

    const reader = stream.getReader();
    const chunks = [];
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
    }

    const totalLength = chunks.reduce((sum, chunk) => sum + chunk.byteLength, 0);
    const result = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(new Uint8Array(chunk), offset);
      offset += chunk.byteLength;
    }

    equals(result.length, source.length);
    for (let i = 0; i < source.length; i++) {
      equals(result[i], source[i]);
    }
  },

  async '.stream() returned ReadableStream can be cancelled'() {
    const stream = new Blob(['Some content']).stream();

    // Cancel the stream before start reading, or this will throw an error
    await stream.cancel();

    const reader = stream.getReader();

    const { done, value: chunk } = await reader.read();

    isTrue(done);
    equals(chunk, undefined);
  },
});
