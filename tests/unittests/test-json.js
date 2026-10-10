import * as std from 'std';
import { read, write, JSONL, JsonParser, JsonPushParser, JsonSerializer, JsonWriter } from 'json';
import { assert, eq, tests } from '../../lib/tinytest.js';

const { NEED_DATA, NONE, OBJECT, OBJECT_END, ARRAY, ARRAY_END, KEY, STRING, TRUE, FALSE, NULL, NUMBER } = JsonParser;

/* tinytest's eq() uses !=, which does reference comparison for arrays/objects -
 * deep-compare via JSON.stringify instead (same convention as test_stream.js). */
const eqArr = (actual, expected) => eq(JSON.stringify(actual), JSON.stringify(expected));

function assertThrows(fn, msg) {
  try {
    fn();
  } catch(e) {
    return e;
  }
  throw new Error('assertThrows(): did not throw' + (msg ? ' - ' + msg : ''));
}

function tokens(p) {
  const out = [];
  let t;

  while((t = p.parse()) !== NEED_DATA) out.push([t, p.token, p.depth, p.pos]);

  out.push([t, p.token, p.depth, p.pos]);
  return out;
}

/* Feeds `text` through a pull callback that hands back at most `max` bytes per call. */
function pullTokens(text, max) {
  const bytes = Uint8Array.from(text, c => c.charCodeAt(0));
  const lens = [];
  let off = 0;
  const p = new JsonParser(
    {
      read(buf, len) {
        const n = Math.min(len, max, bytes.length - off);

        lens.push(len);
        new Uint8Array(buf).set(bytes.subarray(off, off + n));
        off += n;
        return n;
      },
    },
    'x',
  );

  return { toks: tokens(p), lens };
}

function drainParser(input, filename) {
  let p = filename !== undefined ? new JsonParser(input, filename) : new JsonParser(input);
  let toks = [];

  for(;;) {
    let t = p.parse();

    toks.push(t);

    if(t === NEED_DATA || t === NONE) break;
  }

  return { toks, parser: p };
}

tests({
  /* ---------- read: primitives ---------- */
  'read: null'() {
    eq(read('null'), null);
  },
  'read: true/false'() {
    eq(read('true'), true);
    eq(read('false'), false);
  },
  'read: integers'() {
    eq(read('42'), 42);
    eq(read('-17'), -17);
    eq(read('0'), 0);
  },
  'read: floats and exponents'() {
    eq(read('3.14'), 3.14);
    eq(read('1e3'), 1000);
    assert(Math.abs(read('1.5e-2') - 0.015) < 1e-12);
  },
  'read: strings'() {
    eq(read('"hello"'), 'hello');
    eq(read('""'), '');
  },

  /* ---------- read: containers ---------- */
  'read: empty containers'() {
    eqArr(read('[]'), []);
    eqArr(read('{}'), {});
  },
  'read: simple containers'() {
    eqArr(read('[1,2,3]'), [1, 2, 3]);
    eqArr(read('{"a":1}'), { a: 1 });
    eqArr(read('["a","b","c"]'), ['a', 'b', 'c']);
    eqArr(read('[true,false,null]'), [true, false, null]);
    eqArr(read('{"a":1,"b":2,"c":3}'), { a: 1, b: 2, c: 3 });
  },
  'read: nested containers'() {
    eqArr(read('[[1,2],[3,4]]'), [[1, 2], [3, 4]]);
    eqArr(read('{"a":{"b":1}}'), { a: { b: 1 } });
    eqArr(read('{"a":1,"b":[2,3],"c":{"d":4}}'), { a: 1, b: [2, 3], c: { d: 4 } });
  },
  'read: whitespace handling'() {
    eqArr(read('  [ 1 , 2 , 3 ]  '), [1, 2, 3]);
    eqArr(read('{\n  "a": 1,\n  "b": 2\n}'), { a: 1, b: 2 });
  },
  'read: deep array nesting (iterative - no stack overflow)'() {
    const depth = 1000;
    let s = '['.repeat(depth) + '42' + ']'.repeat(depth);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(Array.isArray(cur)) {
      actualDepth++;
      cur = cur[0];
    }

    assert(actualDepth === depth && cur === 42, `depth=${actualDepth}`);
  },
  'read: deep object nesting (iterative - no stack overflow)'() {
    const depth = 1000;
    let s = '{"a":'.repeat(depth) + '42' + '}'.repeat(depth);
    let r = read(s);
    let cur = r;
    let actualDepth = 0;

    while(typeof cur === 'object' && cur !== null && 'a' in cur) {
      actualDepth++;
      cur = cur.a;
    }

    assert(actualDepth === depth && cur === 42, `depth=${actualDepth}`);
  },
  'read: wide array'() {
    const count = 5000;
    let arr = new Array(count).fill(0).map((_, i) => i);
    let s = '[' + arr.join(',') + ']';
    let r = read(s);

    assert(r.length === count && r[count - 1] === count - 1);
  },
  'read: malformed input throws'() {
    assertThrows(() => read(']'), 'stray ]');
    assertThrows(() => read('}'), 'stray }');
    assertThrows(() => read('[1,2'), 'unclosed array');
    assertThrows(() => read('"abc'), 'unclosed string');
    assertThrows(() => read('xyz'), 'unknown token');
  },

  /* ---------- write: primitives ---------- */
  'write: primitives round-trip through read()'() {
    eq(read(write(null)), null);
    eq(read(write(true)), true);
    eq(read(write(false)), false);
    eq(read(write(42)), 42);
    eq(read(write(3.14)), 3.14);
    eq(read(write(-100)), -100);
    eq(read(write(0)), 0);
    eq(read(write('hello')), 'hello');
    eq(read(write('')), '');
  },
  'write: containers'() {
    eq(write([]), '[]');
    eq(write({}), '{}');
    eq(write([1, 2, 3]), '[1,2,3]');
    eq(write({ a: 1 }), '{"a":1}');
  },
  'write: string escapes'() {
    eq(write('a"b'), '"a\\"b"');
    eq(write('a\\b'), '"a\\\\b"');
    eq(write('a\nb'), '"a\\nb"');
    eq(write('a\tb'), '"a\\tb"');
    eq(write('a\rb'), '"a\\rb"');
    eq(write('a\bb'), '"a\\bb"');
    eq(write('a\fb'), '"a\\fb"');
    eq(write('\x01\x02\x1f'), '"\\u0001\\u0002\\u001f"');
  },
  'write: special numbers become null'() {
    eq(write(NaN), 'null');
    eq(write(Infinity), 'null');
    eq(write(-Infinity), 'null');
  },
  'write: non-JSON values become null'() {
    eq(write(undefined), 'null');
    eq(write(() => 1), 'null');
    eq(write(Symbol('s')), 'null');
    eq(write([1, undefined, 2]), '[1,null,2]');
    eq(write([1, NaN, 2]), '[1,null,2]');
  },
  'write: nested containers'() {
    eq(write([[1, 2], [3, 4]]), '[[1,2],[3,4]]');
    eq(write({ a: { b: 1 } }), '{"a":{"b":1}}');
    eq(write({ a: 1, b: [2, 3], c: { d: 4 } }), '{"a":1,"b":[2,3],"c":{"d":4}}');
  },
  'write: deep array nesting (iterative - no stack overflow)'() {
    const depth = 1000;
    let v = 42;

    for(let i = 0; i < depth; i++) v = [v];

    let r = read(write(v));
    let cur = r;
    let actualDepth = 0;

    while(Array.isArray(cur)) {
      actualDepth++;
      cur = cur[0];
    }

    assert(actualDepth === depth && cur === 42);
  },
  'write: deep object nesting (iterative - no stack overflow)'() {
    const depth = 1000;
    let v = 42;

    for(let i = 0; i < depth; i++) v = { a: v };

    let r = read(write(v));
    let cur = r;
    let actualDepth = 0;

    while(typeof cur === 'object' && cur !== null && 'a' in cur) {
      actualDepth++;
      cur = cur.a;
    }

    assert(actualDepth === depth && cur === 42);
  },
  'write: wide array'() {
    const count = 5000;
    let arr = new Array(count).fill(0).map((_, i) => i);
    let r = read(write(arr));

    assert(r.length === count && r[count - 1] === count - 1);
  },
  'write: circular object (self) truncated, still parses back'() {
    let a = { x: 1 };
    a.self = a;

    let r = read(write(a));

    assert(typeof r === 'object' && r.x === 1);
  },
  'write: circular array (self) truncated, still parses back'() {
    let a = [1, 2];
    a.push(a);

    let r = read(write(a));

    assert(Array.isArray(r) && r[0] === 1 && r[1] === 2);
  },
  'write: mutually circular objects, still parses back'() {
    let a = { name: 'a' },
      b = { name: 'b', a: a };
    a.b = b;

    let r = read(write(a));

    assert(typeof r === 'object' && r.name === 'a');
  },
  'write: circular value whose toString would overflow does not crash'() {
    /* write_json_primitive's fallback calls JS_ToCString on the circular value;
     * for a deeply nested element this triggers Array.prototype.join recursion
     * that hits QuickJS's "stack overflow" InternalError. The writer must catch
     * that and clear the exception rather than propagate/crash. */
    let inner = 42;
    for(let i = 0; i < 800; i++) inner = [inner];

    let root = [inner, null];
    root[1] = root;

    let s = write(root);

    assert(typeof s === 'string' && s.length > 0);
  },
  'round-trip parity across value shapes'() {
    const cases = [
      null,
      true,
      false,
      0,
      42,
      -17,
      3.14,
      '',
      'hello',
      [],
      {},
      [1, 2, 3],
      { a: 1 },
      [1, 'two', true, null, [3]],
      { name: 'Alice', age: 30, hobbies: ['reading', 'coding'], address: { city: 'NY', zip: 10001 } },
    ];

    for(let v of cases) {
      let s = write(v);
      let r = read(s);

      eqArr(r, v);
    }
  },
  'known limitation: reader keeps \\n, \\", \\\\ literal (does not decode string escapes)'() {
    /* The underlying sj.h library returns the raw byte range between the
     * surrounding quotes, so \n in the JSON source stays as literal backslash-n
     * in the JS string. The writer DOES escape correctly (see write: string
     * escapes above) - this pins down read()'s current, asymmetric behavior. */
    eq(read('"a\\nb"'), 'a\\nb');
    eq(read('"a\\"b"'), 'a\\"b');
    eq(read('"a\\\\b"'), 'a\\\\b');
  },

  /* ---------- JsonParser (pull) ---------- */
  'JsonParser: is a constructor'() {
    assert(typeof JsonParser === 'function');
  },
  'JsonParser: basic token sequence'() {
    let { toks } = drainParser('{"a":1}');

    eqArr(toks, [OBJECT, KEY, NUMBER, OBJECT_END, NEED_DATA]);
  },
  'JsonParser: KEY recognized after nested array closes'() {
    /* Regression test: closing a nested array/object must restore the
     * *enclosing* context (object-expects-key vs. array-expects-element), not
     * the container's own type - the bitset stack tracks what to restore, not
     * what's being closed (that's already known from which token, '}' or ']',
     * triggered the close). */
    let { toks } = drainParser('{"a":1,"b":[2,"x"],"c":null}');

    eqArr(toks, [OBJECT, KEY, NUMBER, KEY, ARRAY, NUMBER, STRING, ARRAY_END, KEY, NULL, OBJECT_END, NEED_DATA]);
  },
  'JsonParser: decodes string escapes and surrogate pairs'() {
    let p = new JsonParser('"a\\nb\\tc\\"d\\\\e caf\\u00e9 \\ud83d\\ude00"');

    p.parse();

    eq(p.token, 'a\nb\tc"d\\e café 😀');
  },
  'JsonParser: mixed array of objects/numbers/literals'() {
    let { toks } = drainParser('[{"x":1},{"y":[2,3]},"end",-1.5e2,null,true,false]');

    eqArr(toks, [
      ARRAY,
      OBJECT,
      KEY,
      NUMBER,
      OBJECT_END,
      OBJECT,
      KEY,
      ARRAY,
      NUMBER,
      NUMBER,
      ARRAY_END,
      OBJECT_END,
      STRING,
      NUMBER,
      NULL,
      TRUE,
      FALSE,
      ARRAY_END,
      NEED_DATA,
    ]);
  },
  'JsonParser: deep nesting stays balanced'() {
    const depth = 200;
    let { toks } = drainParser('['.repeat(depth) + '1' + ']'.repeat(depth));
    let opens = toks.filter(t => t === ARRAY).length;
    let closes = toks.filter(t => t === ARRAY_END).length;

    assert(opens === depth && closes === depth);
  },
  'JsonParser: throws on invalid token/escape, message includes line:column'() {
    assertThrows(() => drainParser('{"a": xyz}'), 'invalid token');
    assertThrows(() => drainParser('"a\\qb"'), 'invalid escape');

    let e = assertThrows(() => drainParser('{"a": xyz}'));
    assert(/^\d+:\d+:/.test(e.message), e.message);
  },
  'JsonParser: .pos and .depth track position'() {
    let p = new JsonParser('{"a":[1,2]}');

    p.parse(); // OBJECT
    eq(p.depth, 1);

    p.parse(); // KEY
    p.parse(); // ARRAY
    eq(p.depth, 2);

    assert(p.pos > 0);
  },
  'JsonParser: .location tracks line/column, .location.file reflects filename'() {
    let { parser } = drainParser('{\n  "a": 1\n}', 'my-input.json');

    assert(typeof parser.location.line === 'number' && typeof parser.location.column === 'number');
    eq(parser.location.file, 'my-input.json');
  },
  'JsonParser: pull callback is asked for a block, not a byte'() {
    let { lens } = pullTokens('{"a":[1,2,3]}', Infinity);

    assert(lens.length > 0 && lens.every(n => n >= 4096), lens.join());
  },
  'JsonParser: pull callback yields the same tokens as string input, however short its reads'() {
    const doc =
      '{"s":"caf\\u00e9 \\ud83d\\ude00 \\u2028 \\"q\\" \\\\ \\n","n":[-1.5e-2,12345678901234,0],' +
      '"lit":[true,false,null],"deep":{"a":{"b":["' + 'x'.repeat(40) + '",{}]}}}';
    const expected = tokens(new JsonParser(doc));

    assert(expected.length > 20);

    for(const max of [1, 2, 3, 5, 7, 4096, Infinity]) eqArr(pullTokens(doc, max).toks, expected);
  },
  'JsonParser: pull callback at block boundaries'() {
    for(const size of [16384, 2 * 16384]) {
      for(let pad = size - 40; pad < size + 3; pad++) {
        /* an escape and a surrogate pair straddle the block boundary for some of these pads */
        const doc = '["' + 'a'.repeat(pad) + '\\u00e9\\ud83d\\ude00",12345,"tail"]';

        eqArr(pullTokens(doc, Infinity).toks, tokens(new JsonParser(doc)));
      }
    }

    for(const size of [16384, 2 * 16384]) {
      const n = Math.floor((size - 3) / 2);
      const doc = '[' + '1,'.repeat(n) + '1' + ' '.repeat(size - 3 - 2 * n) + ']';

      eq(doc.length, size);
      eqArr(pullTokens(doc, Infinity).toks, tokens(new JsonParser(doc)));
    }
  },
  'JsonParser: pull callback with empty input'() {
    eqArr(pullTokens('', Infinity).toks, [[NEED_DATA, null, 0, 0]]);
  },
  'JsonParser: accepts an fd number'() {
    const doc = '{"a":[1,"b",null],"c":{"d":true}}';
    const f = std.tmpfile();

    f.puts(doc);
    f.flush();
    f.seek(0, std.SEEK_SET);

    eqArr(tokens(new JsonParser(f.fileno())), tokens(new JsonParser(doc)));
    f.close();
  },

  /* ---------- JsonPushParser (push, .write()) ---------- */
  'JsonPushParser: whole document in one write()'() {
    let p = new JsonPushParser();
    p.write('{"a":1,"b":[2,3,"x"],"c":{"d":null,"e":true}}');

    eqArr(p.root, { a: 1, b: [2, 3, 'x'], c: { d: null, e: true } });
    eqArr(p.path, []);
  },
  'JsonPushParser: fed one byte at a time'() {
    let p = new JsonPushParser();
    let doc = '{"name":"hello world","nums":[1,22,333],"flag":true,"nil":null,"nested":{"x":-1.5e2}}';

    for(let i = 0; i < doc.length; i++) p.write(doc[i]);

    eqArr(p.root, {
      name: 'hello world',
      nums: [1, 22, 333],
      flag: true,
      nil: null,
      nested: { x: -150 },
    });
  },
  'JsonPushParser: .path tracks position mid-parse'() {
    let p = new JsonPushParser();
    p.write('{"a":{"b":[1,2,');

    eqArr(p.path, ['a', 'b', 2]);

    p.write('3]}}');

    eqArr(p.path, []);
    eqArr(p.root, { a: { b: [1, 2, 3] } });
  },
  'JsonPushParser: nesting deeper than 64 levels'() {
    let doc = '';

    for(let i = 0; i < 300; i++) doc += i % 2 ? '{"k":' : '[1,null,true,"s",';
    doc += '0';
    for(let i = 299; i >= 0; i--) doc += i % 2 ? '}' : ']';

    let whole = new JsonPushParser();
    whole.write(doc);
    whole.close();
    eqArr(whole.root, JSON.parse(doc));

    let chunked = new JsonPushParser();
    for(let i = 0; i < doc.length; i += 5) chunked.write(doc.slice(i, i + 5));
    chunked.close();
    eqArr(chunked.root, JSON.parse(doc));
  },
  'JsonPushParser: close() flushes a trailing top-level scalar'() {
    let p = new JsonPushParser();
    p.write('42');

    assert(p.root === undefined, 'ambiguous trailing number before close()');

    p.close();

    eq(p.root, 42);
  },
  'JsonPushParser: close() throws on an incomplete document'() {
    assertThrows(() => {
      let p = new JsonPushParser();
      p.write('{"a":1');
      p.close();
    });
  },
  'JsonPushParser: throws on malformed input, then resyncs and recovers'() {
    let p = new JsonPushParser();

    assertThrows(() => p.write('[1, xyz, 2]'));
    eqArr(p.root, [1, 2]);
  },
  'JsonPushParser: decodes string escapes and surrogate pairs'() {
    let p = new JsonPushParser();
    p.write('{"s":"a\\nb\\tc\\"d\\\\e caf\\u00e9 \\ud83d\\ude00"}');

    eq(p.root.s, 'a\nb\tc"d\\e café 😀');
  },
  'JsonPushParser: .location getter advances'() {
    let p = new JsonPushParser();
    let loc0 = p.location.clone();

    assert(typeof loc0.line === 'number' && typeof loc0.column === 'number');

    p.write('{"a":1}');

    let loc1 = p.location;

    assert(loc1.charOffset > loc0.charOffset, `${loc0.charOffset} -> ${loc1.charOffset}`);
  },

  /* ---------- JsonSerializer (pull, .read()) ---------- */
  'JsonSerializer: read(n) round-trips, arbitrary chunk size'() {
    const cases = [
      null,
      true,
      false,
      0,
      42,
      -17,
      3.14,
      '',
      'hello',
      [],
      {},
      [1, 2, 3],
      { a: 1 },
      [1, 'two', true, null, [3]],
      { name: 'Alice', age: 30, hobbies: ['reading', 'coding'], address: { city: 'NY', zip: 10001 } },
    ];

    for(let v of cases) {
      let s = new JsonSerializer(v);
      let out = '',
        chunk;

      while((chunk = s.read(3))) out += chunk;

      eqArr(JSON.parse(out), v);
    }
  },
  /*'JsonSerializer: indent option matches write() options'() {
    let s = new JsonSerializer({ a: 1, b: [2, 3] }, 2);
    let out = '',
      chunk;

    while((chunk = s.read(1000)) !== '') out += chunk;

    eq(out, write({ a: 1, b: [2, 3] }, 2));
  },*/
  'JsonSerializer: read(buffer) writes directly into an ArrayBuffer/TypedArray'() {
    function utf8decode(bytes) {
      let out = '',
        i = 0;

      while(i < bytes.length) {
        let b0 = bytes[i];

        if(b0 < 0x80) {
          out += String.fromCharCode(b0);
          i += 1;
        } else if((b0 & 0xe0) === 0xc0) {
          out += String.fromCodePoint(((b0 & 0x1f) << 6) | (bytes[i + 1] & 0x3f));
          i += 2;
        } else if((b0 & 0xf0) === 0xe0) {
          out += String.fromCodePoint(((b0 & 0x0f) << 12) | ((bytes[i + 1] & 0x3f) << 6) | (bytes[i + 2] & 0x3f));
          i += 3;
        } else {
          out += String.fromCodePoint(((b0 & 0x07) << 18) | ((bytes[i + 1] & 0x3f) << 12) | ((bytes[i + 2] & 0x3f) << 6) | (bytes[i + 3] & 0x3f));
          i += 4;
        }
      }

      return out;
    }

    function readIntoBuffer(value, bufSize) {
      let s = new JsonSerializer(value);
      let buf = new Uint8Array(bufSize);
      let out = [];
      let n;

      while((n = s.read(buf)) > 0) for(let i = 0; i < n; i++) out.push(buf[i]);

      return utf8decode(out);
    }

    const cases = [
      [1, 2, 3, 'four', { five: 5 }],
      { name: 'Alice', age: 30, hobbies: ['reading', 'coding'], address: { city: 'NY', zip: 10001 } },
      { s: 'quote:"  backslash:\\  nl:\n tab:\t  ctrl:\x01 unicode:café' },
      [[[[1, 2], [3, 4]], [[5, 6]]], {}, [], '', 0, -3.5, true, false, null],
      'just a bare string as the whole document',
      42,
    ];

    for(let v of cases) {
      for(let bufSize of [1, 2, 3, 7, 64]) {
        let text = readIntoBuffer(v, bufSize);

        eqArr(JSON.parse(text), JSON.parse(write(v)));
      }
    }
  },
  'JsonSerializer: read(buffer) returns byte count, then 0 at EOF'() {
    let s = new JsonSerializer([1, 2, 3]);
    let buf = new Uint8Array(64);
    let n = s.read(buf);

    eq(n, write([1, 2, 3]).length);
    eq(s.read(buf), 0);
  },
  'JsonSerializer: .location getter advances after read()'() {
    let s = new JsonSerializer({ a: 1, b: 2 });
    let loc0 = s.location.clone();

    s.read(3);

    let loc1 = s.location;

    assert(loc1.charOffset >= loc0.charOffset, `${loc0.charOffset} -> ${loc1.charOffset}`);
  },
  /* ---------- comments ---------- */
  'JsonPushParser: // and block comments are skipped, one byte at a time'() {
    let p = new JsonPushParser();
    let doc = '// head\n{ /* a */ "a": 1, // x\n "b": [2, /* ** */ 3 /**/], "c": 4 /* t */ } // tail';

    for(let i = 0; i < doc.length; i++) p.write(doc[i]);

    eqArr(p.root, { a: 1, b: [2, 3], c: 4 });
  },
  'JsonPushParser: comment right after number and literal'() {
    let p = new JsonPushParser();

    p.write('[1/*x*/,true//y\n,null/*z*/]');
    eqArr(p.root, [1, true, null]);
  },
  'JsonParser: comments are skipped'() {
    eqArr(tokens(new JsonParser('/* c */ {"a" /* k */ : // v\n [1, 2]}')).map(([t, v]) => [t, v]), tokens(new JsonParser('{"a":[1,2]}')).map(([t, v]) => [t, v]));
  },
  'JsonParser: comment markers inside strings are kept'() {
    let p = new JsonPushParser();

    p.write('["// not", "/* a */"]');
    eqArr(p.root, ['// not', '/* a */']);
  },
  'JsonParser: comments = true reports COMMENT tokens'() {
    let p = new JsonParser('// a\n[1, /* b */ 2] // c');
    let got = [];
    let t;

    p.comments = true;

    while((t = p.parse()) !== NEED_DATA) got.push([t, t === JsonParser.COMMENT ? p.token : null]);

    eqArr(got, [
      [JsonParser.COMMENT, '// a'],
      [ARRAY, null],
      [NUMBER, null],
      [JsonParser.COMMENT, '/* b */'],
      [NUMBER, null],
      [ARRAY_END, null],
      [JsonParser.COMMENT, '// c'],
    ]);
  },
  'JSONL.parse'() {
    eqArr(JSONL.parse('{"a":1}\n{"a":2}\n'), [{ a: 1 }, { a: 2 }]);
    eqArr(JSONL.parse('1\r\n2\r\n'), [1, 2]);
    eqArr(JSONL.parse('1\n\n[3,\n4]\n"x"\ntrue\nnull\n'), [1, [3, 4], 'x', true, null]);
    eqArr(JSONL.parse(''), []);
    eqArr(JSONL.parse('1\n2\n{"a"'), [1, 2]);
    eqArr(JSONL.parse('1\nxx\n3'), [1]);
    eqArr(JSONL.parse(Uint8Array.from([49, 10, 50])), [1, 2]);
    assertThrows(() => JSONL.parse('xx\n1'));
  },
  'JSONL.parseChunk'() {
    const chunk = (...a) => JSON.stringify(JSONL.parseChunk(...a), (k, v) => (k == 'error' ? (v && v.name) : v));

    eq(chunk('1\n2\n'), '{"values":[1,2],"read":3,"done":true,"error":null}');
    eq(chunk('1\n2\n{"a"'), '{"values":[1,2],"read":3,"done":false,"error":null}');
    eq(chunk('1\nxx\n3'), '{"values":[1],"read":1,"done":false,"error":"SyntaxError"}');
    eq(chunk('1 2 3'), '{"values":[1],"read":1,"done":false,"error":"SyntaxError"}');
    eq(chunk(''), '{"values":[],"read":0,"done":true,"error":null}');
    eq(chunk('12'), '{"values":[12],"read":2,"done":true,"error":null}');
    eq(chunk('1\nxx'), '{"values":[1],"read":1,"done":false,"error":null}');
    eq(chunk('"a\\u00'), '{"values":[],"read":0,"done":false,"error":null}');
    eq(chunk('1e'), '{"values":[],"read":0,"done":false,"error":null}');
  },
  'JSONL.parseChunk start, end and units'() {
    const chunk = (...a) => JSON.stringify(JSONL.parseChunk(...a));
    const u8 = s => Uint8Array.from(unescape(encodeURIComponent(s)), c => c.charCodeAt(0));

    eq(chunk('1\n2\n3\n', 2), '{"values":[2,3],"read":5,"done":true,"error":null}');
    eq(chunk(u8('1\n2\n3\n'), 2, 4), '{"values":[2],"read":3,"done":true,"error":null}');
    eq(chunk('1\n2', 0, 1), '{"values":[1],"read":1,"done":true,"error":null}');
    /* read counts chars for a string, bytes for a Uint8Array */
    eq(chunk('"é"\n"😀"\n"x"\n'), '{"values":["é","😀","x"],"read":12,"done":true,"error":null}');
    eq(chunk(u8('"é"\n"x"\n')), '{"values":["é","x"],"read":8,"done":true,"error":null}');
  },
  'JSONL.parseChunk carries the unread tail across chunks'() {
    const text = '{"a":1}\n[2,\n3]\n"x"\n';
    const values = [];
    let buf = '';

    for(let i = 0; i < text.length; i += 3) {
      buf += text.slice(i, i + 3);

      const { values: v, read } = JSONL.parseChunk(buf);

      values.push(...v);
      buf = buf.slice(read);
    }

    eqArr(values, [{ a: 1 }, [2, 3], 'x']);
  },

'JsonWriter: bareKeys, singleQuotes, hexNumbers, minify'() {
  const emit = options => {
    const buf = new Uint8Array(256);
    const w = new JsonWriter(buf, options);

    w.objectStart();
    w.key('id');
    w.value(255);
    w.key('kebab-key');
    w.value("it's");
    w.key('list');
    w.arrayStart();
    w.value(-31);
    w.value(1.5);
    w.arrayEnd();
    w.objectEnd();

    return String.fromCharCode(...buf.subarray(0, buf.indexOf(0)));
  };

  eq(emit({}), '{"id":255,"kebab-key":"it\'s","list":[-31,1.5]}');
  eq(emit({ bareKeys: true }), '{id:255,"kebab-key":"it\'s",list:[-31,1.5]}');
  eq(emit({ singleQuotes: true }), "{'id':255,'kebab-key':'it\\'s','list':[-31,1.5]}");
  eq(emit({ hexNumbers: true }), '{"id":0xff,"kebab-key":"it\'s","list":[-0x1f,1.5]}');
  eq(emit({ indent: 2, minify: true }), '{"id":255,"kebab-key":"it\'s","list":[-31,1.5]}');
  eq(emit({ bareKeys: true, singleQuotes: true, hexNumbers: true, minify: true }), "{id:0xff,'kebab-key':'it\\'s',list:[-0x1f,1.5]}");
},

'JsonWriter: written counts every byte, whatever the options'() {
  const optionSets = [undefined, 2, { minify: true, indent: 4 }, { indent: 2, bareKeys: true, singleQuotes: true, hexNumbers: true }];

  for(const options of optionSets) {
    const buf = new Uint8Array(256);
    const w = new JsonWriter(buf, options);

    w.objectStart();
    w.key('a-b');
    w.arrayStart();
    w.value("x'y");
    w.value(255);
    w.arrayEnd();
    w.objectEnd();

    eq(w.written, buf.indexOf(0));
  }
},
'JsonWriter: structural mistakes throw TypeError, a full buffer throws Error'() {
  const fails = (fn, message) => {
    try {
      fn();
    } catch(e) {
      eq(e.message, message);
      return;
    }

    assert(false, 'expected: ' + message);
  };
  const writer = () => new JsonWriter(new Uint8Array(64));
  let w;

  fails(() => writer().arrayEnd(), 'JsonWriter: unmatched arrayEnd');
  fails(() => writer().key('k'), 'JsonWriter: key cannot be at root level');
  fails(() => (w = writer(), w.objectStart(), w.value(1)), 'JsonWriter: expected key');
  fails(() => (w = writer(), w.objectStart(), w.key('a'), w.key('b')), 'JsonWriter: expected value for previous key');
  fails(() => (w = writer(), w.arrayStart(), w.key('a')), 'JsonWriter: key cannot be used inside an array');
  fails(() => (w = new JsonWriter(new Uint8Array(4)), w.objectStart(), w.key('toolong')), 'JsonWriter: write failed');
},
});
