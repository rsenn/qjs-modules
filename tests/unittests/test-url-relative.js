/* @portable */
/* a relative URL resolves against its base like the WHATWG URL does */
import { eq, tests } from '../../lib/tinytest.js';

const resolve = (rel, base) => new URL(rel, base).pathname;

tests({
  'parent directory'() {
    eq(resolve('../x', 'file:///a/b/c.js'), '/a/x');
  },

  'child path'() {
    eq(resolve('x/y', 'file:///a/b/c.js'), '/a/b/x/y');
    eq(resolve('./x', 'file:///a/b/'), '/a/b/x');
  },

  '.. above the root stays at the root'() {
    eq(resolve('../../../x', 'file:///a/b/c'), '/x');
  },

  'trailing . and .. keep the slash'() {
    eq(resolve('..', 'file:///a/b/c'), '/a/');
    eq(resolve('.', 'file:///a/b/c'), '/a/b/');
  },

  'directory of a script URL'() {
    eq(resolve('../../lib', 'file:///t/tests/unittests/t.js'), '/t/lib');
  },
});
