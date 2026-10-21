// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* string_decoder: StringDecoder, as in Node, on qjsm lib/string_decoder.js */
import { StringDecoder } from 'string_decoder';
import decoderModule from 'string_decoder';
import { assert, eq, tests } from '../../lib/tinytest.js';

const u = (...a) => Uint8Array.from(a);
const all = (enc, ...chunks) => {
  const d = new StringDecoder(enc);

  return JSON.stringify([...chunks.map(c => d.write(c)), d.end()]);
};

/* the error fn throws, bun has no code on the argument type errors */
function thrown(fn) {
  try {
    fn();
  } catch(e) {
    return e;
  }
}

tests({
  'the default export holds StringDecoder'() {
    assert(decoderModule.StringDecoder === StringDecoder);
  },

  'encoding names are normalized'() {
    const names = [undefined, null, 'UTF-8', 'ucs2', 'UTF-16LE', 'binary', 'base64url', 'Hex', 'ascii'];

    eq('utf8,utf8,utf8,utf16le,utf16le,latin1,base64url,hex,ascii', names.map(e => new StringDecoder(e).encoding).join());
    eq('ERR_UNKNOWN_ENCODING', thrown(() => new StringDecoder('buffer'))?.code);
  },

  'utf8 holds a split character until it is complete'() {
    eq('["","","€",""]', all('utf8', u(0xe2), u(0x82), u(0xac)));
    eq('["�A","","","😀�",""]', all('utf8', u(0xe2, 0x41), u(0xf0, 0x9f), u(0x98), u(0x80, 0xff)));
  },

  'end() turns an incomplete utf8 character into U+FFFD and starts over'() {
    const d = new StringDecoder();

    eq('', d.write(u(0xe2, 0x82)));
    eq('�', d.end());
    eq('A', d.write(u(0x41)));
    eq('�A', (d.write(u(0xe2)), d.end(u(0x41))));
  },

  'a byte order mark is kept'() {
    eq('﻿A', new StringDecoder().write(u(0xef, 0xbb, 0xbf, 0x41)));
    eq('﻿A', new StringDecoder('utf16le').write(u(0xff, 0xfe, 0x41, 0)));
  },

  'utf16le holds an odd byte and a trailing high surrogate'() {
    eq('["","😀",""]', all('utf16le', u(0x3d, 0xd8), u(0x00, 0xde)));
    eq('["","\\ud83d","\\ude00",""]', all('utf16le', u(0x3d), u(0xd8, 0x00), u(0xde)));
  },

  'utf16le keeps lone surrogates, end() drops an odd byte'() {
    eq('["\\udc00A","\\ud83dA",""]', all('utf16le', u(0x00, 0xdc, 0x41, 0), u(0x3d, 0xd8, 0x41, 0)));
    eq('["","\\ud83d"]', all('utf16le', u(0x3d, 0xd8)));
    eq('["",""]', all('utf16le', u(0x41)));
  },

  'base64 encodes whole 3-byte groups, end() pads the rest'() {
    eq('["","AQID","BAU="]', JSON.stringify([...(d => [d.write(u(1)), d.write(u(2, 3, 4)), d.end(u(5))])(new StringDecoder('base64'))]));
    eq('["AQIDBAUG","","Bwg="]', all('base64', u(1, 2, 3, 4, 5, 6, 7), u(8)));
    eq('["","-_8"]', all('base64url', u(0xfb, 0xff)));
  },

  'hex, latin1 and ascii decode each byte'() {
    eq('["01ab",""]', all('hex', u(1, 0xab)));
    eq('["éA",""]', all('latin1', u(0xe9, 0x41)));
    eq('["iA",""]', all('ascii', u(0xe9, 0x41)));
  },

  'a string passes through, anything but bytes throws'() {
    const d = new StringDecoder();

    eq('str', d.write('str'));
    assert(thrown(() => d.write(5)) instanceof TypeError);
    assert(thrown(() => d.write(new ArrayBuffer(1))) instanceof TypeError);
    assert(thrown(() => d.end(null)) instanceof TypeError);
  },
});
