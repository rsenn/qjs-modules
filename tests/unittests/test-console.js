import { Console } from '../../lib/console.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

function capture(inspectOptions) {
  const out = [];
  const stream = { write() {}, puts: s => out.push(s) };

  return { out, console: new Console(stream, { inspectOptions: { colors: false, ...inspectOptions } }) };
}

tests({
  'log joins arguments with spaces and ends the line'() {
    const { out, console } = capture();

    console.log('a', 1, true);
    eq('a 1 true\n', out.join(''));
  },

  'log, info, warn, error and debug all write'() {
    const { out, console } = capture();

    for(const method of ['log', 'info', 'warn', 'error', 'debug']) console[method](method);

    eq('log\ninfo\nwarn\nerror\ndebug\n', out.join(''));
  },

  'objects are inspected'() {
    const { out, console } = capture({ compact: Infinity, breakLength: Infinity });

    console.log({ x: 1, y: [1, 2] });
    assert(/x: 1/.test(out.join('')) && /y: \[/.test(out.join('')), out.join(''));
  },

  'circular references are marked, not followed'() {
    const { out, console } = capture();
    const o = { n: 1 };

    o.self = o;
    console.log(o);
    assert(out.join('').includes('[loop]'), out.join(''));
  },

  'numberBase changes how numbers print'() {
    const { out, console } = capture({ numberBase: 16 });

    console.log(255);
    assert(/ff/i.test(out.join('')), out.join(''));
  },

  'options reflect the inspectOptions passed in'() {
    const { console } = capture({ maxArrayLength: 3 });

    eq(3, console.options.maxArrayLength);
  },

  'maxArrayLength truncates long arrays'() {
    const { out, console } = capture({ maxArrayLength: 2, compact: Infinity, breakLength: Infinity });

    console.log([1, 2, 3, 4, 5]);
    assert(!out.join('').includes('5'), out.join(''));
  },
});
