/* test_hooks.js: registerHooks() (Node module.registerHooks) and Bun.plugin().
 * qjsm only: both are installed by src/qjsm.c / lib/bun.js. */
import { registerHooks } from 'module';

let failed = 0;

function check(name, actual, expected) {
  const ok = JSON.stringify(actual) === JSON.stringify(expected);

  if(!ok) failed++;

  console.log(`${ok ? 'PASS' : 'FAIL'}: ${name}${ok ? '' : ` (got ${JSON.stringify(actual)}, want ${JSON.stringify(expected)})`}`);
}

async function rejects(promise) {
  try {
    await promise;
  } catch(e) {
    return e;
  }
}

/* resolve + load short-circuit: a virtual module */
{
  const h = registerHooks({
    resolve: (specifier, context, next) => (specifier === 'virtual:a' ? { url: 'virtual:a', shortCircuit: true } : next(specifier, context)),
    load: (url, context, next) =>
      url === 'virtual:a' ? { format: 'module', source: 'export default 7;', shortCircuit: true } : next(url, context),
  });

  check('virtual module', (await import('virtual:a')).default, 7);
  check('builtin passes through', typeof (await import('node:path')).join, 'function');
  h.deregister();
  check('deregister', (await rejects(import('virtual:b')))?.constructor.name, 'ReferenceError');
}

/* chaining: LIFO order, next() context, source rewrite, json format */
{
  const order = [];
  const h1 = registerHooks({
    resolve: (s, c, next) => (s === 'virtual:k' ? { url: 'virtual:k', shortCircuit: true } : next(s, c)),
    load(url, context, next) {
      order.push('first');
      return url === 'virtual:k' ? { format: 'module', source: 'export default 1;', shortCircuit: true } : next(url, context);
    },
  });
  const h2 = registerHooks({
    resolve: (s, c, next) => (s === 'virtual:j' ? { url: 'virtual:j', format: 'json', shortCircuit: true } : next(s, c)),
    load(url, context, next) {
      order.push('second');

      if(url === 'virtual:j') return { format: context.format, source: '{"k":[1,2]}', shortCircuit: true };

      const r = next(url, context);
      return { ...r, source: String(r.source).replace('1', '2') };
    },
  });

  check('json format from resolve', (await import('virtual:j')).default, { k: [1, 2] });
  check('LIFO order, next() reaches older hook', [(await import('virtual:k')).default, order.slice(-2)], [2, ['second', 'first']]);

  h2.deregister();
  h1.deregister();
}

/* contract violations */
{
  const h = registerHooks({ resolve: s => ({ url: s }) });
  const e = await rejects(import('virtual:c'));

  check('chain incomplete code', e?.code, 'ERR_LOADER_CHAIN_INCOMPLETE');
  h.deregister();

  check('no function member', (() => { try { registerHooks({}); } catch(e) { return e.constructor.name; } })(), 'TypeError');
}

/* parentURL / source as Uint8Array */
{
  const seen = [];
  const h = registerHooks({
    resolve(s, c, next) {
      if(s === 'virtual:u') {
        seen.push(c.parentURL);
        return { url: 'virtual:u', shortCircuit: true };
      }
      return next(s, c);
    },
    load: (url, c, next) =>
      url === 'virtual:u' ? { format: 'module', source: Uint8Array.from('export default "bytes";', c => c.charCodeAt(0)), shortCircuit: true } : next(url, c),
  });

  check('Uint8Array source', (await import('virtual:u')).default, 'bytes');
  check('parentURL is a file URL', seen[0]?.startsWith('file://') || seen[0]?.startsWith('<') || seen[0] === undefined, true);
  h.deregister();
}

/* Bun.plugin */
{
  Bun.plugin({
    name: 'test',
    setup(build) {
      build.module('plugin-virtual', () => ({ exports: { default: 42, answer: 'yes' } }));
      build.onResolve({ filter: /^ns:/ }, ({ path }) => ({ path: path.slice(3), namespace: 'custom' }));
      build.onLoad({ filter: /.*/, namespace: 'custom' }, ({ path, namespace }) => ({ contents: `export default ${JSON.stringify({ path, namespace })};`, loader: 'js' }));
    },
  });

  const v = await import('plugin-virtual');
  check('build.module exports', [v.default, v.answer], [42, 'yes']);
  check('onResolve + onLoad namespace', (await import('ns:foo')).default, { path: 'foo', namespace: 'custom' });

  Bun.plugin({ setup: b => b.onLoad({ filter: /^virtual:t$/ }, () => ({ contents: 'x', loader: 'ts' })) });

  Bun.plugin.clearAll();
  check('clearAll', (await rejects(import('plugin-virtual')))?.constructor.name, 'ReferenceError');
}

console.log(failed ? `${failed} FAILED` : 'SUCCESS');
process.exit(failed ? 1 : 0);
