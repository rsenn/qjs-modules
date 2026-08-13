# tree_walker

Source: `lib/tree_walker.js` (pure JS)

> Distinct from the native [`tree-walker`](tree-walker.md) binding. This is a
> JS module that exposes a `TreeWalker` — using the native binding when running
> under QuickJS, and falling back to a pure-JS implementation otherwise.

## Exports

| Export | Kind | Description |
| --- | --- | --- |
| `TreeWalker` | const/class | DOM-style traversal object over a nested value (see [tree-walker](tree-walker.md) for the method surface). |
