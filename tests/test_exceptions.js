import * as os from 'os';
import * as path from 'path';
import * as fs from 'fs';
import * as std from 'std';
import { toString } from 'util';
import { assert, tests } from './tinytest.js';

/*
 * For every kind of exception a script can raise, spawn a fresh `qjsm` child process on a
 * throwaway script and check that the combined stdout+stderr it printed actually locates the
 * error: at minimum the throw site's `file:line` must appear somewhere in the output, and
 * (per-scenario) every "    at ..." frame in the printed stack should carry a line number,
 * not just a bare filename.
 */

const TMPDIR = std.getenv('TMPDIR') ?? '/tmp';
const ROOT = path.join(TMPDIR, `qjs-exceptions-test-${Date.now()}-${Math.floor(Math.random() * 1e6)}`);

function rmrf(p) {
  let st;
  try {
    st = fs.lstatSync(p);
  } catch(e) {
    return;
  }
  if(st.isDirectory()) {
    for(const name of fs.readdirSync(p)) if(name != '.' && name != '..') rmrf(path.join(p, name));
  }
  fs.unlinkSync(p);
}

fs.mkdirSync(ROOT);

function runScript(name, code) {
  const file = path.join(ROOT, name + '.js');
  fs.writeFileSync(file, code);

  const [rfd, wfd] = os.pipe();
  const devnull = os.open('/dev/null', os.O_RDONLY);

  const status = os.exec([process.execPath, file], {
    block: true,
    stdin: devnull,
    stdout: wfd,
    stderr: wfd,
  });

  os.close(wfd);
  os.close(devnull);

  let output = '';
  const buf = new ArrayBuffer(4096);
  for(;;) {
    const n = os.read(rfd, buf, 0, buf.byteLength);
    if(n <= 0) break;
    output += toString(buf.slice(0, n));
  }
  os.close(rfd);

  return { status, output, file };
}

function frameLines(output) {
  return output.split('\n').filter(l => /^\s*at\s/.test(l));
}

function assertLocated(output, file, line, label) {
  const marker = `${file}:${line}`;
  assert(output.includes(marker), `${label}: expected output to contain '${marker}'\n--- output ---\n${output}`);
}

function assertEveryFrameLocated(output, label) {
  const frames = frameLines(output);
  assert(frames.length > 0, `${label}: expected at least one "at ..." stack frame\n--- output ---\n${output}`);
  for(const l of frames) assert(/:\d+/.test(l), `${label}: frame has no line number: '${l.trim()}'\n--- output ---\n${output}`);
}

try {
  /* tinytest's `tests()` is meant to be called fire-and-forget at module scope (see its own
     header comment), but that leaves the very next synchronous statement - this `finally` -
     running before any test body past the first has actually executed, deleting ROOT out from
     under them. Await it explicitly instead so cleanup only happens once every test is done. */
  await tests({
    'synchronous throw at top level'() {
      const { output, file } = runScript(
        'sync-top-level',
        `throw new Error("boom");\n`,
      );
      assertLocated(output, file, 1, 'sync-top-level');
    },

    'synchronous throw inside nested function calls'() {
      const { output, file } = runScript(
        'sync-nested',
        'function foo() {\n' +
        '  throw new Error("boom");\n' +
        '}\n' +
        'function bar() {\n' +
        '  foo();\n' +
        '}\n' +
        'bar();\n',
      );
      assertLocated(output, file, 2, 'sync-nested (throw site, foo)');
      assertEveryFrameLocated(output, 'sync-nested');
    },

    'synchronous throw inside a single-line function body'() {
      /* Same as the nested-function case above, except foo's entire body - including the
         throw - sits on the same source line as its declaration. That alone is enough to
         make find_line_num() come up empty for foo/bar's frames: they print just the bare
         filename with no ':<line>' at all (only the outermost frame keeps its line number),
         unlike the identical code split across lines above. */
      const { output, file } = runScript(
        'sync-nested-oneline',
        'function foo() { throw new Error("boom"); }\n' +
        'function bar() { foo(); }\n' +
        'bar();\n',
      );
      assertEveryFrameLocated(output, 'sync-nested-oneline');
    },

    'throwing a non-Error value'() {
      /* A thrown non-Error value has no .stack (only Error instances get one), so unlike
         every other case here there is no file:line to expect - just the value itself. */
      const { output } = runScript(
        'throw-primitive',
        `throw "just a string";\n`,
      );
      assert(output.includes('just a string'), `throw-primitive: expected message in output\n--- output ---\n${output}`);
    },

    'builtin TypeError (null property access)'() {
      const { output, file } = runScript(
        'type-error',
        `null.foo;\n`,
      );
      assertLocated(output, file, 1, 'type-error');
      assert(output.includes('TypeError'), `type-error: expected 'TypeError' in output\n--- output ---\n${output}`);
    },

    'builtin ReferenceError (undefined variable)'() {
      const { output, file } = runScript(
        'reference-error',
        `thisVariableDoesNotExist;\n`,
      );
      assertLocated(output, file, 1, 'reference-error');
      assert(output.includes('ReferenceError'), `reference-error: expected 'ReferenceError' in output\n--- output ---\n${output}`);
    },

    'builtin RangeError (invalid array length)'() {
      const { output, file } = runScript(
        'range-error',
        `new Array(-1);\n`,
      );
      assertLocated(output, file, 1, 'range-error');
      assert(output.includes('RangeError'), `range-error: expected 'RangeError' in output\n--- output ---\n${output}`);
    },

    'throw inside a class constructor'() {
      const { output, file } = runScript(
        'class-constructor',
        'class Foo {\n' +
        '  constructor() {\n' +
        '    throw new Error("ctor");\n' +
        '  }\n' +
        '}\n' +
        'new Foo();\n',
      );
      assertLocated(output, file, 3, 'class-constructor (throw site)');
    },

    'throw inside an async function (unhandled rejection)'() {
      const { output, file } = runScript(
        'async-throw',
        'async function foo() {\n' +
        '  throw new Error("async-boom");\n' +
        '}\n' +
        'foo();\n',
      );
      assertLocated(output, file, 2, 'async-throw');
      assert(output.includes('unhandled promise'), `async-throw: expected an unhandled-rejection report\n--- output ---\n${output}`);
    },

    'throw inside a Promise executor'() {
      const { output, file } = runScript(
        'promise-executor',
        'new Promise((resolve, reject) => {\n' +
        '  throw new Error("executor-boom");\n' +
        '});\n',
      );
      assertLocated(output, file, 2, 'promise-executor');
    },

    'throw inside an unobserved .then() callback'() {
      const { output, file } = runScript(
        'promise-then',
        'Promise.resolve().then(() => {\n' +
        '  throw new Error("then-boom");\n' +
        '});\n',
      );
      assertLocated(output, file, 2, 'promise-then');
      assert(output.includes('unhandled promise'), `promise-then: expected an unhandled-rejection report\n--- output ---\n${output}`);
    },

    'throw inside a setTimeout callback'() {
      const { output, file } = runScript(
        'timer-throw',
        'import { setTimeout } from "os";\n' +
        'setTimeout(() => {\n' +
        '  throw new Error("timer-boom");\n' +
        '}, 0);\n',
      );
      assertLocated(output, file, 3, 'timer-throw');
    },

    'throw inside a generator, at next()'() {
      const { output, file } = runScript(
        'generator-throw',
        'function* gen() {\n' +
        '  throw new Error("gen-boom");\n' +
        '}\n' +
        'gen().next();\n',
      );
      assertLocated(output, file, 2, 'generator-throw (throw site)');
    },

    'rethrow after catch'() {
      const { output, file } = runScript(
        'rethrow',
        'try {\n' +
        '  throw new Error("first");\n' +
        '} catch(e) {\n' +
        '  throw new Error("rethrown: " + e.message);\n' +
        '}\n',
      );
      assertLocated(output, file, 4, 'rethrow (rethrow site)');
    },

    'importing a nonexistent module'() {
      const { output, file } = runScript(
        'bad-import',
        `import "this_module_does_not_exist_xyz";\n`,
      );
      assert(output.includes('could not load module'), `bad-import: expected a module-load error\n--- output ---\n${output}`);
      assert(output.includes(path.basename(file)), `bad-import: expected the failing file name in output\n--- output ---\n${output}`);
    },
  });
} finally {
  rmrf(ROOT);
}
