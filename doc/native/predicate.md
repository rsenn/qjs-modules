# predicate

Source: `quickjs-predicate.c` — module exports **`Predicate`**, `PredicateOperators`, `PredicateOperatorSet`, and function lists.

Composable, serializable predicate/matcher objects. A `Predicate` is callable
and can be combined with others to build matching expressions and small
arithmetic terms.

## Predicate instances

```js
new Predicate(...)   // length 1
```

| Member | Args | Kind | Description |
| --- | --- | --- | --- |
| `eval(value)` | 1 | method | Evaluates the predicate against `value` (alias `call`). |
| `toString()` | 0 | method | Source-like string form. |
| `toSource()` | 0 | method | Reconstructable source form. |
| `keys()` | 0 | method | Sub-predicate keys. |
| `values()` | 0 | method | Sub-predicate values. |
| `id` | — | getter | Predicate type id. |
| `length` | — | getter | Argument count. |

## Predicate factory functions

Constructors that build predicates (module-level function list):

- **Type / value:** `type`, `charset`, `string`, `regexp`, `instanceOf`,
  `prototypeIs`, `equal`, `property`, `has`, `member`, `index`, `function`.
- **Logical:** `not`, `notnot`, `or`, `and`, `xor`, `some`, `every`.
- **Arithmetic / bitwise:** `add`, `sub`, `mul`, `div`, `mod`, `pow`, `sqrt`,
  `atan2`, `bor`, `band`, `bnot`, `shift`, `slice`.

Each takes 1 or 2 arguments (operands or nested predicates) and returns a new
`Predicate`.

## PredicateOperators

An operator table exposing arithmetic/bitwise operators as 2-argument functions:
`+`, `-`, `*`, `/`, `%`, `|`, `&`, `**`. Exported as `PredicateOperators` /
`PredicateOperatorSet` for operator-overloading use.

## How composition works

A `Predicate` is not a value — it's a lazy expression node. Building one (via
`Predicate.add(a, b)`, `Predicate.and(...)`, the `new Predicate(id, ...)`
constructor sugar, or a JS operator like `a + b` on two `Predicate`s) just
*stores* its operands; nothing is computed until the predicate is later
called/`eval()`'d against some value(s). See `predicate_eval()`
(`src/predicate.c`) for the full per-kind evaluation switch — this section
covers the arithmetic/bitwise family (`add`/`sub`/`mul`/`div`/`mod`/`pow`/
`atan2`/`bor`/`band`), since that's the family wired up to real JS operators.

### What an operand can be

Each operand of `add`/`sub`/`mul`/... (`pr->binary.left`, `pr->binary.right`)
is one of three things, resolved by `predicate_value()` (`src/predicate.c`)
at evaluation time, given the same call arguments the outer predicate itself
was invoked with:

- **Another `Predicate`** — evaluated recursively (`predicate_eval()`), so
  expression trees can nest arbitrarily deep:
  `Predicate.mul(Predicate.add(a, b), c)` builds `(a + b) * c`.
- **A plain JS function** — called with the (remaining) call arguments and
  its return value used, e.g. `Predicate.add(x => x.length, 1)` is
  `value => value.length + 1`.
- **Anything else (a number, string, ...)** — used as a literal constant.

### The "missing operand" convention

`predicate_add()`/`predicate_sub()`/etc. (`include/predicate.h`) normalize an
explicit `null`/`undefined` operand to `JS_UNDEFINED` at construction time.
`predicate_eval()` then treats *that specific* sentinel specially: for
operand index `i` (0 = left, 1 = right), a still-`undefined` operand is
replaced with `args[i]` — the `i`-th argument the *composed predicate itself*
was called with — before going through `predicate_value()` above. This is
what turns an arithmetic predicate into a genuine unary function of the
tested value rather than a constant-folded expression:

```js
Predicate.add(undefined, 5)   // value => value + 5
Predicate.mul(undefined, 2)   // value => value * 2  (left omitted -> arg[0])
Predicate.mul(2)              // value => 2 * value  (right omitted -> arg[1])
```

Passing both operands as `undefined` (or simply omitting both) makes the
predicate a binary function of its own two call arguments
(`(a, b) => a + b`). Leaving a *trailing* operand out of the call works the
same as passing `undefined` explicitly — both go through `predicate_nextarg()`
(`quickjs-predicate.c`), which normalizes `js_arguments_shift()`'s (
`include/utils.h`) "no more arguments" sentinel the same way it normalizes an
explicit `null`/`undefined`.

### JS operators: `+ - * / % | & **`

`js_predicate_operator()` (`quickjs-predicate.c`) is the C function behind
every one of these operators; its `magic` argument (`OPERATOR_PLUS`, ...)
selects which `predicate_##op()` builder from `include/predicate.h` to call
on its two arguments — it does not evaluate anything, only composes. The
result is always a **new** `Predicate` wrapping whatever was passed in
(`Predicate`, function, or literal — see above), never a computed number.

This single function is installed three times, via `Symbol.operatorSet`
(`js_predicate_init()`, see `doc/operator-overloading.md` for the mechanism):

| Table (JS-facing key) | Internal `JSOperatorSetData` field | Used for |
| --- | --- | --- |
| self-ops | `self_ops[]` | `Predicate + Predicate` |
| `{ left: Number, ... }` | `right` (cross-type key inverted by `js_operators_create_internal`, see below) | `Number + Predicate` |
| `{ right: Number, ... }` | `left` | `Predicate + Number` |

The `left`/`right` *property names* passed to `Operators.create()` describe
where the *other* operand (`Number`) sits in the written expression, not
where `Predicate` sits — `quickjs.c`'s `js_operators_create_internal` reads a
`left` property into `opset->right` and a `right` property into
`opset->left` (the two are swapped on purpose, so that at dispatch time
`opset->right` answers "what do I do when I'm on the right and the other
operand is `Number`?"). Regardless of which table was consulted to find it,
`js_predicate_operator()` is always called as `(op1, op2)` in the same
left-to-right order the expression was written in, so `left`/`right` inside
the resulting `Predicate` always match the source: `5 * p` composes
`Predicate.mul(5, p)`, `p * 5` composes `Predicate.mul(p, 5)`.

Because `Number.prototype` only carries the empty, `is_primitive` marker
operator set (installed by `JS_AddIntrinsicOperators()`), mixing a
`Predicate` with any *other* class this way isn't possible without that
class also having its own operator set — plain numbers work because that
marker set's `operator_counter` is exactly what the `left`/`right` tables
above are keyed against.
