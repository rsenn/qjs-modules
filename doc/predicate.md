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
