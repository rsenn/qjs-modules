import { decode, encode, isAscii, isUtf8, TextDecoder, TextEncoder, utf8Offsets } from 'textcode';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'TextDecoder decode() decodes UTF-8 bytes to a string'() {
    const dec = new TextDecoder();
    const bytes = new Uint8Array([72, 101, 108, 108, 111]);
    eq(dec.decode(bytes), 'Hello');
  },
  'TextDecoder decode() handles multi-byte UTF-8 sequences'() {
    const dec = new TextDecoder();
    const enc = new TextEncoder();
    const bytes = enc.encode('héllo€');
    eq(dec.decode(bytes), 'héllo€');
  },
  'TextDecoder default encoding is utf-8'() {
    const dec = new TextDecoder();
    eq(dec.encoding, 'utf-8');
  },
  'TextDecoder(label) sets the WHATWG encoding name'() {
    eq(new TextDecoder('utf-8').encoding, 'utf-8');
    eq(new TextDecoder(' UTF8 ').encoding, 'utf-8');
    eq(new TextDecoder('utf-16').encoding, 'utf-16le');
    eq(new TextDecoder('utf-16be').encoding, 'utf-16be');
    eq(new TextDecoder('latin1').encoding, 'windows-1252');
    eq(new TextDecoder('iso-8859-9').encoding, 'windows-1254');
    eq(new TextDecoder('tis-620').encoding, 'windows-874');
    eq(new TextDecoder('utf-32').encoding, 'utf-32le');
  },
  'TextDecoder(label) throws RangeError for an unknown label'() {
    for(const label of ['foo', 'utf16', ''])
      try {
        new TextDecoder(label);
        assert(false, label);
      } catch(e) {
        assert(e instanceof RangeError && e.code == 'ERR_ENCODING_NOT_SUPPORTED', label);
      }
  },
  'TextDecoder replaces each maximal invalid subpart with U+FFFD'() {
    const dec = new TextDecoder();
    eq(dec.decode(new Uint8Array([0xf0, 0x9f, 0x41, 0xed, 0xa0, 0x80])), '\ufffdA\ufffd\ufffd\ufffd');
    eq(dec.decode(new Uint8Array([0x41, 0, 0xe9, 0x80])), 'A\0\ufffd');
    eq(dec.decode(new Uint8Array([0xc0, 0x80, 0xf5])), '\ufffd\ufffd\ufffd');
    eq(dec.decode(new Uint8Array([0xe2, 0x82])), '\ufffd');
  },
  'TextDecoder fatal throws TypeError'() {
    const dec = new TextDecoder('utf-8', { fatal: true });
    assert(dec.fatal && !dec.ignoreBOM);

    for(const bytes of [[0xff], [0xe2, 0x82]])
      try {
        dec.decode(new Uint8Array(bytes));
        assert(false, bytes);
      } catch(e) {
        assert(e instanceof TypeError && e.code == 'ERR_ENCODING_INVALID_ENCODED_DATA', bytes);
      }

    eq(dec.decode(new Uint8Array([0x61])), 'a');
  },
  'TextDecoder stream: true keeps a sequence split across chunks'() {
    const bytes = [0xef, 0xbb, 0xbf, 0xe2, 0x82, 0xac, 0x41, 0xf0, 0x9f, 0x98, 0x80, 0xe0, 0x41];

    for(let k = 0; k <= bytes.length; k++) {
      const dec = new TextDecoder();
      const s = dec.decode(new Uint8Array(bytes.slice(0, k)), { stream: true }) + dec.decode(new Uint8Array(bytes.slice(k)));

      eq(s, '\u20acA\u{1f600}\ufffdA', `split at ${k}`);
    }

    const dec = new TextDecoder();
    eq(dec.decode(new Uint8Array([0xe2, 0x82]), { stream: true }), '');
    eq(dec.buffered, 2);
    eq(dec.decode(), '\ufffd');
  },
  'TextDecoder removes a BOM unless ignoreBOM'() {
    const bom = new Uint8Array([0xef, 0xbb, 0xbf, 0x41]);

    eq(new TextDecoder().decode(bom), 'A');
    eq(new TextDecoder('utf-8', { ignoreBOM: true }).decode(bom), '\ufeffA');
    eq(new TextDecoder('utf-16le').decode(new Uint8Array([0xff, 0xfe, 0x41, 0])), 'A');
  },
  'TextDecoder utf-16 pairs surrogates, a lone one is U+FFFD'() {
    const bytes = new Uint8Array([0x41, 0, 0x3d, 0xd8, 0x00, 0xde, 0x00, 0xd8, 0x42, 0, 0x43]);

    eq(new TextDecoder('utf-16le').decode(bytes), 'A\u{1f600}\ufffdB\ufffd');
    eq(new TextDecoder('utf-16be').decode(new Uint8Array([0, 0x41, 0xd8, 0x3d, 0xde, 0x00])), 'A\u{1f600}');
  },
  'TextDecoder single-byte encodings follow the WHATWG index'() {
    eq(new TextDecoder('windows-1252').decode(new Uint8Array([0x80, 0x81, 0x9f, 0xe9])), '\u20ac\u0081\u0178\u00e9');
    eq(new TextDecoder('windows-1255').decode(new Uint8Array([0xca])), '\u05ba');
    eq(new TextDecoder('windows-874').decode(new Uint8Array([0x85, 0xa1])), '\u2026\u0e01');
    eq(new TextDecoder('iso-8859-2').decode(new Uint8Array([0xa1])), '\u0104');
    eq(new TextDecoder('iso-8859-3').decode(new Uint8Array([0xa5])), '\ufffd');
  },
  'TextDecoder decodes ArrayBuffer, DataView and nothing'() {
    const ab = new Uint8Array([0x61, 0x62, 0x63, 0x64]).buffer;

    eq(new TextDecoder().decode(ab), 'abcd');
    eq(new TextDecoder().decode(new DataView(ab, 1, 2)), 'bc');
    eq(new TextDecoder().decode(new Uint8Array(ab, 2)), 'cd');
    eq(new TextDecoder().decode(), '');

    try {
      new TextDecoder().decode('abc');
      assert(false);
    } catch(e) {
      assert(e instanceof TypeError && e.code == 'ERR_INVALID_ARG_TYPE');
    }
  },
  'TextDecoder endian defaults to little-endian (falsy)'() {
    const dec = new TextDecoder('utf-16');
    assert(!dec.endian);
  },
  'TextDecoder(label) with be suffix sets big-endian'() {
    const dec = new TextDecoder('utf-16be');
    assert(dec.endian);
  },
  'TextDecoder buffered starts empty'() {
    const dec = new TextDecoder();
    eq(dec.buffered, 0);
  },

  'TextEncoder encode() returns a Uint8Array of UTF-8 bytes'() {
    const enc = new TextEncoder();
    const out = enc.encode('Hi');
    assert(out instanceof Uint8Array);
    eq(out.length, 2);
    eq(out[0], 72);
    eq(out[1], 105);
  },
  'TextEncoder default encoding is utf-8'() {
    eq(new TextEncoder().encoding, 'utf-8');
  },
  'TextEncoder(label) sets encoding'() {
    eq(new TextEncoder('utf-16').encoding, 'utf-16le');
    eq(new TextEncoder('utf-32').encoding, 'utf-32le');
  },
  'TextEncoder encodeInto() writes into an existing typed array'() {
    const enc = new TextEncoder();
    const dest = new Uint8Array(16);
    const result = enc.encodeInto('Hi', dest);
    eq(result.read, 2);
    eq(result.written, 2);
    eq(dest[0], 72);
    eq(dest[1], 105);
  },
  'TextEncoder encodeInto() truncates when dest is too small'() {
    const enc = new TextEncoder();
    const dest = new Uint8Array(2);
    const result = enc.encodeInto('Hello', dest);
    assert(result.written <= 2);
  },
  'TextEncoder encodeInto() reports read in UTF-16 units, as WHATWG'() {
    const enc = new TextEncoder();
    const r = enc.encodeInto('a\u00e9\ud83d\ude00', new Uint8Array(16));
    eq(r.read, 4);
    eq(r.written, 7);
    eq(enc.encodeInto('\ud83d\ude00\ud83d\ude00x', new Uint8Array(5)).read, 2);
  },
  'utf8Offsets() maps UTF-16 indices to UTF-8 byte offsets in place'() {
    const enc = new TextEncoder();
    const text = 'a\u00e9\ud83d\ude00b\u4e2d\n\u20acx';
    const idx = Uint32Array.from({ length: text.length + 1 }, (_, i) => i);
    const ret = utf8Offsets(enc.encode(text), idx);
    assert(ret === idx);

    for(let i = 0; i <= text.length; i++) {
      const pair = i > 0 && i < text.length && text.charCodeAt(i - 1) >= 0xd800 && text.charCodeAt(i - 1) < 0xdc00;
      eq(idx[i], enc.encode(text.slice(0, pair ? i - 1 : i)).length, `index ${i}`);
    }

    eq([...utf8Offsets(enc.encode('ab'), new Uint32Array([2, 0, 9]))].join(), '2,0,2');
  },
  'isAscii() and isUtf8() check bytes like node'() {
    const u = (...b) => new Uint8Array(b);
    eq(isAscii(u()), true);
    eq(isAscii(u(0x41, 0x7f)), true);
    eq(isAscii(u(0x41, 0x80)), false);
    eq(isAscii(new Uint8Array(1000).fill(65).subarray(3)), true);
    eq(isUtf8(u(0xe2, 0x82, 0xac)), true);
    eq(isUtf8(u(0xf0, 0x9f, 0x98, 0x80)), true);
    eq(isUtf8(u(0xc0, 0xaf)), false);
    eq(isUtf8(u(0xed, 0xa0, 0x80)), false);
    eq(isUtf8(u(0xf4, 0x90, 0x80, 0x80)), false);
    eq(isUtf8(u(0xe2, 0x82)), false);
    eq(isUtf8(new Uint8Array([65, 66, 67, 68, 69, 70, 71, 72, 73, 0xc3, 0xa9]).buffer), true);
  },
  'encode() and decode() handle latin1, ascii and utf-16le'() {
    const bytes = b => [...new Uint8Array(b)].join();
    eq(bytes(encode('A\u00e9\u0100\ud83d\ude00', 'latin1')), '65,233,0,61,0');
    eq(bytes(encode('A\ud800\u00e9', 'utf-16le')), '65,0,0,216,233,0');
    eq(decode(new Uint8Array([0x41, 0xe9, 0xff, 0x80]), 'latin1'), 'A\u00e9\u00ff\u0080');
    eq(decode(new Uint8Array([0x41, 0xe9, 0xff, 0x80]), 'ascii'), 'Ai\x7f\0');

    let name;
    try {
      encode('x', 'utf-7');
    } catch(e) {
      name = e.name;
    }
    eq(name, 'RangeError');
  },
});
