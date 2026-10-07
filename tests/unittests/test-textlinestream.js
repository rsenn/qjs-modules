// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/textLineStream.js: Deno's TextLineStream; the results below are those of jsr:@std/streams */
import { eq, tests } from '../../lib/tinytest.js';

/* qjsm's global streams have no pipeTo/pipeThrough: use the WHATWG implementation */
if(typeof ReadableStream == 'undefined' || !ReadableStream.prototype.pipeThrough) {
  const m = await import('../../lib/stream.js');

  for(const n of ['ReadableStream', 'WritableStream', 'TransformStream']) globalThis[n] = m[n];
}

const { TextLineStream } = await import('../../lib/textLineStream.js');
const { JsonParseStream } = await import('../../lib/jsonStreams.js');

const from = chunks =>
  new ReadableStream({
    start(c) {
      for(const x of chunks) c.enqueue(x);
      c.close();
    },
  });

const collect = async readable => {
  const out = [];

  await readable.pipeTo(new WritableStream({ write: v => void out.push(v) }));

  return JSON.stringify(out);
};

const lines = (chunks, options) => collect(from(chunks).pipeThrough(new TextLineStream(options)));

tests({
  async 'splits on \\n and \\r\\n, however the input is chunked'() {
    eq(await lines(['a\nb\nc']), '["a","b","c"]');
    eq(await lines(['a\nb\nc\n']), '["a","b","c"]');
    eq(await lines(['a\r\nb\r\n']), '["a","b"]');
    eq(await lines(['a', 'b\nc', '\nd\r', '\ne']), '["ab","c","d","e"]');
    eq(await lines(['\n\n']), '["",""]');
    eq(await lines(['']), '[]');
    eq(await lines([]), '[]');
  },
  async 'a lone \\r is part of the line unless allowCR is set'() {
    eq(await lines(['a\rb\r']), '["a\\rb\\r"]');
    eq(await lines(['a\r', 'b\n']), '["a\\rb"]');
    eq(await lines(['a\rb\r'], { allowCR: true }), '["a","b"]');
    eq(await lines(['a\r', 'b\n'], { allowCR: true }), '["a","b"]');
    eq(await lines(['a\r\n', '\r\nb\n'], { allowCR: true }), '["a","","b"]');
    eq(await lines(['a\r', '\nb'], { allowCR: true }), '["a","b"]');
  },
  async 'lines feed JsonParseStream'() {
    eq(await collect(from(['{"a":1}\n[2', ']\n3\n']).pipeThrough(new TextLineStream()).pipeThrough(new JsonParseStream())), '[{"a":1},[2],3]');
  },
});
