// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/terminal.js: the pure functions and the exports, the same on every runtime */
import { EAGAIN, decodeKeys, enableCapabilities, parseCapabilityReplies, queryCapabilities, syncOutput, SYNC_BEGIN, SYNC_END, stringWidth, stripAnsi, truncate, truncateLines, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite, windowSize, wrap } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'tty primitives are exported'() {
    for(const f of [ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite]) eq(typeof f, 'function');
    eq(EAGAIN, 11);
  },

  'stripAnsi / stringWidth'() {
    eq(stripAnsi('\x1b[1mbold\x1b[0m'), 'bold');
    eq(stringWidth('abc'), 3);
    eq(stringWidth('\x1b[31m日本\x1b[0m'), 4);
  },

  'truncate / wrap'() {
    eq(truncate('hello world', 6), 'hello…');
    assert(wrap('aaa bbb ccc', 7).every(l => l.length <= 7));
  },

  'stringWidth counts cells, not code points'() {
    eq(stringWidth(''), 0);
    eq(stringWidth('e\u0301'), 1);
    eq(stringWidth('😀'), 2);
    eq(stringWidth('a\x1b]0;title\x07b'), 2);
    eq(stringWidth('a\x1b]8;;http://x\x1b\\b'), 2);
    eq(stringWidth('\x1b7a'), 1);
  },

  'stripAnsi cuts CSI, OSC and two byte sequences'() {
    eq(stripAnsi('plain'), 'plain');
    eq(stripAnsi('\x1b[1;4mx\x1b[m\x1b7y'), 'xy');
    eq(stripAnsi('a\x1b]0;t\x07b\x1b]8;;u\x1b\\c'), 'abc');
    eq(stripAnsi(12), '12');
  },

  'truncate keeps escapes, closes a style, honors the ellipsis'() {
    eq(truncate('hello', 5), 'hello');
    eq(truncate('hello world', 6, '..'), 'hell..');
    eq(truncate('日本語', 4), '日…');
    eq(truncate('\x1b[31mhello world\x1b[0m', 6), '\x1b[31mhello\x1b[0m…');
    eq(truncate('hello', 0), '');
    eq(truncate('hello', 1, '....'), '');
  },

  'truncateLines cuts every element like truncate'() {
    eq(JSON.stringify(truncateLines(['hello world', 'hi', '\x1b[31mhello world\x1b[0m'], 6)), JSON.stringify(['hello…', 'hi', '\x1b[31mhello\x1b[0m…']));
    eq(JSON.stringify(truncateLines(['hello world'], 6, '..')), '["hell.."]');
    eq(JSON.stringify(truncateLines([], 6)), '[]');
  },

  'wrap breaks after spaces and carries a style over the break'() {
    eq(JSON.stringify(wrap('one two three', 7)), '["one two","three"]');
    eq(JSON.stringify(wrap('a\n\nb', 5)), '["a","","b"]');
    eq(JSON.stringify(wrap('\x1b[31mred green\x1b[0m', 5)), JSON.stringify(['\x1b[31mred\x1b[0m', '\x1b[31mgreen\x1b[0m']));
    eq(JSON.stringify(wrap('abcdefgh', 3)), '["abc","def","gh"]');
  },

  'wrap puts a cell wider than the width on a line of its own'() {
    eq(JSON.stringify(wrap('日本x', 1)), '["日","本","x"]');
  },

  'parseCapabilityReplies reads DECRQM, kitty, XTVERSION and DA1 answers'() {
    const r = parseCapabilityReplies('a\x1b[?2026;2$y\x1b[?2004;0$y\x1b[?0u\x1bP>|kitty(0.35)\x1b\\\x1b[?62;4cb');
    eq(r.modes[2026], 2);
    eq(r.modes[2004], 0);
    eq(r.kitty, 0);
    eq(r.version, 'kitty(0.35)');
    assert(r.da1);
    eq(r.rest, 'ab');
    assert(!parseCapabilityReplies('x').da1);
    eq(parseCapabilityReplies('x').kitty, undefined);
  },

  'queryCapabilities gives false everywhere without a terminal'() {
    const caps = queryCapabilities(1, { env: {} });
    if(caps.answered) return;
    assert(!caps.syncOutput && !caps.kittyKeyboard && !caps.bracketedPaste && !caps.truecolor);
    eq(typeof enableCapabilities(1, caps), 'function');
  },

  'syncOutput brackets a frame'() {
    eq(SYNC_BEGIN, '\x1b[?2026h');
    eq(SYNC_END, '\x1b[?2026l');
    eq(typeof syncOutput, 'function');
  },

  'decodeKeys reads kitty keys, focus and a bracketed paste'() {
    const { keys } = decodeKeys('\x1b[27u\x1b[13;2u\x1b[97;5u\x1b[9;3:3u\x1b[I\x1b[O\x1b[200~a\nb\x1b[201~\x1b[?2026;2$yq', true);
    const k = keys.map(k => k.key);
    eq(k[0].name, 'escape');
    assert(k[1].name == 'return' && k[1].shift && !k[1].ctrl);
    assert(k[2].name == 'a' && k[2].ctrl && !k[2].meta);
    assert(k[3].name == 'tab' && k[3].meta && k[3].event == 'release');
    eq(k[4].name, 'focusin');
    eq(k[5].name, 'focusout');
    eq(k[6].name, 'paste');
    eq(k[6].paste, 'a\nb');
    eq(keys.at(-1).s, 'q');
    eq(keys.length, 9);
  },

  'decodeKeys keeps an unfinished paste for the next chunk'() {
    const { keys, rest } = decodeKeys('x\x1b[200~abc');
    eq(keys.length, 1);
    eq(rest, '\x1b[200~abc');
  },

  'decodeKeys'() {
    const { keys } = decodeKeys('a\x1b[A', true);
    eq(keys.length, 2);
    eq(keys[0].s, 'a');
    eq(keys[1].key.name, 'up');
  },

  'windowSize falls back to 80x24 or the real size'() {
    const [c, r] = windowSize(1);
    assert(c > 0 && r > 0);
  },
});
