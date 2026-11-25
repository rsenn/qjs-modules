/* util.stripAnsi is terminal.stripAnsi: one implementation, OSC and the other escape kinds included (Tier 31.8).
 * qjsm only: lib/util.js imports qjsm modules. */
import { padAnsi, stripAnsi as utilStrip } from 'util';
import { stripAnsi as termStrip } from 'terminal';
import { assert, eq, tests } from '../../lib/tinytest.js';

const BEL = '\x07',
  ESC = '\x1b',
  ST = '\x1b\\';

tests({
  'util.stripAnsi is terminal.stripAnsi'() {
    eq(true, utilStrip === termStrip);
  },

  'a plain string is unchanged'() {
    eq('hello world', utilStrip('hello world'));
    eq('', utilStrip(''));
    eq('漢字 é', utilStrip('漢字 é'));
  },

  'CSI sequences are removed'() {
    eq('red', utilStrip(`${ESC}[31mred${ESC}[0m`));
    eq('bold', utilStrip(`${ESC}[1;4;38;5;196mbold${ESC}[m`));
    eq('ab', utilStrip(`a${ESC}[2K${ESC}[10;20Hb`));
    eq('x', utilStrip(`${ESC}[?25lx${ESC}[?25h`));
  },

  'an OSC sequence goes with its text, BEL or ST terminated'() {
    eq('x', utilStrip(`${ESC}]0;title${BEL}x`));
    eq('x', utilStrip(`${ESC}]0;title${ST}x`));
    eq('link', utilStrip(`${ESC}]8;;https://example.com${BEL}link${ESC}]8;;${BEL}`));
  },

  'the old regex left the text of an OSC behind'() {
    const old = s => (s + '').replace(new RegExp('\x1b[[(?);]{0,2}(;?[0-9])*.', 'g'), '');

    eq('0;t\x07', old(`${ESC}]0;t${BEL}`));
    eq('', utilStrip(`${ESC}]0;t${BEL}`));
  },

  'DCS, SOS, PM and APC strings are removed'() {
    eq('x', utilStrip(`${ESC}Pq#0;2;0;0;0${ST}x`));
    eq('x', utilStrip(`${ESC}_Gi=1;data${ST}x`));
    eq('x', utilStrip(`${ESC}^private${ST}x`));
    eq('x', utilStrip(`${ESC}Xsos${ST}x`));
  },

  'two-byte escapes are removed'() {
    eq('ab', utilStrip(`a${ESC}7${ESC}8b`));
    eq('ab', utilStrip(`a${ESC}(Bb`));
    eq('ab', utilStrip(`a${ESC}=b${ESC}>`));
  },

  'a non-string is converted'() {
    eq('42', utilStrip(42));
    eq('null', utilStrip(null));
    eq('x', utilStrip({ toString: () => `${ESC}[1mx${ESC}[0m` }));
  },

  'both give the same result on a mix of sequences'() {
    const samples = [`${ESC}[31ma${ESC}]0;t${BEL}b${ESC}Pq${ST}c`, `${ESC}[`, `${ESC}`, `a${ESC}[1`, `${ESC}]unterminated`, `\r\n\t`, `${ESC}[38;2;1;2;3mrgb${ESC}[0m`];

    for(const s of samples) eq(termStrip(s), utilStrip(s));
  },

  'padAnsi counts the visible text only, an OSC title adds no width'() {
    eq('   ', padAnsi(`${ESC}]0;title${BEL}ab`, 5));
    eq('   ', padAnsi(`${ESC}[31mab${ESC}[0m`, 5));
  },
});
