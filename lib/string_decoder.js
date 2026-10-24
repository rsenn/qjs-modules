/* string_decoder.js: Node's string_decoder module, StringDecoder.
 * depends on: textcode (TextDecoder, decode), misc (concatArrayBuffer).
 * rule: a chunk never splits a character, the bytes of an incomplete one
 * wait in the decoder for the next write() or end().
 *
 *   import { StringDecoder } from 'string_decoder';
 *
 *   const d = new StringDecoder('utf8');
 *   d.write(Uint8Array.of(0xe2, 0x82));   // ''
 *   d.end(Uint8Array.of(0xac));           // '€'
 */
import { concatArrayBuffer } from 'misc';
import { decode, TextDecoder } from 'textcode';

/* label, lowercase | encoding */
const ENCODINGS = {
  utf8: 'utf8',
  'utf-8': 'utf8',
  utf16le: 'utf16le',
  'utf-16le': 'utf16le',
  ucs2: 'utf16le',
  'ucs-2': 'utf16le',
  latin1: 'latin1',
  binary: 'latin1',
  base64: 'base64',
  base64url: 'base64url',
  hex: 'hex',
  ascii: 'ascii',
};

function nodeError(Type, code, message) {
  const e = new Type(message);

  e.code = code;
  return e;
}

/* the bytes of a TypedArray or DataView as a Uint8Array,
 * throws TypeError ERR_INVALID_ARG_TYPE for anything else */
function bytes(buf) {
  if(ArrayBuffer.isView(buf)) return new Uint8Array(buf.buffer, buf.byteOffset, buf.byteLength);

  const received = buf == null ? String(buf) : typeof buf == 'object' ? `an instance of ${buf.constructor?.name ?? 'Object'}` : `type ${typeof buf} (${String(buf)})`;

  throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', `The "buf" argument must be an instance of Buffer, TypedArray, or DataView. Received ${received}`);
}

/* UTF-16LE units as a string, lone surrogates kept as Node does */
const utf16Decoder = new TextDecoder('utf-16le', { ignoreBOM: true });

function utf16(u8) {
  let s = utf16Decoder.decode(u8);

  if(!s.includes('�')) return s;

  s = '';

  for(let i = 0; i + 1 < u8.length; i += 2) s += String.fromCharCode(u8[i] | (u8[i + 1] << 8));

  return s;
}

export class StringDecoder {
  #decoder = null;
  #rest = new Uint8Array(0);

  constructor(encoding) {
    const enc = encoding == null || encoding === '' ? 'utf8' : ENCODINGS[String(encoding).toLowerCase()];

    if(!enc) throw nodeError(TypeError, 'ERR_UNKNOWN_ENCODING', `Unknown encoding: ${encoding}`);

    this.encoding = enc;

    if(enc == 'utf8') this.#decoder = new TextDecoder('utf-8', { ignoreBOM: true });
  }

  /* the text of the held bytes plus buf, up to the last whole character,
   * a string buf is returned as is */
  write(buf) {
    if(typeof buf == 'string') return buf;

    let u8 = bytes(buf);

    if(this.#decoder) return this.#decoder.decode(u8, { stream: true });

    if(this.#rest.length) {
      const r = this.#rest;

      u8 = new Uint8Array(concatArrayBuffer(r.buffer, r.byteOffset, r.byteOffset + r.length, u8.buffer, u8.byteOffset, u8.byteOffset + u8.length));
    }

    let keep = 0;

    switch (this.encoding) {
      case 'utf16le': {
        /* an odd byte waits, so does a high surrogate ending an even chunk */
        if(u8.length & 1) keep = 1;
        else if(u8.length && (u8[u8.length - 1] & 0xfc) == 0xd8) keep = 2;
        break;
      }
      case 'base64':
      case 'base64url': {
        keep = u8.length % 3;
        break;
      }
    }

    this.#rest = u8.slice(u8.length - keep);

    return this.#text(u8.subarray(0, u8.length - keep));
  }

  /* write(buf) plus the held bytes, then the decoder starts over:
   * incomplete UTF-8 gives U+FFFD, a held high surrogate stays lone */
  end(buf) {
    let s = buf === undefined ? '' : this.write(buf);

    if(this.#decoder) return s + this.#decoder.decode();

    const r = this.#rest;

    this.#rest = new Uint8Array(0);

    return s + this.#text(this.encoding == 'utf16le' ? r.subarray(0, r.length & ~1) : r);
  }

  #text(u8) {
    if(!u8.length) return '';

    switch (this.encoding) {
      case 'utf16le':
        return utf16(u8);
      case 'hex':
        return u8.toHex();
      case 'base64':
        return u8.toBase64();
      case 'base64url':
        return u8.toBase64({ alphabet: 'base64url', omitPadding: true });
      default:
        return decode(u8, this.encoding);
    }
  }
}

export default { StringDecoder };
