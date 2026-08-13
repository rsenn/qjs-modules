# util

Source: `lib/util.js` (pure JS)

The project's central utility library — a large collection of helpers for type
checking, object/property manipulation, functional composition, collections,
strings, numbers, time, async, and more. Re-exports everything from
[`misc`](misc.md) and `inspect` from [`inspect`](inspect.md).

> This module is large; functions are grouped by theme below. Argument lists are
> abbreviated — see the source for defaults.

## Type checks & reflection

`isObject`, `typeIsObject`, `allObjects`, `isClass`, `isNative`,
`isPropertyKey`, `isArrowFunction`, `isAsync`, `isNumeric`, `isIndex`,
`inRange`, `isInstanceOf(c, o)`, `isPrototypeOf(p, o)`, `hasPrototype`,
`hasBuiltIn`, `hasOwnProperty`, `hasOwn`, `hasProperty`, `hasFn`,
`propertyIsEnumerable`, `getConstructorOf`, `getConstructorChain`. Type tables:
`types`, `TypeIds`, `errors`. Constructor handles: `AsyncFunction`,
`GeneratorFunction`, `AsyncGeneratorFunction`, `TypedArray`, `Generator`,
`SetIterator`, `MapIterator`, and their prototypes.

## Object / property utilities

Object reflection wrappers: `getPrototypeOf`, `setPrototypeOf`,
`getOwnPropertyNames`, `getOwnPropertySymbols`, `getOwnPropertyDescriptor(s)`,
`defineProperty`, `defineProperties`, `seal`, `freeze`, `isSealed`, `isFrozen`,
`isExtensible`, `preventExtensions`, `assign`, `is`, `valueOf`, `toString`,
`toLocaleString`.

Accessor builders: `getset`, `getter`, `setter`, `gettersetter`, `modifier`,
`wrapGetSet`, `weakGetSet`, `getSetArgument`, `hasGetSet`, `addremovehas`,
`remover`, `getOrCreate`, `lookupObject`, `mapObject`, `propertyLookup`,
`propertyLookupHandlers`, `bindProperties`.

Definition helpers: `define`, `weakDefine`, `declare`, `extend`, `merge`,
`nonenumerable`, `enumerable`, `defineGetter`, `defineGetterSetter`,
`defineGettersSetters`, `lazyProperty`, `lazyProperties`, `observeProperties`,
`decorate`, `decorateProperty`, `weakAssoc`.

Property/method enumeration: `keys`, `values`, `entries`, `fromEntries`, `pick`,
`omit`, `properties`, `getPropertyNames`, `getProperties`, `getMethodNames`,
`getMethods`, `bindMethods`, `prototypeIterator`, `inherits`.

## Functional / composition

`memoize`, `once`, `curry`, `chain`, `chainRight`, `chainArray`, `mapFunctional`,
`predicate`, `transformer`, `wrapFunction`, `catchFunction`, `finallyFunction`,
`tryFunction`, `tryCatch`, `wrapGenerator`, `wrapGeneratorMethods`,
`copyFunctionNameAndLength`, `setFunctionName`, `functionName` (alias `fnName`),
`className`, `getFunctionArguments`, `instrument`, `catchable`.

## Collections & arrays

`filter`, `filterKeys`, `map`, `range`, `split`, `chunkArray`, `partitionArray`,
`partition`, `unique`, `uniquePred`, `pushUnique`, `push`, `eraseIf`, `inserter`,
`generate`, `arraysInCommon`, `arrayFacade`. Set algebra: `intersect`,
`intersection`, `union`, `difference`, `symmetricDifference`. `histogram`,
`mapAdapter`, `mapFunction`, `mapWrapper`, `weakMapper`, `hash`.

## Strings & text

`stripAnsi`, `padAnsi`, `padStartAnsi`, `padEndAnsi`, `pad`, `padFn`, `trim`,
`abbreviate`, `shorten`, `ucfirst`, `lcfirst`, `camelize`, `decamelize`,
`decodeHTMLEntities`, `bytesToUTF8`, `codePointsToString`, `bufferToString`,
`matchAll`, `indexOf`, `searchAll`. ANSI styling: `ansiStyles`.

## Numbers & math

`clamp`, `mod`, `roundTo`, `roundDigits`, `toBigInt`, `numericIndex`,
`randInt`, `randFloat`, `randStr`. Curried operators: `add`, `sub`, `mul`,
`div`, `pow`, `xor`, `or`, `and`. Length/slice helpers: `length`, `slice`.

## Time & dates

`isoDate`, `toUnixTime`, `unixTime`, `fromUnixTime`.

## Async & scheduling

`setImmediate`, `clearImmediate`, `queueMicrotask`, `waitFor`,
`waitCancellable`, plus re-exported `setInterval`/`clearInterval` (from
[`timers`](timers.md)).

## Assertions & errors

`assert`, `assertEqual`, `getSystemErrorName`, `getSystemErrorMap`.

## CLI

`getOpt(options, args)` — parses command-line options; `showHelp(opts, exitCode)`
— prints usage.

## Environment & misc

`isBrowser`, `startInteractive`, `Membrane(instance, obj, proto, …)`,
`repeat`, `repeater`, `inspectSymbol`.

## Re-exports

`inspect` (from `inspect`), everything from [`misc`](misc.md), and
`setInterval`/`clearInterval` (from `timers`).
