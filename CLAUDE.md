# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

A collection of **native C modules for QuickJS** (`quickjs-*.c`/`.h` bindings, e.g. `stream`,
`xml`, `deep`, `dom`, `json`), plus JS-side wrappers and helpers in `lib/*.js`. Built via CMake;
tests live in `tests/test_*.js` and run under `qjs`/`qjsm`, wired up as CTest cases
(`add_test` in `CMakeLists.txt`).

## Tracking work — always keep these current

Outstanding work in this repo lives in three files, not in ad-hoc notes or memory. Whenever a
session finds a bug, fixes one, or turns up new unfinished work, **update the relevant file
before the session ends** — don't leave it for the user to transcribe later.

- **`TODO.md`** — the authoritative, actively maintained tracker. Items are tiered by leverage
  (highest-impact/cheapest fixes first) and each one is *verified against the code* (file:line,
  concrete failure, repro where applicable), not just grepped or guessed. This is where new
  bugs/gaps discovered by reading or running the code get added, and where fixed items get
  removed or marked done.
- **`TODO`** (no extension) — legacy sparse list, superseded by `TODO.md`. Don't add new items
  here; if you touch something on this list, fold the item into `TODO.md` and note it as
  folded-in, same as the existing entries.
- **`BUGS`** — plaintext, bugs found *incidentally* while doing other work (e.g. while writing
  unit tests) and deliberately left unfixed at the time. Add to this file when you find a bug
  but the task at hand isn't "fix bugs" — don't fix it out of scope, but don't lose it either.
  Append newly discovered bugs to the **end** of the file, in the order found, wrapped to 78
  columns, all lowercase, in this format:
  ```
  - <canonical-kebab-case-name>: <prose description, all lowercase, wrapped to 78
    columns, covering what's wrong, why, and where (file:line) it was found>

      <minimal JS (or build/shell) snippet that triggers/reproduces it, 4-space
      indented, with a comment showing the actual vs. expected result>
  ```
  If no isolated repro exists yet (e.g. a suspected leak or a build-only failure), say so
  explicitly in the prose rather than inventing one.

When in doubt about where an item goes: a verified, actionable fix → `TODO.md`; a bug noticed
in passing that's out of scope for the current task → `BUGS`.

## Build / test

```sh
cmake -B build/$(cc -dumpmachine) -S .
cmake --build build/$(cc -dumpmachine)
ctest --test-dir build/$(cc -dumpmachine)
```

Individual tests can also be run directly, e.g. `qjs tests/test_stream.js`.
