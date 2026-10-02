import { getOpt } from '../../lib/util.js';
import { eq, tests } from '../../lib/tinytest.js';

const OPTIONS = () => ({
  types: [false, null, 't'],
  list: [false, null, 'l'],
  loc: [true, null, 'L'],
  pattern: [true, null, 'p'],
  '@': 'files',
});

tests({
  'getOpt() parses a long option with a value after a short flag'() {
    const r = getOpt(OPTIONS(), ['-t', '--loc', 'loc,range', 'f.c']);
    eq(r.types, true);
    eq(r.loc, 'loc,range');
    eq(r['@'].join(), 'f.c');
  },
  'getOpt() parses --name=value after a short flag'() {
    const r = getOpt(OPTIONS(), ['-t', '--pattern=^s$', 'f.c']);
    eq(r.pattern, '^s$');
    eq(r['@'].join(), 'f.c');
  },
  'getOpt() still parses short options after long ones'() {
    const r = getOpt(OPTIONS(), ['--loc', 'range', '-t', '-L', 'line', 'f.c']);
    eq(r.types, true);
    eq(r.loc, 'line');
    eq(r['@'].join(), 'f.c');
  },
  'getOpt() splits clustered short flags'() {
    const r = getOpt(OPTIONS(), ['-tl', 'f.c']);
    eq(r.types, true);
    eq(r.list, true);
  },
});
