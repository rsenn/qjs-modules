import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
import { Console } from 'console';
import { Location, Stack, StackFrame } from '../lib/stack.js';

function Func1() {
  return Func2();
}

function Func2() {
  return Func3();
}

function Func3() {
  return Func4();
}

function Func4() {
  return new Stack();
}

function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      depth: 8,
      breakLength: 100,
      maxStringLength: Infinity,
      maxArrayLength: Infinity,
      compact: 1,
      showHidden: false
    }
  });
  let stack = Func1();
  console.log('stack', stack);
  let frame = stack[0];

  console.log('frame', frame);
  console.log('frame.toString()', frame.toString());
  console.log('stack.toString()', stack.toString());
  console.log(
    `stack.map(fr => fr+'')`,
    [...stack].map(fr => fr + '')
  );
  console.log(
    `stack.map(fr => fr.loc)`,
    [...stack].map(fr => fr.loc)
  );

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
