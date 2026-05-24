# testharnessreport

Source: `lib/testharnessreport.js` (pure JS)

Reporting layer for the [`testharness`](testharness.md) module: collects results
and renders them. Re-exports the whole testharness API.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| *(re-export)* | — | — | `export * from './testharness.js'`. |
| `dump_test_results(tests, status)` | 2 | function | Formats and emits the collected test results and overall status. |
