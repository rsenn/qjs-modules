# dom

Source: `lib/dom.js` (pure JS)

A lightweight DOM implementation: node/element classes, a parser, collections,
serialization, and mutation observers.

## Core node classes

| Class | Description |
| --- | --- |
| `Node` | Base node (`extends Interface`): parent/child links, `nodeType`, traversal. |
| `Element` | Element node (`extends Node`): attributes, children, query methods. |
| `Document` | Document root (`extends Element`). |
| `Attr` | Attribute node (`extends Node`). |
| `CharacterData` | Base for text-bearing nodes (`extends Node`). |
| `Text` | Text node (`extends CharacterData`). |
| `Comment` | Comment node (`extends CharacterData`). |
| `Interface` | Shared base providing property plumbing. |
| `DOMException` | DOM error type (`extends Error`). |

## Collections

| Class / function | Description |
| --- | --- |
| `NodeList` | Live/static list of nodes. |
| `HTMLCollection` | Element collection. |
| `NamedNodeMap` | Attribute map. |
| `NamedMap(node, get, keys)` | Generic name-keyed map. |
| `Collection(obj, get, len, proto)` | Builds an array-like collection facade. |
| `TokenList` | `classList`-style token set. |
| `CSSStyleDeclaration` | `style`-attribute model. |
| `ListAdapter(list, key)` | Adapts a list to a named map. |
| `MapItems(list, t)` / `FindItem(list, pred)` / `FindItemIndex(list, pred)` | List helpers. |

## Parsing, building & serialization

| Class / function | Description |
| --- | --- |
| `Parser` | Parses markup into a node tree. |
| `Factory` | Creates nodes of the right class for an owner document. |
| `Classes()` / `Prototypes(constructors)` | Build the class/prototype tables. |
| `GetType(raw)` / `GetNode(raw, owner, factory)` | Map raw parse nodes to DOM nodes. |
| `Serializer` | Serializes a node tree back to markup. |

## Mutation observation

| Class | Description |
| --- | --- |
| `MutationObserver` | Observes subtree mutations and delivers records. |
| `MutationRecord` | A single recorded mutation. |

## Constants

`Entities`, `nodeTypes`, `NODE_TYPES` — entity tables and node-type enumerations.
