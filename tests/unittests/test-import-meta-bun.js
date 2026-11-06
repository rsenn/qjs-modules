/* import.meta.dir, path and file of Bun, next to filename and dirname.
 * qjsm and Bun only: Node and Deno have none of the three.
 * runs under bun as well: bun tests/unittests/test-import-meta-bun.js */
import * as dep from '../fixtures/import-meta/dep.js';
import { eq, tests } from '../../lib/tinytest.js';

const { filename, dirname, dir, path, file } = import.meta;

tests({
  'path is filename and dir is dirname'() {
    eq(filename, path);
    eq(dirname, dir);
  },

  'file is the base name of path'() {
    eq('test-import-meta-bun.js', file);
    eq(path.replace(/^.*\//, ''), file);
  },

  'an imported module gets its own dir, path and file'() {
    eq(dir.replace(/unittests$/, 'fixtures/import-meta'), dep.dir);
    eq(dep.dir + '/dep.js', dep.path);
    eq('dep.js', dep.file);
  },
});
