# intl

Source: `lib/intl.js` (pure JS, no imports) — default export: the `Intl` object. `lib/globals.js` and `lib/bun.js`
install it as the global `Intl`, which QuickJS lacks.

A minimal [ECMA-402](https://tc39.es/ecma402/) with English data only. Every locale resolves to `en` or `en-US`
and formats as `en-US` does in node (ICU 74); `tests/unittests/test-intl.js` checks node's strings on qjsm, node,
bun and deno.

```js
import Intl from 'intl';   // or: import 'globals'

new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD' }).format(-1234.5);  // '-$1,234.50'
new Intl.NumberFormat('en-US', { notation: 'compact' }).format(1500000);                // '1.5M'
new Intl.DateTimeFormat('en-US', { dateStyle: 'full', timeZone: 'UTC' }).format(0);      // 'Thursday, January 1, 1970'
new Intl.RelativeTimeFormat('en', { numeric: 'auto' }).format(-1, 'day');               // 'yesterday'
new Intl.ListFormat('en').format(['a', 'b', 'c']);                                      // 'a, b, and c'
new Intl.PluralRules('en', { type: 'ordinal' }).select(22);                             // 'two'
['b', 'a', 'B', 'á'].sort(new Intl.Collator('en').compare);                             // ['a', 'á', 'b', 'B']
```

## What is there

| Member | Covers |
| --- | --- |
| `NumberFormat` | styles `decimal`, `percent`, `currency` (symbol, narrowSymbol, code, name for USD EUR GBP JPY, accounting); fraction, significant and integer digits; `useGrouping`; notations `standard`, `scientific`, `engineering`, `compact` (short, long); `signDisplay`; `formatToParts`; strings and BigInts format exactly |
| `DateTimeFormat` | `dateStyle`, `timeStyle`, the fields `weekday` `year` `month` `day` `hour` `minute` `second` `fractionalSecondDigits` `timeZoneName`; `hour12`, `hourCycle`; `formatToParts` |
| `PluralRules` | `cardinal` (one, other) and `ordinal` (one, two, few, other), with the digit options of NumberFormat |
| `Collator` | punctuation < digits < letters, then accents, then case; `sensitivity`, `numeric`, `caseFirst`, `ignorePunctuation` |
| `ListFormat` | `conjunction`, `disjunction`, `unit` in `long`, `short`, `narrow` |
| `RelativeTimeFormat` | second … year in `long`, `short`, `narrow`, `numeric: 'auto'` (yesterday, last week, now) |
| `Locale` | `language`, `script`, `region`, `baseName`, the `-u-` keywords (`calendar`, `hourCycle`, `numeric`, ...), `toString()` |
| `getCanonicalLocales`, `supportedValuesOf` | `'zh-hant-tw'` → `'zh-Hant-TW'`, RangeError for a malformed tag |

`NumberFormat`, `DateTimeFormat` and `Collator` can be called without `new`, as in the spec. `format` and `compare`
are bound getters, so `list.map(nf.format)` works.

## Time zones

| `timeZone` | Formats in |
| --- | --- |
| omitted | the local zone, named by `process.env.TZ`, else `'UTC'` or the current offset (`'+02:00'`) |
| `'UTC'`, `'Etc/UTC'`, `'GMT'` | UTC (`'Coordinated Universal Time'`, `'UTC'`) |
| `'+05:30'`, `'-08'` | that fixed offset (`'GMT+5:30'`, long `'GMT+05:30'`) |
| any other IANA name | RangeError `Invalid time zone specified` (no tz data) |

## Differences from node

| Node | Here |
| --- | --- |
| Every CLDR locale | English only: `supportedLocalesOf(['de', 'en-GB'])` is `['en-GB']`, `'en-GB'` resolves to `'en'` and formats as en-US |
| `Segmenter`, `DisplayNames`, `DurationFormat` | absent |
| NumberFormat `style: 'unit'`, `formatRange` | RangeError for `unit`, no `formatRange` |
| DateTimeFormat `era`, `dayPeriod` | ignored |
| Named IANA zones (`'Europe/Berlin'`) | RangeError, see above |
| `formatToParts` puts U+202F before AM/PM | a plain space, as bun and deno do |
| `Number.prototype.toLocaleString`, `Date.prototype.toLocaleString`, `String.prototype.localeCompare` use Intl | left as the engine has them |
