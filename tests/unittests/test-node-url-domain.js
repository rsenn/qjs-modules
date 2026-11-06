// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* node:url domainToASCII() and domainToUnicode(); expected values come from node 23 */
import url, { domainToASCII, domainToUnicode } from 'node:url';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* Deno differs from Node: domainToUnicode keeps an invalid host, a non-string is no domain, a missing argument does not throw */
const deno = typeof Deno != 'undefined';
const NUL = String.fromCharCode(0);
const ZWJ = String.fromCharCode(0x200d);

/* domain | domainToASCII | domainToUnicode, null when the unicode form is not compared */
const VALID = [
  ['español.com', 'xn--espaol-zwa.com', 'español.com'],
  ['中文.com', 'xn--fiq228c.com', '中文.com'],
  ['xn--fiq228c.com', 'xn--fiq228c.com', '中文.com'],
  ['EXAMPLE.COM', 'example.com', 'example.com'],
  ['münchen.de.', 'xn--mnchen-3ya.de.', 'münchen.de.'],
  ['faß.de', 'xn--fa-hia.de', 'faß.de'],
  ['ǆ.com', 'xn--d-toa.com', 'dž.com'],
  ['xn--mnchen-3ya.de', 'xn--mnchen-3ya.de', 'münchen.de'],
  ['müNcHen.DE', 'xn--mnchen-3ya.de', 'münchen.de'],
  ['日本語。jp', 'xn--wgv71a119e.jp', '日本語.jp'],
  ['Ⅷ.com', 'viii.com', 'viii.com'],
  ['a..b', 'a..b', 'a..b'],
  ['a_b.com', 'a_b.com', 'a_b.com'],
  ['-a.com', '-a.com', '-a.com'],
  ['a-.com', 'a-.com', 'a-.com'],
  ['com', 'com', 'com'],
  ['a'.repeat(64) + '.com', 'a'.repeat(64) + '.com', 'a'.repeat(64) + '.com'],
];

/* domains that are no host: '' */
const INVALID = ['ex ample.com', 'xn--iñvalid.com', 'xn--', 'a.xn--', 'xn--a.com', 'http://x.com', NUL + 'a.com', 'a' + ZWJ + 'b.com', ''];

/* the last label is a number, so the host is an IPv4 address */
const ADDRESSES = [
  ['1.2.3.4', '1.2.3.4'],
  ['123.456', '123.0.1.200'],
  ['0x7f.1', '127.0.0.1'],
  ['0177.0.0.1', '127.0.0.1'],
  ['1', '0.0.0.1'],
];

tests({
  'exports both functions, also on the default object'() {
    eq('function', typeof domainToASCII);
    eq('function', typeof domainToUnicode);
    assert(url.domainToASCII === domainToASCII, 'default.domainToASCII');
    assert(url.domainToUnicode === domainToUnicode, 'default.domainToUnicode');
  },

  'domainToASCII maps, lowercases and punycode-encodes'() {
    for(const [domain, ascii] of VALID) eq(ascii, domainToASCII(domain));
  },

  'domainToUnicode decodes the xn-- labels'() {
    for(const [domain, , unicode] of VALID) eq(unicode, domainToUnicode(domain));
  },

  'an invalid host gives an empty string'() {
    for(const domain of INVALID) {
      eq('', domainToASCII(domain));
      if(!deno) eq('', domainToUnicode(domain));
    }
  },

  'a numeric last label is an IPv4 address'() {
    for(const [domain, address] of ADDRESSES) eq(address, domainToASCII(domain));
  },

  'an IPv6 address in brackets stays'() {
    eq('[::1]', domainToASCII('[::1]'));
    eq('[::1]', domainToASCII('[0:0:0:0:0:0:0:1]'));
    eq('[2001:db8::1]', domainToASCII('[2001:DB8:0:0:0:0:0:1]'));
    eq('[::ffff:102:304]', domainToASCII('[::ffff:1.2.3.4]'));
    eq('', domainToASCII('[::1'));
    eq('', domainToASCII('[1:2:3]'));
  },

  'the argument is converted to a string'() {
    if(deno) return;

    eq('0.0.0.1', domainToASCII(1));
    eq('null', domainToASCII(null));
    eq('xn--4ca.com', domainToASCII({ toString: () => 'ä.com' }));
  },

  'a missing argument throws ERR_MISSING_ARGS'() {
    if(deno) return;

    try {
      domainToASCII();
    } catch(e) {
      eq('TypeError ERR_MISSING_ARGS', `${e.name} ${e.code}`);
      return;
    }

    assert(false, 'no throw');
  },
});
