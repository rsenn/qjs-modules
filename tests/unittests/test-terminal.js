/* lib/terminal.js: every function writes to the std FILE it is given */
import * as std from 'std';
import { close, pipe, write } from 'os';
import * as terminal from '../../lib/terminal.js';
import { Screen, cursorPosition, cursorSave, cursorUp, decodeKeys, devicecodeQuery, eraseInLine, mousetrackingEnable, readKey, setScrollRegion, stringWidth, ttyGetWinSize, ttyWrite, windowSize } from '../../lib/terminal.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* what fn writes to a fresh tmpfile */
function written(fn) {
  const f = std.tmpfile();

  fn(f);
  f.flush();
  f.seek(0, std.SEEK_SET);

  const s = f.readAsString();

  f.close();
  return s;
}

tests({
  'there is no default output'() {
    eq(undefined, terminal.terminal);
  },

  'cursor functions write to the given file'() {
    eq('\x1b[3;5H', written(f => cursorPosition(f, 3, 5)));
    eq('\x1b[2A', written(f => cursorUp(f, 2)));
    eq('\x1b[A', written(f => cursorUp(f, 1)));
    eq('\x1b7', written(f => cursorSave(f)));
  },

  'ttyWrite takes a string or bytes and flushes the file'() {
    eq('a\x1b[1mb', written(f => (f.puts('a'), ttyWrite(f, '\x1b[1m'), ttyWrite(f, new Uint8Array([0x62]).buffer))));
  },

  'eraseInLine goes through the file buffer in order'() {
    eq('ab\x1b[0Kc', written(f => (f.puts('ab'), eraseInLine(f, 0), f.puts('c'))));
  },

  'setScrollRegion takes the file first'() {
    eq('\x1b[1;20r', written(f => setScrollRegion(f, 1, 20)));
  },

  'queries and modes write to the given file'() {
    eq('\x1b[c', written(f => devicecodeQuery(f)));
    eq('\x1b[?1000;1006;1015h', written(f => mousetrackingEnable(f)));
  },

  'Screen flushes its buffer to its file'() {
    eq('\x1b[2;3Hx', written(f => new Screen(f).moveTo(2, 3).write('x').flush()));
  },

  'windowSize falls back to 80x24 for a file that is not a tty'() {
    const f = std.tmpfile();

    eq('80,24', windowSize(f).join());
    eq('80,24', windowSize(f.fileno()).join());
    eq(null, ttyGetWinSize(f.fileno()));
    f.close();
  },

  'decodeKeys splits keys and keeps an unfinished sequence'() {
    const { keys, rest } = decodeKeys('a\x1b[1;5D\x1b[Z\x1b[');

    eq('a,left,tab', keys.map(k => k.key.name).join());
    assert(keys[1].key.ctrl && keys[2].key.shift);
    eq('\x1b[', rest);
  },

  'stringWidth counts cells, not escapes'() {
    eq(3, stringWidth('\x1b[1m漢x\x1b[0m'));
    eq(1, stringWidth('e\u0301'));
  },

  'readKey decodes what arrives on the fd'() {
    const [r, w] = pipe();
    const bytes = new TextEncoder().encode('\x1b[Aä\r\x03');

    write(w, bytes.buffer, 0, bytes.length);
    close(w);

    eq('up', readKey(r).type);
    eq('ä', readKey(r).char);
    eq('enter', readKey(r).type);
    eq('ctrlc', readKey(r).type);
    eq('eof', readKey(r).type);
    close(r);
  },
});
