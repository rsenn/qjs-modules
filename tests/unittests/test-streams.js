import { TextDecoderStream, TextEncoderStream } from '../../lib/streams.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* lib/streams.js prints debug lines through console.log; keep the output readable */
async function quiet(fn) {
  const log = console.log;

  console.log = () => {};

  try {
    return await fn();
  } finally {
    console.log = log;
  }
}

async function readAll(readable) {
  const reader = readable.getReader();
  const chunks = [];

  for(;;) {
    const { done, value } = await reader.read();

    if(done) break;
    chunks.push(value);
  }

  return chunks;
}

tests({
  async 'TextEncoderStream encodes written strings as UTF-8'() {
    const bytes = await quiet(async () => {
      const stream = new TextEncoderStream();
      const writer = stream.writable.getWriter();

      const chunks = readAll(stream.readable);

      await writer.write('hi€');
      await writer.close();

      return [...new Uint8Array((await chunks)[0])];
    });

    eq('104,105,226,130,172', bytes.join());
  },

  async 'the readable side ends after the writer closes'() {
    const chunks = await quiet(async () => {
      const stream = new TextEncoderStream();
      const writer = stream.writable.getWriter();

      const chunks = readAll(stream.readable);

      await writer.write('a');
      await writer.write('b');
      await writer.close();

      return chunks;
    });

    eq(2, chunks.length);
  },

  async 'TextDecoderStream is exported'() {
    eq('function', typeof TextDecoderStream);
  },

  async 'TextDecoderStream joins a character split across chunks, skips empty ones'() {
    const stream = new TextDecoderStream();
    const writer = stream.writable.getWriter();
    const reader = stream.readable.getReader();
    const chunks = [];
    const read = (async () => {
      for(let r; !(r = await reader.read()).done; ) chunks.push(r.value);
    })();

    for(const bytes of [[0x61, 0xe2], [], [0x82], [0xac, 0x62]]) await writer.write(new Uint8Array(bytes));

    await writer.close();
    await read;
    eq('a,\u20acb', chunks.join());
  },

  async 'TextDecoderStream ends an incomplete sequence with U+FFFD'() {
    const stream = new TextDecoderStream();
    const writer = stream.writable.getWriter();
    const reader = stream.readable.getReader();

    writer.write(new Uint8Array([0x61, 0xe2, 0x82]));
    writer.close();
    eq('a', (await reader.read()).value);
    eq('\ufffd', (await reader.read()).value);
    assert((await reader.read()).done);
  },

  async 'TextDecoderStream options and getters, fatal errors the stream'() {
    const stream = new TextDecoderStream('latin1', { ignoreBOM: true });

    eq('windows-1252,false,true', [stream.encoding, stream.fatal, stream.ignoreBOM].join());

    const fatal = new TextDecoderStream('utf-8', { fatal: true });
    const writer = fatal.writable.getWriter();
    const reader = fatal.readable.getReader();

    writer.write(new Uint8Array([0xff])).catch(() => {});

    try {
      await reader.read();
      assert(false, 'no error');
    } catch(e) {
      assert(e instanceof TypeError, e);
    }

    try {
      new TextDecoderStream('nope');
      assert(false, 'no RangeError');
    } catch(e) {
      assert(e instanceof RangeError, e);
    }
  },
});
