# testharness

Source: `lib/testharness.js` (pure JS)

A port of the W3C **testharness.js** (web-platform-tests) framework: define
tests, make assertions, and collect results. See also
[`testharnessreport`](testharnessreport.md) for output.

> Large module; functions grouped by role below.

## Defining & running tests

| Function | Args | Description |
| --- | --- | --- |
| `test(func, name, properties)` | 1–3 | Defines a synchronous test. |
| `async_test(func, name, properties)` | 1–3 | Defines an async test (manual `done`). |
| `promise_test(func, name, properties)` | 1–3 | Defines a promise-returning test. |
| `generate_tests(func, args, properties)` | 2–3 | Generates many tests from argument tuples. |
| `setup(func_or_properties, maybe_properties)` | 1–2 | Configures the harness before tests run. |
| `promise_setup(func, properties)` | 1–2 | Async setup. |
| `done()` | 0 | Signals that all tests are defined/finished. |
| `timeout()` | 0 | Forces the harness timeout. |
| `step_timeout(func, timeout)` | 2 | Schedules a callback as a test step. |
| `on_event(object, event, callback)` | 3 | Adds an event listener (harness helper). |

## Promise rejection assertions

`promise_rejects_js(test, constructor, promise, description)`,
`promise_rejects_dom(test, type, promiseOr, …)`,
`promise_rejects_exactly(test, exception, promise, description)`.

## Assertions

Truthiness / equality: `assert_true`, `assert_false`, `assert_equals`,
`assert_not_equals`, `assert_in_array`, `assert_object_equals`,
`assert_array_equals`, `assert_array_approx_equals`, `assert_approx_equals`.

Ordering: `assert_less_than`, `assert_greater_than`, `assert_less_than_equal`,
`assert_greater_than_equal`, `assert_between_exclusive`,
`assert_between_inclusive`.

Strings / classes: `assert_regexp_match`, `assert_class_string`.

Properties: `assert_own_property`, `assert_not_own_property`, `assert_inherits`,
`assert_idl_attribute`, `assert_readonly`.

Throwing: `assert_throws_js`, `assert_throws_dom`, `assert_throws_exactly`.

Control / combinators: `assert_unreached`, `assert_any`, `assert_implements`,
`assert_implements_optional`.

## Result callbacks

`add_start_callback`, `add_test_state_callback`, `add_result_callback`,
`add_completion_callback`.

## Cross-context test collection

`fetch_tests_from_worker(port)`, `fetch_tests_from_window(window)`,
`fetch_tests_from_shadow_realm(realm)`, `begin_shadow_realm_tests(postMessage)`.

## Test environments

`WindowTestEnvironment`, `WorkerTestEnvironment`,
`SharedWorkerTestEnvironment`, `ServiceWorkerTestEnvironment`,
`ShadowRealmTestEnvironment`, `ShellTestEnvironment`.

## Core types & helpers

| Export | Description |
| --- | --- |
| `Test`, `RemoteTest`, `RemoteContext` | Test object and remote-test plumbing. |
| `Tests`, `TestsStatus`, `AssertRecord` | Test registry, overall status, recorded assertions. |
| `Output` | Result formatter/sink. |
| `EventWatcher(test, node, eventTypes, timeoutPromise)` | Awaits a sequence of events. |
| `format_value(val, seen)` | Renders a value for messages. |
| `AssertionError(message)` | Assertion failure error. |
| `OptionalFeatureUnsupportedError(message)` | Skipped-feature error. |
| `tests` | The live test list (`let`). |
