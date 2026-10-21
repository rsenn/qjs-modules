// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/intl.js: the expected strings are what node 23 (ICU 74) gives for en-US,
 * except the space before AM in formatToParts: U+202F in node, a plain space
 * in bun, deno and here */
import Intl from '../../lib/intl.js';
import { eq, tests } from '../../lib/tinytest.js';

const d = Date.UTC(2024, 2, 5, 14, 7, 9, 45);
const N = (o, x) => new Intl.NumberFormat('en-US', o).format(x);
const D = o => new Intl.DateTimeFormat('en-US', { timeZone: 'UTC', ...o }).format(d);
const error = f => {
  try {
    f();
  } catch(e) {
    return e.constructor.name;
  }
};

tests({
  'NumberFormat decimal'() {
    eq('1,234.5|-1,234.568|1.005|0|-0|NaN|∞|-∞', [1234.5, -1234.5678, 1.005, 0.000123, -0, NaN, Infinity, -Infinity].map(x => N({}, x)).join('|'));
    eq('1,000,000,000,000,000,000,000', N({}, 1e21));
    eq('12,345,678,901,234,567,890', N({}, 12345678901234567890n));
    eq('1,234.5', N({}, '1234.5'));
  },

  'NumberFormat digits and grouping'() {
    eq('1.00|3|-3|005', [N({ minimumFractionDigits: 2 }, 1), N({ maximumFractionDigits: 0 }, 2.5), N({ maximumFractionDigits: 0 }, -2.5), N({ minimumIntegerDigits: 3 }, 5)].join('|'));
    eq('123,000|1.5000', [N({ maximumSignificantDigits: 3 }, 123456), N({ minimumSignificantDigits: 5 }, 1.5)].join('|'));
    eq('1234567|1000|10,000', [N({ useGrouping: false }, 1234567), N({ useGrouping: 'min2' }, 1000), N({ useGrouping: 'min2' }, 10000)].join('|'));
  },

  'NumberFormat percent and currency'() {
    eq('26%|1.2%', [N({ style: 'percent' }, 0.256), N({ style: 'percent', maximumFractionDigits: 1 }, 0.01234)].join('|'));
    eq('-$1,234.50|€1.00|¥1,235|CA$1.00|$1.00', [N({ style: 'currency', currency: 'USD' }, -1234.5), N({ style: 'currency', currency: 'EUR' }, 1), N({ style: 'currency', currency: 'JPY' }, 1234.5), N({ style: 'currency', currency: 'CAD' }, 1), N({ style: 'currency', currency: 'CAD', currencyDisplay: 'narrowSymbol' }, 1)].join('|'));
    eq('USD 1.00|1.00 US dollars|($1.00)', [N({ style: 'currency', currency: 'USD', currencyDisplay: 'code' }, 1), N({ style: 'currency', currency: 'USD', currencyDisplay: 'name' }, 1), N({ style: 'currency', currency: 'USD', currencySign: 'accounting' }, -1)].join('|'));
  },

  'NumberFormat notations and signs'() {
    eq('999|1.2K|12K|123K|1.5M|1M|2.5B|1000T|-1.2K|0.5|1K', [999, 1234, 12345, 123456, 1500000, 999999, 2.5e9, 1e15, -1234, 0.5, 1000].map(x => N({ notation: 'compact' }, x)).join('|'));
    eq('1.2 million', N({ notation: 'compact', compactDisplay: 'long' }, 1234567));
    eq('1.235E5|123.456E3', [N({ notation: 'scientific' }, 123456), N({ notation: 'engineering' }, 123456)].join('|'));
    eq('+1 -1 +0 -0|1 1 0 0|+1 -1 0 0|1 -1 0 0', ['always', 'never', 'exceptZero', 'negative'].map(s => [1, -1, 0, -0].map(x => N({ signDisplay: s }, x)).join(' ')).join('|'));
  },

  'NumberFormat parts, options and errors'() {
    eq('minusSign:-,currency:$,integer:1,group:,,integer:234,decimal:.,fraction:50', new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD' }).formatToParts(-1234.5).map(p => `${p.type}:${p.value}`).join(','));
    eq('integer:1,decimal:.,fraction:5,compact:K', new Intl.NumberFormat('en-US', { notation: 'compact' }).formatToParts(1500).map(p => `${p.type}:${p.value}`).join(','));
    eq('{"locale":"en-US","numberingSystem":"latn","style":"decimal","minimumIntegerDigits":1,"minimumFractionDigits":0,"maximumFractionDigits":3,"useGrouping":"auto","notation":"standard","signDisplay":"auto","roundingIncrement":1,"roundingMode":"halfExpand","roundingPriority":"auto","trailingZeroDisplay":"auto"}', JSON.stringify(new Intl.NumberFormat('en-US').resolvedOptions()));
    eq('1,2', ['1', '2'].map(new Intl.NumberFormat('en-US').format).join());
    eq('TypeError RangeError RangeError RangeError', [() => N({ style: 'currency' }), () => N({ style: 'currency', currency: 'US' }), () => N({ minimumFractionDigits: 3, maximumFractionDigits: 1 }), () => N({ style: 'x' })].map(error).join(' '));
  },

  'DateTimeFormat styles'() {
    eq('3/5/2024', D({}));
    eq('Tuesday, March 5, 2024|March 5, 2024|Mar 5, 2024|3/5/24', ['full', 'long', 'medium', 'short'].map(dateStyle => D({ dateStyle })).join('|'));
    eq('2:07:09 PM Coordinated Universal Time|2:07:09 PM UTC|2:07:09 PM|2:07 PM', ['full', 'long', 'medium', 'short'].map(timeStyle => D({ timeStyle })).join('|'));
    eq('March 5, 2024 at 2:07:09 PM UTC|Mar 5, 2024, 2:07:09 PM|3/5/24, 2:07 PM', ['long', 'medium', 'short'].map(s => D({ dateStyle: s, timeStyle: s })).join('|'));
  },

  'DateTimeFormat fields'() {
    eq('March 5, 2024|Mar 5, 2024|M 5, 2024|Tue, 3/5/2024|March 2024|March 5|3/5|3/2024|03/05/24', [{ year: 'numeric', month: 'long', day: 'numeric' }, { year: 'numeric', month: 'short', day: 'numeric' }, { year: 'numeric', month: 'narrow', day: 'numeric' }, { weekday: 'short', year: 'numeric', month: 'numeric', day: 'numeric' }, { year: 'numeric', month: 'long' }, { month: 'long', day: 'numeric' }, { month: 'numeric', day: 'numeric' }, { month: 'numeric', year: 'numeric' }, { year: '2-digit', month: '2-digit', day: '2-digit' }].map(D).join('|'));
    eq('2:07:09 PM|2 PM|14|14:07:09|07:09.045', [{ hour: 'numeric', minute: 'numeric', second: 'numeric' }, { hour: 'numeric' }, { hour: 'numeric', hour12: false }, { hour: 'numeric', minute: '2-digit', second: '2-digit', hour12: false }, { minute: '2-digit', second: '2-digit', fractionalSecondDigits: 3 }].map(D).join('|'));
    eq('March 5 at 2 PM|Mar 5, 2024, 2:07 PM|3/5/2024, 2:07 PM UTC', [{ month: 'long', day: 'numeric', hour: 'numeric' }, { month: 'short', day: 'numeric', year: 'numeric', hour: 'numeric', minute: '2-digit' }, { year: 'numeric', month: 'numeric', day: 'numeric', hour: 'numeric', minute: '2-digit', timeZoneName: 'short' }].map(D).join('|'));
  },

  'DateTimeFormat time zones'() {
    const z = (timeZone, timeZoneName) => new Intl.DateTimeFormat('en-US', { timeZone, hour: 'numeric', timeZoneName }).format(0);

    eq('12 AM GMT|12 AM UTC|12 AM Coordinated Universal Time', [z('UTC', 'longOffset'), z('UTC', 'short'), z('UTC', 'long')].join('|'));
    eq('2 AM GMT+2|2 AM GMT+02:00|6 PM GMT-5:30', [z('+02:00', 'shortOffset'), z('+02:00', 'longOffset'), z('-05:30', 'shortOffset')].join('|'));
    eq('UTC +02:00', [new Intl.DateTimeFormat('en-US', { timeZone: 'etc/utc' }).resolvedOptions().timeZone, new Intl.DateTimeFormat('en-US', { timeZone: '+02:00' }).resolvedOptions().timeZone].join(' '));
    eq('00:00|12:00 AM', [{ hourCycle: 'h23' }, {}].map(o => new Intl.DateTimeFormat('en-US', { timeZone: 'UTC', hour: 'numeric', minute: 'numeric', ...o }).format(Date.UTC(2024, 0, 1))).join('|'));
  },

  'DateTimeFormat parts, options and errors'() {
    eq('month:1,literal:/,day:1,literal:/,year:24,literal:, ,hour:9,literal::,minute:05,literal: ,dayPeriod:AM', new Intl.DateTimeFormat('en-US', { timeZone: 'UTC', dateStyle: 'short', timeStyle: 'short' }).formatToParts(Date.UTC(2024, 0, 1, 9, 5)).map(p => `${p.type}:${p.value}`).join(','));
    eq('{"locale":"en-US","calendar":"gregory","numberingSystem":"latn","timeZone":"UTC","hourCycle":"h12","hour12":true,"timeStyle":"short"}', JSON.stringify(new Intl.DateTimeFormat('en-US', { timeZone: 'UTC', timeStyle: 'short' }).resolvedOptions()));
    eq('RangeError RangeError TypeError', [() => D({}) && new Intl.DateTimeFormat('en-US').format(NaN), () => new Intl.DateTimeFormat('en-US', { timeZone: 'Foo/Bar' }), () => new Intl.DateTimeFormat('en-US', { dateStyle: 'short', month: 'long' })].map(error).join(' '));
    eq('1/1/6', new Intl.DateTimeFormat('en-US', { timeZone: 'UTC' }).format(Date.UTC(-5, 0, 1)));
  },

  'PluralRules'() {
    const pr = new Intl.PluralRules('en-US'),
      po = new Intl.PluralRules('en-US', { type: 'ordinal' });

    eq('other one other other one one one one', [0, 1, 2, 1.5, -1, '1', 1.0001, 0.9999].map(n => pr.select(n)).join(' '));
    eq('other', new Intl.PluralRules('en-US', { minimumFractionDigits: 1 }).select(1));
    eq('other one two few other other other other one two few one other other', [0, 1, 2, 3, 4, 11, 12, 13, 21, 22, 23, 101, 111, 112].map(n => po.select(n)).join(' '));
  },

  'ListFormat'() {
    const L = (type, style) => [[], ['a'], ['a', 'b'], ['a', 'b', 'c']].map(l => new Intl.ListFormat('en-US', { type, style }).format(l)).join('|');

    eq('|a|a and b|a, b, and c', L('conjunction', 'long'));
    eq('|a|a & b|a, b, & c', L('conjunction', 'short'));
    eq('|a|a or b|a, b, or c', L('disjunction', 'long'));
    eq('|a|a b|a b c', L('unit', 'narrow'));
    eq('TypeError', error(() => new Intl.ListFormat('en-US').format([1])));
  },

  'RelativeTimeFormat'() {
    const R = o => new Intl.RelativeTimeFormat('en-US', o);

    eq('1 day ago|in 1 day|in 0 days|in 1.5 years|0 minutes ago|in 1,000 weeks', [[-1, 'day'], [1, 'day'], [0, 'day'], [1.5, 'year'], [-0, 'minute'], [1000, 'week']].map(([v, u]) => R().format(v, u)).join('|'));
    eq('yesterday|today|tomorrow|last year|now|1 second ago|this hour|next quarter', [[-1, 'day'], [0, 'day'], [1, 'day'], [-1, 'year'], [0, 'second'], [-1, 'second'], [0, 'hour'], [1, 'quarter']].map(([v, u]) => R({ numeric: 'auto' }).format(v, u)).join('|'));
    eq('in 3 mo.|2 yr. ago|2 qtrs. ago|1 qtr. ago|2 days ago|last wk.', [R({ style: 'short' }).format(3, 'month'), R({ style: 'short' }).format(-2, 'years'), R({ style: 'short' }).format(-2, 'quarter'), R({ style: 'short' }).format(-1, 'quarter'), R({ style: 'short' }).format(-2, 'day'), R({ style: 'narrow', numeric: 'auto' }).format(-1, 'week')].join('|'));
    eq('1d ago|in 3mo|2y ago|in 5m', [[-1, 'day'], [3, 'month'], [-2, 'year'], [5, 'minute']].map(([v, u]) => R({ style: 'narrow' }).format(v, u)).join('|'));
    eq('literal:in ,integer:1,group:,,integer:234,literal: days', R().formatToParts(1234, 'day').map(p => `${p.type}:${p.value}`).join(','));
    eq('RangeError', error(() => R().format(1, 'decade')));
  },

  'Collator'() {
    const c = new Intl.Collator('en-US');

    eq('_a 10 9 a A á ä a b ab b B e é z Z', ['b', 'a', 'B', 'A', 'á', 'ä', 'z', 'Z', '10', '9', 'a b', 'ab', '_a', 'é', 'e'].sort(c.compare).join(' '));
    eq('0 0 -1|0 -1|-1 0', [['base', [['a', 'A'], ['a', 'á'], ['a', 'b']]], ['accent', [['a', 'A'], ['a', 'á']]], ['case', [['a', 'A'], ['a', 'á']]]].map(([sensitivity, pairs]) => pairs.map(([x, y]) => new Intl.Collator('en-US', { sensitivity }).compare(x, y)).join(' ')).join('|'));
    eq('1 9 10 a9 a10', ['10', '9', '1', 'a10', 'a9'].sort(new Intl.Collator('en-US', { numeric: true }).compare).join(' '));
    eq('A a B b', ['a', 'A', 'b', 'B'].sort(new Intl.Collator('en-US', { caseFirst: 'upper' }).compare).join(' '));
    eq(0, new Intl.Collator('en-US', { ignorePunctuation: true }).compare('a-b', 'ab'));
  },

  'locales'() {
    eq('en-US zh-Hant-TW de', Intl.getCanonicalLocales(['EN-us', 'zh-hant-tw', 'en-US', 'de']).join(' '));
    eq('RangeError', error(() => Intl.getCanonicalLocales('en_US')));
    eq('en-GB en', Intl.NumberFormat.supportedLocalesOf(['en-GB', 'de', 'en']).join(' '));
    eq('zh Hant TW zh-Hant-TW', (l => [l.language, l.script, l.region, l.baseName].join(' '))(new Intl.Locale('zh-Hant-TW')));
    eq('en-GB', String(new Intl.Locale('en', { region: 'GB' })));
    eq('en-US gregory h23 en-US-u-ca-gregory-hc-h23', (l => [l.baseName, l.calendar, l.hourCycle, String(l)].join(' '))(new Intl.Locale('en-US-u-ca-gregory-hc-h23')));
    eq('[object Intl] [object Intl.NumberFormat]', [Object.prototype.toString.call(Intl), Object.prototype.toString.call(new Intl.NumberFormat())].join(' '));
  },

  'NumberFormat, DateTimeFormat and Collator work without new'() {
    eq('function function function', [Intl.NumberFormat().format, Intl.DateTimeFormat().format, Intl.Collator().compare].map(f => typeof f).join(' '));
    eq('TypeError', error(() => Intl.PluralRules()));
  },
});
