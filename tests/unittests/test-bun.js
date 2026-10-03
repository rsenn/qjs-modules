/* Bun.plugin(): lib/bun.js on top of registerHooks(), qjsm only */
import { assert, eq, tests } from '../../lib/tinytest.js';

async function rejection(promise) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
}

tests({
  async 'build.module serves a virtual module with exports'() {
    Bun.plugin({ setup: build => build.module('bun-test-virtual', () => ({ exports: { default: 42, answer: 'yes' } })) });

    try {
      const m = await import('bun-test-virtual');

      eq(42, m.default);
      eq('yes', m.answer);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'onResolve plus onLoad in a custom namespace'() {
    Bun.plugin({
      setup(build) {
        build.onResolve({ filter: /^ns:/ }, ({ path }) => ({ path: path.slice(3), namespace: 'custom' }));
        build.onLoad({ filter: /.*/, namespace: 'custom' }, ({ path, namespace }) => ({
          contents: `export default ${JSON.stringify({ path, namespace })};`,
          loader: 'js',
        }));
      },
    });

    try {
      const { default: info } = await import('ns:foo');

      eq('foo', info.path);
      eq('custom', info.namespace);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'onLoad with the json loader'() {
    Bun.plugin({ setup: build => build.onResolve({ filter: /^json:/ }, ({ path }) => ({ path, namespace: 'j' })) });
    Bun.plugin({ setup: build => build.onLoad({ filter: /.*/, namespace: 'j' }, () => ({ contents: '{"a":1}', loader: 'json' })) });

    try {
      eq(1, (await import('json:x')).default.a);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'onLoad with the text loader'() {
    Bun.plugin({
      setup(build) {
        build.onResolve({ filter: /^text:/ }, ({ path }) => ({ path, namespace: 't' }));
        build.onLoad({ filter: /.*/, namespace: 't' }, () => ({ contents: 'hello', loader: 'text' }));
      },
    });

    try {
      eq('hello', (await import('text:x')).default);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'returning undefined falls through to the next plugin'() {
    Bun.plugin({ setup: build => build.module('bun-test-fall', () => undefined) });
    Bun.plugin({
      setup(build) {
        build.onResolve({ filter: /^fall:/ }, () => undefined);
        build.onResolve({ filter: /^fall:/ }, ({ path }) => ({ path, namespace: 'f' }));
        build.onLoad({ filter: /.*/, namespace: 'f' }, () => ({ contents: 'export default 5;', loader: 'js' }));
      },
    });

    try {
      eq(5, (await import('fall:x')).default);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'a loader without a transpiler is a TypeError'() {
    Bun.plugin({
      setup(build) {
        build.onResolve({ filter: /^ts:/ }, ({ path }) => ({ path, namespace: 'ts' }));
        build.onLoad({ filter: /.*/, namespace: 'ts' }, () => ({ contents: 'let x: number = 1', loader: 'ts' }));
      },
    });

    try {
      eq('TypeError', (await rejection(import('ts:x')))?.constructor.name);
    } finally {
      Bun.plugin.clearAll();
    }
  },

  async 'clearAll removes every plugin'() {
    Bun.plugin({ setup: build => build.module('bun-test-cleared', () => ({ exports: { default: 1 } })) });
    Bun.plugin.clearAll();

    eq('ReferenceError', (await rejection(import('bun-test-cleared')))?.constructor.name);
  },

  'plugin() needs a setup function'() {
    let name;

    try {
      Bun.plugin({});
    } catch(e) {
      name = e.constructor.name;
    }

    eq('TypeError', name);
  },

  'filter must be a RegExp'() {
    let name;

    Bun.plugin({
      setup(build) {
        try {
          build.onLoad({ filter: 'x' }, () => {});
        } catch(e) {
          name = e.constructor.name;
        }
      },
    });
    Bun.plugin.clearAll();

    eq('TypeError', name);
  },
});
