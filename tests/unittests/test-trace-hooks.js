/* lib/traceHooks.js: the registerHooks() tracer, qjsm only */
import * as fs from 'fs';
import { trace } from '../../lib/traceHooks.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

const DIR = fs.mkdtempSync(`${globalThis.process?.env?.TMPDIR ?? '/tmp'}/qjs-trace-hooks-`);

/* a module file that was never imported before: the module cache would hide a second import */
function target(name) {
  fs.writeFileSync(`${DIR}/${name}.js`, 'export default 1;\n');
  return `${DIR}/${name}.js`;
}

tests({
  async 'trace logs the resolve and the load of a file import'() {
    const lines = [];
    const t = trace({ log: l => lines.push(l) });

    try {
      await import(target('one'));
    } finally {
      t.deregister();
    }

    assert(lines.some(l => /^resolve {2}.*\/one\.js <- file:.*test-trace-hooks\.js {3}=> file:.*\/one\.js$/.test(l)), lines.join('\n'));
    assert(lines.some(l => /^load {5}file:.*\/one\.js \(module\)$/.test(l)), lines.join('\n'));
  },

  async 'trace logs a failing import and rethrows'() {
    const lines = [];
    const t = trace({ log: l => lines.push(l) });
    let error;

    try {
      await import(`${DIR}/missing.js`);
    } catch(e) {
      error = e;
    } finally {
      t.deregister();
    }

    assert(error, 'import should fail');
    assert(lines.some(l => / FAIL /.test(l)), lines.join('\n'));
  },

  async 'filter restricts the traced specifiers and urls'() {
    const lines = [];
    const t = trace({ log: l => lines.push(l), filter: /nothing-matches-this/ });

    try {
      await import(target('two'));
    } finally {
      t.deregister();
    }

    eq(0, lines.length);
  },

  async 'deregister stops the tracing'() {
    const lines = [];

    trace({ log: l => lines.push(l) }).deregister();
    await import(target('three'));
    eq(0, lines.length);
  },

  'cleanup'() {
    fs.rmSync(DIR, { recursive: true });
    eq(false, fs.existsSync(DIR));
  },
});
