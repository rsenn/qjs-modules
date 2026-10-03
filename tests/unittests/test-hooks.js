/* registerHooks(): Node's synchronous module.registerHooks, qjsm only */
import { registerHooks } from 'module';
import { assert, eq, tests } from '../../lib/tinytest.js';

async function rejection(promise) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
}

const virtual = (name, source) => ({
  resolve: (specifier, context, next) => (specifier === name ? { url: name, shortCircuit: true } : next(specifier, context)),
  load: (url, context, next) => (url === name ? { format: 'module', source, shortCircuit: true } : next(url, context)),
});

tests({
  async 'a short-circuiting hook can serve a virtual module'() {
    const hooks = registerHooks(virtual('virtual:a', 'export default 7;'));

    try {
      eq(7, (await import('virtual:a')).default);
    } finally {
      hooks.deregister();
    }
  },

  async 'unhandled specifiers fall through to the engine'() {
    const hooks = registerHooks(virtual('virtual:b', 'export default 1;'));

    try {
      eq('function', typeof (await import('node:path')).join);
    } finally {
      hooks.deregister();
    }
  },

  async 'deregister() removes the hooks'() {
    registerHooks(virtual('virtual:c', 'export default 1;')).deregister();

    eq('ReferenceError', (await rejection(import('virtual:c')))?.constructor.name);
  },

  async 'hooks run last-registered first and next() reaches the older one'() {
    const order = [];
    const older = registerHooks({
      ...virtual('virtual:d', 'export default 1;'),
      load(url, context, next) {
        order.push('older');
        return url === 'virtual:d' ? { format: 'module', source: 'export default 1;', shortCircuit: true } : next(url, context);
      },
    });
    const newer = registerHooks({
      load(url, context, next) {
        order.push('newer');
        return next(url, context);
      },
    });

    try {
      await import('virtual:d');
      eq('newer,older', order.join());
    } finally {
      newer.deregister();
      older.deregister();
    }
  },

  async 'a load hook can rewrite the source returned by next()'() {
    const provider = registerHooks(virtual('virtual:e', 'export default 1;'));
    const rewriter = registerHooks({
      load(url, context, next) {
        const result = next(url, context);

        return url === 'virtual:e' ? { ...result, source: String(result.source).replace('1', '2') } : result;
      },
    });

    try {
      eq(2, (await import('virtual:e')).default);
    } finally {
      rewriter.deregister();
      provider.deregister();
    }
  },

  async 'resolve can hand a format to load'() {
    let seen;
    const hooks = registerHooks({
      resolve: (specifier, context, next) => (specifier === 'virtual:f' ? { url: 'virtual:f', format: 'json', shortCircuit: true } : next(specifier, context)),
      load(url, context, next) {
        if(url !== 'virtual:f') return next(url, context);

        seen = context.format;
        return { format: 'json', source: '{"k":[1,2]}', shortCircuit: true };
      },
    });

    try {
      eq('1,2', (await import('virtual:f')).default.k.join());
      eq('json', seen);
    } finally {
      hooks.deregister();
    }
  },

  async 'source may be a Uint8Array'() {
    const bytes = Uint8Array.from('export default "bytes";', c => c.charCodeAt(0));
    const hooks = registerHooks(virtual('virtual:g', bytes));

    try {
      eq('bytes', (await import('virtual:g')).default);
    } finally {
      hooks.deregister();
    }
  },

  async 'a hook that neither calls next nor short-circuits is an error'() {
    const hooks = registerHooks({ resolve: specifier => ({ url: specifier }) });

    try {
      eq('ERR_LOADER_CHAIN_INCOMPLETE', (await rejection(import('virtual:h')))?.code);
    } finally {
      hooks.deregister();
    }
  },

  async 'an exception in a hook rejects the import'() {
    const hooks = registerHooks({
      resolve(specifier, context, next) {
        if(specifier === 'virtual:i') throw new RangeError('boom');
        return next(specifier, context);
      },
    });

    try {
      const error = await rejection(import('virtual:i'));

      assert(error && /boom/.test(error.message), String(error));
    } finally {
      hooks.deregister();
    }
  },

  async 'an unsupported format is a TypeError'() {
    const hooks = registerHooks({
      ...virtual('virtual:j', ''),
      load: (url, context, next) => (url === 'virtual:j' ? { format: 'wasm', source: new Uint8Array(0), shortCircuit: true } : next(url, context)),
    });

    try {
      eq('TypeError', (await rejection(import('virtual:j')))?.constructor.name);
    } finally {
      hooks.deregister();
    }
  },

  'registerHooks needs a function member'() {
    let name;

    try {
      registerHooks({});
    } catch(e) {
      name = e.constructor.name;
    }

    eq('TypeError', name);
  },

  async 'resolve sees the importer as a file URL'() {
    let parent;
    const hooks = registerHooks({
      resolve(specifier, context, next) {
        if(specifier === 'virtual:k') {
          parent = context.parentURL;
          return { url: 'virtual:k', shortCircuit: true };
        }

        return next(specifier, context);
      },
      load: (url, context, next) => (url === 'virtual:k' ? { format: 'module', source: '', shortCircuit: true } : next(url, context)),
    });

    try {
      await import('virtual:k');
      assert(parent === undefined || /^file:\/\/\//.test(parent), String(parent));
    } finally {
      hooks.deregister();
    }
  },
});
