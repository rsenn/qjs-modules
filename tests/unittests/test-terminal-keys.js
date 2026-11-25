// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
// import { decodeKeys } from '../../lib/terminal.js';
// import { assert, eq, tests } from '../../lib/tinytest.js';

import { decodeKeys } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'decodeKeys legacy vs kitty: Esc'() {
    const { keys: legacy } = decodeKeys('\x1b', true);
    const { keys: kitty } = decodeKeys('\x1b[27u', true);
    // Compare name, ctrl, meta, shift explicitly
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Shift-Tab'() {
    const { keys: legacy } = decodeKeys('\x1b[Z', true);
    const { keys: kitty } = decodeKeys('\x1b[9;2u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Alt-Tab'() {
    const { keys: legacy } = decodeKeys('\x1b\t', true);
    const { keys: kitty } = decodeKeys('\x1b[9;3u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Alt-1'() {
    const { keys: legacy } = decodeKeys('\x1b1', true);
    const { keys: kitty } = decodeKeys('\x1b[49;3u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Alt-0'() {
    const { keys: legacy } = decodeKeys('\x1b0', true);
    const { keys: kitty } = decodeKeys('\x1b[48;3u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Alt-backslash'() {
    const { keys: legacy } = decodeKeys('\x1b\\', true);
    const { keys: kitty } = decodeKeys('\x1b[92;3u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Alt-letter'() {
    const { keys: legacy } = decodeKeys('\x1ba', true);
    const { keys: kitty } = decodeKeys('\x1b[97;3u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Ctrl-letter'() {
    const { keys: legacy } = decodeKeys('\x01', true);  // Ctrl-A
    const { keys: kitty } = decodeKeys('\x1b[97;5u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Enter'() {
    const { keys: legacy } = decodeKeys('\r', true);
    const { keys: kitty } = decodeKeys('\x1b[13u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys legacy vs kitty: Backspace'() {
    const { keys: legacy } = decodeKeys('\x7f', true);
    const { keys: kitty } = decodeKeys('\x1b[127u', true);
    const k1 = legacy[0].key;
    const k2 = kitty[0].key;
    eq(k1.name, k2.name);
    eq(k1.ctrl, k2.ctrl);
    eq(k1.meta, k2.meta);
    eq(k1.shift, k2.shift);
  },

  'decodeKeys F2 legacy forms'() {
    // \x1bOQ and \x1b[12~ both decode to f2
    const { keys: oq } = decodeKeys('\x1bOQ', true);
    const { keys: tilde } = decodeKeys('\x1b[12~', true);
    eq(oq[0].key.name, 'f2');
    eq(tilde[0].key.name, 'f2');
  },

  'decodeKeys Shift-F2 legacy forms'() {
    // \x1b[1;2Q should give name "f2" with shift true
    const { keys: q } = decodeKeys('\x1b[1;2Q', true);
    const { keys: tilde2 } = decodeKeys('\x1b[24~', true);
    eq(q[0].key.name, 'f2');
    eq(q[0].key.shift, true);
    eq(tilde2[0].key.name, 'f12');
  },

  'decodeKeys PageUp/Dn'() {
    const { keys: pu } = decodeKeys('\x1b[5~', true);
    const { keys: pd } = decodeKeys('\x1b[6~', true);
    eq(pu[0].key.name, 'pageup');
    eq(pd[0].key.name, 'pagedown');
  },

  'decodeKeys Esc Esc'() {
    const { keys: esc } = decodeKeys('\x1b\x1b', true);
    eq(esc[0].key.name, 'escape');
    eq(esc[0].key.meta, true);
  },
});
