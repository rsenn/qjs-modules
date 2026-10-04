/* import attributes: `with { type: 'json' }`; qjsm checks the keys */
import * as os from 'os';
import * as std from 'std';
import { assert, eq, tests } from '../../lib/tinytest.js';

async function outcome(promise) {
  try {
    return { module: await promise };
  } catch(e) {
    return { error: e };
  }
}

const json = 'data:application/json,{"a":1}';

tests({
  async 'type: json imports a JSON module'() {
    const { module, error } = await outcome(import(json, { with: { type: 'json' } }));

    eq(undefined, error);
    eq(1, module.default.a);
  },

  async 'an unsupported key in a dynamic import is a TypeError'() {
    const { error } = await outcome(import(json, { with: { foo: 'bar' } }));

    assert(error instanceof TypeError, String(error));
    assert(/'foo' is not supported/.test(error.message), error.message);
  },

  async 'an unsupported key in a static import is a TypeError, whatever else is listed'() {
    for(const attrs of ['{ foo: "bar" }', '{ type: "json", foo: "bar" }']) {
      const { error } = await outcome(import(`data:text/javascript,import d from "data:application/json,1" with ${attrs}; export default d`));

      assert(error instanceof TypeError && /'foo' is not supported/.test(error.message), attrs + ': ' + error);
    }
  },

  async 'a static import with type: json works'() {
    const { module, error } = await outcome(import('data:text/javascript,import d from "data:application/json,[1,2]" with { type: "json" }; export default d'));

    eq(undefined, error);
    eq('1,2', module.default.join());
  },

  async 'type: json is honored for a file whose name does not say json'() {
    const file = '/tmp/test-import-attributes-' + os.getpid() + '.dat';
    const f = std.open(file, 'w');

    f.puts('{"b":2}');
    f.close();

    try {
      const { module, error } = await outcome(import(file, { with: { type: 'json' } }));

      eq(undefined, error);
      eq(2, module.default.b);
    } finally {
      os.remove(file);
    }
  },

  async 'only the keys are checked: the value of type and a missing type are the loader\'s business'() {
    const { error } = await outcome(import(json, { with: { type: 'json' } }));
    const bare = await outcome(import(json));

    eq(undefined, error);
    eq(undefined, bare.error);
  },
});
