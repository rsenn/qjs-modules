import { IOReadDecorator } from '../../lib/io.js';
import { eq, tests } from '../../lib/tinytest.js';

/* a reader over bytes with the read(buffer, offset, length) of a file */
function reader(bytes) {
  let pos = 0;

  return Object.assign(Object.create(IOReadDecorator), {
    read(buffer, offset = 0, length = buffer.byteLength - offset) {
      const n = Math.min(length, bytes.length - pos);

      new Uint8Array(buffer, offset, n).set(bytes.subarray(pos, pos + n));
      pos += n;
      return n;
    },
  });
}

const lines = text => {
  const r = reader(new TextEncoder().encode(text)),
    out = [];
  let line;

  while((line = r.getline()) !== null) out.push(line);

  return out;
};

tests({
  'getline() returns the lines of ASCII text, letters and digits alike'() {
    eq(JSON.stringify(lines('abc\n123\nx y\n')), JSON.stringify(['abc', '123', 'x y']));
  },
  'getline() keeps multi-byte characters whole'() {
    eq(JSON.stringify(lines('héllo\n€\u{1f600}\n')), JSON.stringify(['héllo', '€\u{1f600}']));
  },
  'getline() returns an unterminated last line, an empty line, then null'() {
    eq(JSON.stringify(lines('a\n\nb')), JSON.stringify(['a', '', 'b']));
    eq(reader(new Uint8Array(0)).getline(), null);
  },
});
