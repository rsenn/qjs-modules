// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/jsonStreams.js: Deno's @std/json streams; the results below are those of jsr:@std/json */
import { eq, tests } from '../../lib/tinytest.js';

/* qjsm's global streams have no pipeTo/pipeThrough: use the WHATWG implementation */
if(typeof ReadableStream == 'undefined' || !ReadableStream.prototype.pipeThrough) {
  const m = await import('../../lib/stream.js');

  for(const n of ['ReadableStream', 'WritableStream', 'TransformStream']) globalThis[n] = m[n];
}

const { JsonParseStream, ConcatenatedJsonParseStream, JsonStringifyStream } = await import('../../lib/jsonStreams.js');

const from = chunks =>
  new ReadableStream({
    start(c) {
      for(const x of chunks) c.enqueue(x);
      c.close();
    },
  });

/* the values the stream emits as JSON, or `ERR name` when it errors */
async function run(S, chunks, ...args) {
  try {
    const out = [];

    await from(chunks).pipeThrough(new S(...args)).pipeTo(new WritableStream({ write: v => void out.push(v) }));

    return JSON.stringify(out);
  } catch(e) {
    return 'ERR ' + e.name;
  }
}

tests({
  async 'JsonParseStream parses one value per chunk'() {
    eq(await run(JsonParseStream, ['{"a":1}', '[2]', '', ' 3 ']), '[{"a":1},[2],3]');
    eq(await run(JsonParseStream, ['{"a":1}\n{"a":2}']), 'ERR SyntaxError');
    eq(await run(JsonParseStream, ['xx']), 'ERR SyntaxError');
  },
  async 'ConcatenatedJsonParseStream splits values however the input is chunked'() {
    eq(await run(ConcatenatedJsonParseStream, ['{"a":1}{"b"', ':2}[3]', '"x"', '4 5 ', 'true null']), '[{"a":1},{"b":2},[3],"x",4,5,true,null]');
    eq(await run(ConcatenatedJsonParseStream, ['12', '3 ']), '[123]');
    eq(await run(ConcatenatedJsonParseStream, ['1{}']), '[1,{}]');
    eq(await run(ConcatenatedJsonParseStream, ['tru', 'e']), '[true]');
    eq(await run(ConcatenatedJsonParseStream, ['"a\\"b""c"']), '["a\\"b","c"]');
    eq(await run(ConcatenatedJsonParseStream, ['{"a":"}"}[1,"]"]']), '[{"a":"}"},[1,"]"]]');
  },
  async 'ConcatenatedJsonParseStream errors on truncated or bad input'() {
    eq(await run(ConcatenatedJsonParseStream, ['{"a":', '1']), 'ERR SyntaxError');
    eq(await run(ConcatenatedJsonParseStream, ['xx ']), 'ERR SyntaxError');
  },
  async 'JsonStringifyStream adds prefix and suffix'() {
    eq(await run(JsonStringifyStream, [{ a: 1 }, [2], 'x', null]), '["{\\"a\\":1}\\n","[2]\\n","\\"x\\"\\n","null\\n"]');
    eq(await run(JsonStringifyStream, [1, 2], { prefix: '\x1e', suffix: '\n' }), '["\\u001e1\\n","\\u001e2\\n"]');
    eq(await run(JsonStringifyStream, [1, 2], { prefix: '[', suffix: ',' }), '["[1,","[2,"]');
  },
});
