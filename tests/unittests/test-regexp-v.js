import { assert, eq, tests } from '../../lib/tinytest.js';

/* needs a QuickJS whose libregexp accepts a plain - outside a class with the v flag; an older engine skips these tests */
const supported = (() => { try { new RegExp('a-b', 'v'); return true; } catch(e) { return false; } })();

tests({
  'a hyphen outside a class is a literal with the v flag'() {
    if(!supported) return;
    assert(new RegExp('a-b', 'v').test('a-b'));
    assert(new RegExp('^(?:-?\\d+n)', 'v').test('-12n'));
  },
  'an escaped class set punctuator is allowed inside a class with the v flag'() {
    if(!supported) return;
    assert(new RegExp('[a\\&]', 'v').test('&'));
    assert(new RegExp('[\\!\\#\\%\\,\\:\\;\\<\\=\\>\\@\\`\\~]', 'v').test('~'));
  },
  'the class set rules still hold inside a class'() {
    if(!supported) return;
    let error;
    try {
      new RegExp('[a&&&b]', 'v');
    } catch(e) {
      error = e;
    }
    eq(error instanceof SyntaxError, true);
    error = undefined;
    try {
      new RegExp('[(]', 'v');
    } catch(e) {
      error = e;
    }
    eq(error instanceof SyntaxError, true);
  },
});