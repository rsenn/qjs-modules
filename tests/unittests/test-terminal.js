/* lib/terminal.js: every function writes to the std FILE it is given */
import * as std from 'std';
import { close, pipe, write } from 'os';
import * as terminal from '../../lib/terminal.js';
import { layoutInput, alternateScreen, mouseDisable, mouseEnable, pad, stripAnsi, truncate, wrap, cursorPosition, cursorSave, cursorUp, devicecodeQuery, eraseInLine, mousetrackingEnable, setScrollRegion, stringWidth, ttyGetWinSize, ttyWrite, windowSize } from '../../lib/terminal.js';
import { decodeKeys, readKey } from '../../lib/terminal/keys.js';
import { BOX_ROUNDED, Region, Screen } from '../../lib/terminal/screen.js';
import { ScrollView } from '../../lib/terminal/scrollview.js';
import { gradient, style } from '../../lib/terminal/style.js';
import { gradientRule, meter, panel, shortPath, sideBySide, bigText, boxLines, inputBox, keyValue, progressBar, rule, scrollbar, sparkline, statusBar, table } from '../../lib/terminal/widgets.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* arrays and objects compared by their JSON, eq() compares with != */
const same = (a, b) => eq(JSON.stringify(a), JSON.stringify(b));

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

  'decodeKeys reads SGR, urxvt and X10 mouse reports'() {
    const { keys, rest } = decodeKeys('\x1b[<64;10;5M\x1b[<2;3;4m\x1b[<32;7;8M\x1b[35;1;2M\x1b[M !!x\x1b[<0;1');
    const m = keys.map(k => k.key.mouse && Object.values(k.key.mouse).join(' '));

    same(['wheelup wheel 10 5', 'right release 3 4', 'left drag 7 8', 'none release 1 2', 'left press 1 1', undefined], m);
    eq('x', keys[5].key.name);
    eq('\x1b[<0;1', rest);
  },

  'readKey returns mouse keys'() {
    const [r, w] = pipe();
    const bytes = new TextEncoder().encode('\x1b[<20;4;6M');

    write(w, bytes.buffer, 0, bytes.length);
    close(w);

    const k = readKey(r);

    same(['mouse', 'left', 'press', 4, 6, true, true], [k.type, k.button, k.action, k.x, k.y, k.ctrl, k.shift]);
    close(r);
  },

  'mouse modes and the alternate screen write their sequences'() {
    eq('\x1b[?1002h\x1b[?1006h', written(f => mouseEnable(f, 'drag')));
    assert(written(f => mouseDisable(f)).includes('\x1b[?1000l'));
    eq('\x1b[?1049hx\x1b[?1049l', written(f => alternateScreen(f, () => f.puts('x'))));
    eq('\x1b[?1049h', written(f => new Screen(f).alternate().flush()));
  },

  'style, truncate and pad keep cell widths'() {
    eq('\x1b[1;38;2;0;255;0mok\x1b[0m', style('ok', { fg: '#0f0', bold: true }));
    eq('hello w…', truncate('hello world', 8));
    eq('ab', truncate('ab', 8));
    eq('  漢  ', pad('漢', 6, 'center'));
    eq(4, stringWidth(pad('\x1b[1mabcdef\x1b[0m', 4)));
  },

  'wrap breaks at spaces and carries colors over the break'() {
    same(['one two', 'three'], wrap('one two three', 7));
    same(['abcd', 'ef'], wrap('abcdef', 4));
    same(['\x1b[31mab\x1b[0m', '\x1b[31mcd\x1b[0m'], wrap('\x1b[31mab cd\x1b[0m', 3));
    same(['a', '', 'b'], wrap('a\n\nb', 5));
  },

  'gradient colors columns and keeps the text'() {
    const g = gradient(['ab', 'c'], ['#000000', '#ffffff']);

    same(['ab', 'c'], g.map(stripAnsi));
    assert(g[0].includes('38;2;0;0;0m') && g[0].includes('38;2;255;255;255m'));
  },

  'bigText draws block letters'() {
    eq(5, bigText('A').length);
    eq(' ███', bigText('A')[0]);
    eq(3, bigText('hi', { half: true }).length);
  },

  'panel boxes the text, truncates with max and keeps the background'() {
    const p = panel('note', 'a\nb\nc\n', 40, { max: 2, background: '#101820', more: n => `+${n}` });
    const lines = stripAnsi(p).split('\n');

    same(true, lines.every(l => stringWidth(l) == stringWidth(lines[0])));
    same(true, lines[3].includes('+1'));
    same(true, p.split('\n').every(l => l.startsWith('\x1b[48;2;16;24;32m')));
  },
  'sideBySide, meter, gradientRule and shortPath'() {
    same(['ab  1', 'c   2', '    3'], sideBySide(['ab', 'c'], ['1', '2', '3'], 2));
    same(10, stringWidth(meter(0.5, 10)));
    same(true, meter(0.9, 4, { bad: '#ff0000' }).includes('38;2;255;0;0'));
    same(true, meter(0.9, 4, { color: 'blue' }).includes('34m'));
    same(20, stringWidth(gradientRule(20, 'log', ['#ff0000', '#0000ff'])));
    same('~/a', shortPath(process.env.HOME + '/a'));
    same('…c/a/b', shortPath('/x/y/c/a/b', 6));
  },
  'boxLines, inputBox, rule and statusBar are as wide as asked'() {
    const box = boxLines('hi there', { title: 't', width: 14, chars: BOX_ROUNDED });

    same(['╭─ t ────────╮', '│ hi there   │', '╰────────────╯'], box);
    eq('── x ─────', rule(10, { label: 'x' }));
    same([20, 20, 20], inputBox('', { width: 20, placeholder: 'type' }).map(l => stringWidth(l)).filter((w, k) => k != 1).concat(20));
    eq('left    mid    right', statusBar(['left', 'mid', 'right'], 20));
  },

  'table draws sections and aligns numbers right'() {
    const t = table([['a', '1'], { section: 'S' }, ['bb', '22']], { header: ['k', 'v'], headerStyle: null, sectionStyle: null });

    same(['┌────┬────┐', '│ k  │ v  │', '├────┼────┤', '│ a  │  1 │', '├────┴────┤', '│    S    │', '├────┬────┤', '│ bb │ 22 │', '└────┴────┘'], t);
  },

  'keyValue, progressBar and sparkline'() {
    same(['a    1', 'bbb  2'], keyValue({ a: 1, bbb: 2, c: null }));
    eq('█████     ', progressBar(0.5, 10));
    eq('▌ ', progressBar(0.25, 2));
    eq('▁▅▃█', sparkline([1, 5, 3, 8]));
  },

  'layoutInput wraps rows and places the cursor'() {
    same({ lines: ['> ab', '. cd'], row: 1, col: 3 }, layoutInput(['> ', '. '], 'ab\ncd', 4, 80));
    same({ lines: ['> abc', 'de'], row: 1, col: 1 }, layoutInput(['> ', ''], 'abcde', 4, 5));
    same({ lines: ['> abc', ''], row: 1, col: 0 }, layoutInput(['> ', ''], 'abc', 3, 5));
    same({ lines: ['> 漢', '字'], row: 0, col: 2 }, layoutInput(['> ', ''], '漢字', 0, 5));
  },

  'Region writes only the rows that changed'() {
    const r = new Region(5, 2, 10);

    eq('\x1b[5;1H\x1b[0ma\x1b[0m\x1b[K\x1b[6;1H\x1b[0mb\x1b[0m\x1b[K', r.update(['a', 'b']));
    eq('', r.update(['a', 'b']));
    eq('\x1b[6;1H\x1b[0mc\x1b[0m\x1b[K', r.update(['a', 'c']));
    eq('\x1b[1;1H\x1b[0ma\x1b[0m', r.moveTo(1, 1).update(['a']).replace('\x1b[K', ''));
    eq('\x1b[1;1H\x1b[0m0123456789\x1b[0m', r.invalidate().update(['0123456789']));
  },

  'ScrollView follows the end until scrolled up'() {
    const v = new ScrollView(3, ['a', 'b', 'c', 'd', 'e']);

    same(['c', 'd', 'e'], v.visible());
    assert(v.handleKey({ name: 'pageup' }));
    same(['a', 'b', 'c'], v.visible());
    v.push('f');
    same(['a', 'b', 'c'], v.visible());
    v.handleKey({ type: 'mouse', button: 'wheeldown' });
    same(['d', 'e', 'f'], v.visible());
    assert(v.follow);
    same(['d ┃', 'e ┃', 'f ┃'].length, v.render(3).length);
    same(['│', '│', '┃', '┃'], scrollbar(4, 8, 4));
  },
});
