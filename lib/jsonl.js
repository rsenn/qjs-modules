/* jsonl.js: Bun's JSONL namespace, values separated by newlines.
 * depends on: nothing, plain JS.
 * rule: portable, the same results as Bun.JSONL; positions count UTF-16 units for a
 * string and bytes for a Uint8Array.
 *
 * ```js
 * import { JSONL } from './jsonl.js';
 *
 * JSONL.parse('1\n{"a":2}\n[3');     // [1, {a: 2}]
 * JSONL.parseChunk('1\n2\n[3');      // {values: [1, 2], read: 3, done: false, error: null}
 * ```
 *
 * | argument            | meaning                                          |
 * | ------------------- | ------------------------------------------------ |
 * | input               | string, or the UTF-8 bytes (Uint8Array, buffer)  |
 * | start               | parseChunk: first unit to read                   |
 * | end                 | parseChunk: unit to stop at                      |
 *
 * parse() returns the values read, a trailing incomplete value is ignored, it throws the
 * SyntaxError when the first value is bad. parseChunk() returns { values, read, done, error }:
 * read is the unit after the last value, done is true when the rest is only whitespace,
 * error the SyntaxError (code, length, loc) of a bad value. Both throw TypeError for another input. */

/* UTF-8 bytes as a string, TextDecoder when the runtime has it */
function utf8(bytes) {
  if(typeof TextDecoder == 'function') return new TextDecoder().decode(bytes);

  let latin1 = '';

  for(let i = 0; i < bytes.length; i += 8192) latin1 += String.fromCharCode(...bytes.subarray(i, i + 8192));
  return decodeURIComponent(escape(latin1));
}

const isws = c => c == 0x20 || c == 0x09 || c == 0x0d || c == 0x0a;

/* end of the value starting at i, or 0 when the input ends inside it */
function valueEnd(at, n, i) {
  const first = at(i);

  if(first == 0x7b || first == 0x5b || first == 0x22) {
    let depth = 0;

    for(let inStr = false; i < n; i++) {
      const c = at(i);

      if(inStr) {
        if(c == 0x5c) i++;
        else if(c == 0x22) {
          inStr = false;
          if(!depth) return i + 1;
        }
      } else if(c == 0x22) inStr = true;
      else if(c == 0x7b || c == 0x5b) depth++;
      else if((c == 0x7d || c == 0x5d) && --depth == 0) return i + 1;
    }

    return 0;
  }

  while(i < n && !isws(at(i))) i++;
  return i;
}

function syntaxError(message, code, line, column, length) {
  const e = new SyntaxError(message);

  Object.defineProperties(e, {
    code: { value: code, writable: true, configurable: true },
    length: { value: length, writable: true, configurable: true },
    loc: { value: { file: '<jsonl>', line, column }, writable: true, configurable: true },
  });
  return e;
}

function scan(input, start, end, name) {
  const isStr = typeof input == 'string';

  if(!isStr && !(input instanceof ArrayBuffer) && !ArrayBuffer.isView(input)) throw new TypeError(`${name}: expecting string or Uint8Array`);

  const bytes = isStr ? null : input instanceof ArrayBuffer ? new Uint8Array(input) : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
  const at = isStr ? i => input.charCodeAt(i) : i => bytes[i];
  const slice = isStr ? (a, b) => input.slice(a, b) : (a, b) => utf8(bytes.subarray(a, b));
  const total = isStr ? input.length : bytes.length;
  const n = end === undefined || end < 0 ? total : Math.min(end, total);
  const values = [];
  let i = Math.min(Math.max(start, 0), total),
    read = i,
    done = false,
    error = null;

  /* 1-based line and column of unit p, the column counts characters */
  const where = p => {
    let line = 1,
      col = 1;

    for(let k = 0; k < p; k++) {
      const c = at(k);

      if(c == 0x0a) (line++, (col = 1));
      else if(isStr ? !(c >= 0xdc00 && c <= 0xdfff) : (c & 0xc0) != 0x80) col++;
    }

    return [line, col];
  };

  for(;;) {
    while(i < n && isws(at(i))) i++;

    if(i >= n) {
      done = true;
      break;
    }

    const vs = i,
      e = valueEnd(at, n, vs);

    if(!e) break;

    let v;

    try {
      v = JSON.parse(slice(vs, e));
    } catch(err) {
      const first = at(vs);

      /* a bare token cut off by the end of input may be completed by more data */
      if(e == n && first != 0x7b && first != 0x5b && first != 0x22) break;

      error = syntaxError(err.message, 'invalid-json', ...where(vs), e - vs);
      break;
    }

    values.push(v);
    read = i = e;

    while(i < n && (at(i) == 0x20 || at(i) == 0x09 || at(i) == 0x0d)) i++;

    if(i < n && at(i) != 0x0a) {
      error = syntaxError('JSONL: expected newline after value', 'unexpected-token', ...where(i), 1);
      break;
    }
  }

  return { values, read, done, error };
}

export const JSONL = {
  parse(input) {
    const { values, error } = scan(input, 0, undefined, 'JSONL.parse()');

    if(!values.length && error) throw error;
    return values;
  },
  parseChunk(input, start = 0, end) {
    return scan(input, start, end, 'JSONL.parseChunk()');
  },
};

export default JSONL;
