import { add_completion_callback } from './testharness.js';
import { setup } from './testharness.js';

export * from './testharness.js';

/* global add_completion_callback */
/* global setup */

/*
 * This file is intended for vendors to implement code needed to integrate
 * testharness.js tests with their own test systems.
 *
 * Typically test system integration will attach callbacks when each test has
 * run, using add_result_callback(callback(test)), or when the whole test file
 * has completed, using
 * add_completion_callback(callback(tests, harness_status)).
 *
 * For more documentation about the callback functions and the
 * parameters they are called with see testharness.js
 */

export function dump_test_results(tests, status) {
  const test_results = tests.map(function (x) {
    return { name: x.name, status: x.status, message: x.message, stack: Stack(x.stack) };
  });

  if(typeof document != 'undefined') {
    const results_element = document.createElement('script');

    results_element.type = 'text/json';
    results_element.id = '__testharness__results__';

    const data = {
      test: window.location.href,
      tests: test_results,
      status: status.status,
      message: status.message,
      stack: status.stack,
    };

    results_element.textContent = JSON.stringify(data);

    // To avoid a HierarchyRequestError with XML documents, ensure that 'results_element'
    // is inserted at a location that results in a valid document.
    const parent = document.body
      ? document.body // <body> is required in XHTML documents
      : document.documentElement; // fallback for optional <body> in HTML5, SVG, etc.

    parent.appendChild(results_element);
  } else {
    const data = {
      test: __filename,
      passed: test_results.filter(r => r.status === 0).length,
      failed: test_results.filter(r => r.status !== 0).length,
      tests: test_results,
      status: status.status,
      message: status.message,
      stack: Stack(status.stack),
    };

    Object.assign(globalThis, { testharnessreport: data });

    console.log(`Testharness report:`, console.config({ compact: 2, hideKeys: ['tests', 'message', 'stack'] }), data);
  }
}

function dump_test_result(test) {
  globalThis.test = test;
  console.log(`Test ` + `#${test.index}: ${test.status === 0 ? '\x1b[0;32mOK' : '\x1b[1;31mFAIL'} \x1b[1;30m${test.name}\x1b[0m`);
}

add_completion_callback(dump_test_results);
add_result_callback(dump_test_result);
/* If the parent window has a testharness_properties object,
 * we use this to provide the test settings. This is used by the
 * default in-browser runner to configure the timeout and the
 * rendering of results
 */
try {
  if(window.opener && 'testharness_properties' in window.opener) {
    /* If we pass the testharness_properties object as-is here without
     * JSON stringifying and reparsing it, IE fails & emits the message
     * "Could not complete the operation due to error 80700019".
     */
    setup(JSON.parse(JSON.stringify(window.opener.testharness_properties)));
  }
} catch(e) {}
// vim: set expandtab shiftwidth=4 tabstop=4:

function Stack(s) {
  if(s === null || s === undefined) return s;

  const re = /\s*at\s+([^(]+)\s\(([^):]*)(?::|)(\d+|)\)\s*/gm;

  return Object.setPrototypeOf(
    [...s.matchAll(re)].map(([, fn, file, line]) => [fn, file, line === '' ? undefined : +line]),
    Stack.prototype,
  );
}

Stack.prototype = [];
Stack.prototype[Symbol.toStringTag] = 'Stack';