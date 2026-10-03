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

      await writer.write('hi€');
      await writer.close();

      return [...new Uint8Array((await readAll(stream.readable))[0])];
    });

    eq('104,105,226,130,172', bytes.join());
  },

  async 'the readable side ends after the writer closes'() {
    const chunks = await quiet(async () => {
      const stream = new TextEncoderStream();
      const writer = stream.writable.getWriter();

      await writer.write('a');
      await writer.write('b');
      await writer.close();

      return readAll(stream.readable);
    });

    eq(2, chunks.length);
  },

  async 'TextDecoderStream is exported'() {
    eq('function', typeof TextDecoderStream);
  },
});
