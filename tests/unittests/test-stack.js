import { Stack, StackFrame } from '../../lib/stack.js';
import { assert, eq, tests } from '../../lib/tinytest.js';

function outer() {
  return middle();
}

function middle() {
  return inner();
}

function inner() {
  return new Stack();
}

tests({
  'Stack captures the calling frames, innermost first'() {
    const stack = outer();

    eq('inner', stack[0].functionName);
    eq('middle', stack[1].functionName);
    eq('outer', stack[2].functionName);
  },

  'length matches the captured frames and Stack is iterable'() {
    const stack = outer();

    assert(stack.length >= 3, `length ${stack.length}`);
    eq(stack.length, [...stack].length);
  },

  'frames carry file name and positive line and column'() {
    const frame = outer()[0];

    assert(/test-stack\.js$/.test(frame.fileName), frame.fileName);
    assert(frame.lineNumber > 0, `line ${frame.lineNumber}`);
    assert(frame.columnNumber > 0, `column ${frame.columnNumber}`);
  },

  'frame.toString() names the function and location'() {
    const text = outer()[0].toString();

    assert(text.startsWith('inner ('), text);
    assert(/test-stack\.js:\d+:\d+\)$/.test(text), text);
  },

  'stack.toString() has one line per frame'() {
    const stack = outer();

    eq(stack.length, stack.toString().split('\n').length);
  },

  'a predicate filters frames'() {
    const stack = new Stack(undefined, fr => fr.functionName == 'outer' || fr.functionName == 'inner');
    const names = [...stack].map(fr => fr.functionName);

    assert(!names.includes('middle'), names.join());
  },

  'a stack can be built from a stack string'() {
    const stack = new Stack('f (a.js:10:5)\ng (b.js:20:7)');

    eq(2, stack.length);
    eq('f', stack[0].functionName);
    eq(10, stack[0].lineNumber);
    eq('b.js', stack[1].fileName);
  },

  'StackFrame is exported'() {
    eq('function', typeof StackFrame);
  },
});
