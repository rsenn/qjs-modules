// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* import.meta.filename, dirname and main, as on Node, Bun and Deno */
import * as dep from '../fixtures/import-meta/dep.js';
import { eq, tests } from '../../lib/tinytest.js';

const { url, filename, dirname, main } = import.meta;

tests({
  'filename is the path of url'() {
    eq(decodeURIComponent(url.replace(/^file:\/\//, '')), filename);
  },

  'dirname is filename without its last component'() {
    eq(filename.replace(/\/[^/]*$/, ''), dirname);
  },

  'an imported module gets its own filename and dirname'() {
    eq(dirname.replace(/unittests$/, 'fixtures/import-meta'), dep.dirname);
    eq(dep.dirname + '/dep.js', dep.filename);
  },

  /* node before 24.2 has no import.meta.main */
  'main is true for the entry module only'() {
    if(main === undefined)
      return;

    eq(true, main);
    eq(false, dep.main);
  },
});
