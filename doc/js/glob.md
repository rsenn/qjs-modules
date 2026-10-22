# glob

Source: `lib/glob.js` (pure JS, runs under node, bun and deno too) — exports `Glob` (also the default) and `globToRegExp`

Glob patterns with the rules of `Bun.Glob`, which is this class. `match()` compares strings only; dot files and
the file system matter only to `scanSync()` and `scan()`. The matching rules were checked against bun 1.4.2 on 21
patterns times 27 paths.

```js
import { Glob } from 'glob';

new Glob('src/**/*.ts').match('src/a/b.ts');          // true
new Glob('!*.js').match('a.ts');                      // true
[...new Glob('**/*.js').scanSync({ cwd: 'lib' })];     // relative paths, '/' separated
for await (const p of new Glob('*.md').scan('.')) {}
```

| Pattern | Matches |
| --- | --- |
| `*` | any characters except `/` |
| `?` | one character except `/` |
| `[ab]`, `[a-c]`, `[!a]`, `[^a]` | one character of the set, or not in it |
| `{a,b}` | either alternative, nests |
| `**` | any number of whole path segments, also none: `a/**/b` matches `a/b`; `src/**` does not match `src` |
| `!p` | at the start: everything `p` does not match |
| `\x` | `x` literally |

`scanSync(options | cwd)` and `scan(options | cwd)` (an async iterator) take:

| Option | Default | Meaning |
| --- | --- | --- |
| `cwd` | `'.'` | where to start |
| `dot` | `false` | also names starting with a dot (on by itself when the pattern has a segment starting with one) |
| `absolute` | `false` | absolute paths instead of relative ones |
| `onlyFiles` | `true` | leave directories out |
| `followSymlinks` | `false` | walk into symlinked directories |

`globToRegExp(pattern)` returns `{ re, negated }`, the anchored `RegExp` and whether the pattern started with `!`.
