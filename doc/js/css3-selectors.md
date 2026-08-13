# css3-selectors

Source: `lib/css3-selectors.js` (pure JS)

Builds CSS3 selector matchers out of composable [`predicate`](predicate.md)
parts.

## Exports

| Export | Args | Kind | Description |
| --- | --- | --- | --- |
| `parseSelectors(s)` | 1 | generator | Parses a selector string, yielding selector matchers. |
| `TypeSelector(tagName)` | 1 | function | Matches elements by tag name. |
| `ClassSelector(className)` | 1 | function | Matches elements carrying a class. |
| `IdSelector(id)` | 1 | function | Matches an element by id. |
| `AttributeSelector(...args)` | * | function | Matches by attribute (name/operator/value). |
| `PseudoClassSelector(pseudoClass)` | 1 | function | Matches a pseudo-class. |
| `LogicPredicate(pred=Predicate.or, ...args)` | 1+ | function | Combines sub-selectors with a logical operator. |
