import { entriesFromIdentifiers } from '../../utilities/remove-dead-functions.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const fn = (name, declaration, references = []) => ({ name, declaration: { kind: 'function', ...declaration }, prototype: [], references });

tests({
  'entriesFromIdentifiers() picks unreferenced functions from -L start,end output'() {
    const e = entriesFromIdentifiers([
      fn('dead', { start: 'src/a.c:12:5', end: 'src/a.c:12:9' }),
      fn('used', { start: 'src/a.c:20:5', end: 'src/a.c:20:9' }, [{ in: 'x', start: 'src/b.c:1:1', end: 'src/b.c:1:5' }]),
      fn('main', { start: 'src/m.c:3:5', end: 'src/m.c:3:9' }),
      { name: 'T', declaration: { kind: 'typedef', start: 'src/a.h:1:1', end: 'src/a.h:1:2' }, prototype: [], references: [] },
    ]);
    eq(JSON.stringify(e), JSON.stringify([{ file: 'src/a.c', name: 'dead', startLine: 12 }]));
  },
  'entriesFromIdentifiers() understands -L range and -L loc output'() {
    const e = entriesFromIdentifiers([
      fn('r', { range: { start: 40, end: 41, file: 'src/r.c' } }),
      fn('l', { loc: { line: 7, column: 3, file: 'src/l.c' } }),
    ]);
    eq(e[0].startOffset, 40);
    eq(e[0].file, 'src/r.c');
    eq(e[1].startLine, 7);
  },
  'entriesFromIdentifiers() takes a file name containing colons from the right'() {
    eq(entriesFromIdentifiers([fn('f', { start: 'C:/x/a.c:3:1' })])[0].file, 'C:/x/a.c');
  },
  'entriesFromIdentifiers() rejects positions without a file'() {
    let msg = '';
    try {
      entriesFromIdentifiers([fn('f', { line: 3 })]);
    } catch(e) {
      msg = e.message;
    }
    assert(/no file/.test(msg));
  },
});
