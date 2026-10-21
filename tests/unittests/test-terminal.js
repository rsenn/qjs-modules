/* lib/terminal.js: every function writes to the std FILE it is given */
import * as std from 'std';
import * as terminal from '../../lib/terminal.js';
import { Screen, cursorPosition, cursorUp, devicecodeQuery, eraseInLine, mousetrackingEnable, setScrollRegion, windowSize } from '../../lib/terminal.js';
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
    f.close();
  },
});
