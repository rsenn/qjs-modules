# CLAUDE.md

Behavioral guidelines to reduce common LLM coding mistakes. Merge with project-specific instructions as needed.

**Tradeoff:** These guidelines bias toward caution over speed. For trivial tasks, use judgment.

## 1. Think Before Coding

**Don't assume. Don't hide confusion. Surface tradeoffs.**

Before implementing:
- State your assumptions explicitly. If uncertain, ask.
- If multiple interpretations exist, present them - don't pick silently.
- If a simpler approach exists, say so. Push back when warranted.
- If something is unclear, stop. Name what's confusing. Ask.

## 2. Simplicity First

**Minimum code that solves the problem. Nothing speculative.**

- No features beyond what was asked.
- No abstractions for single-use code.
- No "flexibility" or "configurability" that wasn't requested.
- No error handling for impossible scenarios.
- If you write 200 lines and it could be 50, rewrite it.

Ask yourself: "Would a senior engineer say this is overcomplicated?" If yes, simplify.

## 3. Surgical Changes

**Touch only what you must. Clean up only your own mess.**

When editing existing code:
- Don't "improve" adjacent code, comments, or formatting.
- Don't refactor things that aren't broken.
- Match existing style, even if you'd do it differently.
- If you notice unrelated dead code, mention it - don't delete it.

When your changes create orphans:
- Remove imports/variables/functions that YOUR changes made unused.
- Don't remove pre-existing dead code unless asked.

The test: Every changed line should trace directly to the user's request.

## 4. Goal-Driven Execution

**Define success criteria. Loop until verified.**

Transform tasks into verifiable goals:
- "Add validation" → "Write tests for invalid inputs, then make them pass"
- "Fix the bug" → "Write a test that reproduces it, then make it pass"
- "Refactor X" → "Ensure tests pass before and after"

For multi-step tasks, state a brief plan:
```
1. [Step] → verify: [check]
2. [Step] → verify: [check]
3. [Step] → verify: [check]
```

Strong success criteria let you loop independently. Weak criteria ("make it work") require constant clarification.

---

**These guidelines are working if:** fewer unnecessary changes in diffs, fewer rewrites due to overcomplication, and clarifying questions come before implementation rather than after mistakes.

---

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
