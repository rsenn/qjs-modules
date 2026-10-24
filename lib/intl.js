/* intl.js: a minimal ECMAScript Intl (ECMA-402) with English data only.
 * depends on: nothing, runs on any engine.
 * rule: every locale resolves to en or en-US and formats as en-US does in node.
 *
 * ```js
 * import Intl from 'intl';
 * new Intl.NumberFormat('en-US', { style: 'currency', currency: 'USD' }).format(-1234.5)  // '-$1,234.50'
 * new Intl.DateTimeFormat('en-US', { dateStyle: 'long', timeZone: 'UTC' }).format(0)      // 'January 1, 1970'
 * ```
 *
 * not here: Segmenter, DisplayNames, DurationFormat, NumberFormat style unit,
 * the era and dayPeriod options, formatRange, named time zones other than UTC
 * and the local one.
 */

const define = (obj, props) => {
  for(const key of Reflect.ownKeys(props)) Object.defineProperty(obj, key, { value: props[key], writable: true, configurable: true });
  return obj;
};

/* the options argument: undefined is {}, null throws as ToObject does */
const opts = o => {
  if(o === null) throw new TypeError('Cannot convert null to object');
  return o === undefined ? {} : Object(o);
};

/* o[key] as one of list, or fallback when undefined */
function pick(o, key, list, fallback, name) {
  if(o[key] === undefined) return fallback;

  const v = String(o[key]);
  if(!list.includes(v)) throw new RangeError(`Value ${v} out of range for Intl.${name} options property ${key}`);
  return v;
}

/* o[key] as an integer in min..max, or fallback when undefined */
function int(o, key, min, max, fallback) {
  if(o[key] === undefined) return fallback;

  const v = Number(o[key]);
  if(!(v >= min && v <= max)) throw new RangeError(`${key} value is out of range.`);
  return Math.floor(v);
}

/* the parts of a format, joined */
const join = parts => parts.map(p => p.value).join('');

/* ---- locales ---- */

const TAG = /^([a-z]{2,3})(-[a-z]{4})?(-(?:[a-z]{2}|\d{3}))?((?:-(?:[a-z\d]{5,8}|\d[a-z\d]{3}))*)((?:-[a-wyz\d](?:-[a-z\d]{2,8})+)*)$/i;

/* 'zh-hant-tw' -> 'zh-Hant-TW', throws RangeError for a malformed tag */
function canonical(tag) {
  if(tag instanceof Locale) return tag.toString();
  if(typeof tag != 'string' && typeof tag != 'object') throw new TypeError('Language ID should be string or object.');

  const m = TAG.exec(String(tag));
  if(!m) throw new RangeError('Incorrect locale information provided');

  const [, lang, script = '', region = '', variants, ext] = m;
  return lang.toLowerCase() + (script && '-' + script[1].toUpperCase() + script.slice(2).toLowerCase()) + region.toUpperCase() + (variants + ext).toLowerCase();
}

function getCanonicalLocales(locales) {
  if(locales === undefined) return [];

  const list = typeof locales == 'string' || locales instanceof Locale ? [locales] : Array.from(Object(locales));
  return [...new Set(list.map(canonical))];
}

const english = tag => /^en(-|$)/.test(tag);

/* the locale formats use: 'en-US' for en-US or nothing English, else 'en' */
function resolve(locales) {
  const tag = getCanonicalLocales(locales).find(english);
  return !tag || /^en-US(-|$)/.test(tag) ? 'en-US' : 'en';
}

/* supportedLocalesOf and the tag on every Intl class */
function service(C, name) {
  define(C, { supportedLocalesOf: locales => getCanonicalLocales(locales).filter(english) });
  Object.defineProperty(C.prototype, Symbol.toStringTag, { value: 'Intl.' + name, configurable: true });
  return C;
}

/* NumberFormat, DateTimeFormat and Collator also work without new */
const callable = C => new Proxy(C, { apply: (C, self, args) => new C(...args) });

/* ---- numbers ---- */

/* x as decimal digits: 1.005 -> { neg: false, d: '1005', e: 0 }, d.ddd times 10^e,
 * NaN and the infinities keep d as the number */
function decimal(x) {
  if(typeof x == 'number' && !isFinite(x)) return { neg: x < 0, d: x };

  const m = /^\s*([+-]?)(\d*)(?:\.(\d*))?(?:e([+-]?\d+))?\s*$/i.exec(typeof x == 'number' ? x.toExponential() : String(x));
  if(typeof x != 'number' && !(m && (m[2] || m[3]))) return decimal(Number(x));

  const d = m[2] + (m[3] ?? ''),
    lead = d.search(/[1-9]/),
    neg = m[1] == '-' || Object.is(x, -0);

  if(lead < 0) return { neg, d: '0', e: 0 };
  return { neg, d: d.slice(lead).replace(/0+$/, ''), e: m[2].length - 1 + Number(m[4] ?? 0) - lead };
}

/* n rounded half away from zero to its first keep digits */
function round({ neg, d, e }, keep) {
  if(keep >= d.length) return { neg, d, e };

  let head = d.slice(0, Math.max(keep, 0));

  if(keep >= 0 && d[keep] >= '5') {
    const nines = head.search(/9*$/);

    if(nines == 0) return { neg, d: '1', e: e + 1 };
    head = head.slice(0, nines - 1) + (+head[nines - 1] + 1);
  }

  head = head.replace(/0+$/, '');
  return head ? { neg, d: head, e } : { neg, d: '0', e: 0 };
}

/* the integer and fraction digits of n, padded to minI and minF */
function layout({ d, e }, minI, minF) {
  const zero = d == '0',
    int = zero || e < 0 ? '0' : d.slice(0, e + 1).padEnd(e + 1, '0'),
    frac = zero ? '' : e < 0 ? '0'.repeat(-e - 1) + d : d.slice(e + 1);

  return [int.padStart(minI, '0'), frac.padEnd(minF, '0')];
}

/* n rounded by fraction digits { minI, minF, maxF } or significant digits { minI, minS, maxS } */
function digits(n, r) {
  if(r.maxS === undefined) return layout(round(n, n.e + 1 + r.maxF), r.minI, r.minF);

  const m = round(n, r.maxS);
  return layout(m, r.minI, Math.max(0, r.minS - (m.d == '0' ? 1 : m.e + 1)));
}

const CURRENCY = { USD: '$', EUR: '€', GBP: '£', JPY: '¥', CNY: 'CN¥', INR: '₹', KRW: '₩', ILS: '₪', VND: '₫', CAD: 'CA$', AUD: 'A$', NZD: 'NZ$', HKD: 'HK$', MXN: 'MX$', TWD: 'NT$', PHP: '₱' };
const CURRENCY_NAME = { USD: ['US dollar', 'US dollars'], EUR: ['euro', 'euros'], GBP: ['British pound', 'British pounds'], JPY: ['Japanese yen', 'Japanese yen'] };
const CURRENCY_DIGITS = { JPY: 0, KRW: 0, VND: 0, CLP: 0, ISK: 0, PYG: 0, UGX: 0, XAF: 0, XOF: 0, BHD: 3, JOD: 3, KWD: 3, OMR: 3, TND: 3 };
const COMPACT = [, ['K', 'thousand'], ['M', 'million'], ['B', 'billion'], ['T', 'trillion']];

class NumberFormat {
  #r;
  #digits;
  #format;

  constructor(locales, options) {
    const o = opts(options),
      name = 'NumberFormat';
    const style = pick(o, 'style', ['decimal', 'percent', 'currency'], 'decimal', name),
      r = { locale: resolve(locales), numberingSystem: 'latn', style };

    if(o.currency !== undefined && !/^[a-z]{3}$/i.test(o.currency)) throw new RangeError(`Invalid currency code : ${o.currency}`);

    if(style == 'currency') {
      if(o.currency === undefined) throw new TypeError('Currency code is required with currency style.');

      r.currency = o.currency.toUpperCase();
      r.currencyDisplay = pick(o, 'currencyDisplay', ['symbol', 'narrowSymbol', 'code', 'name'], 'symbol', name);
      r.currencySign = pick(o, 'currencySign', ['standard', 'accounting'], 'standard', name);
    }

    const notation = pick(o, 'notation', ['standard', 'scientific', 'engineering', 'compact'], 'standard', name),
      cd = CURRENCY_DIGITS[r.currency] ?? 2;
    const [minF, maxF] = { decimal: [0, 3], percent: [0, 0], currency: [cd, cd] }[style];

    r.minimumIntegerDigits = int(o, 'minimumIntegerDigits', 1, 21, 1);

    if(o.minimumSignificantDigits !== undefined || o.maximumSignificantDigits !== undefined) {
      r.minimumSignificantDigits = int(o, 'minimumSignificantDigits', 1, 21, 1);
      r.maximumSignificantDigits = int(o, 'maximumSignificantDigits', r.minimumSignificantDigits, 21, 21);
    } else if(o.minimumFractionDigits !== undefined || o.maximumFractionDigits !== undefined) {
      const lo = int(o, 'minimumFractionDigits', 0, 100),
        hi = int(o, 'maximumFractionDigits', 0, 100);

      if(lo > hi) throw new RangeError('maximumFractionDigits value is out of range.');
      r.minimumFractionDigits = lo ?? Math.min(minF, hi);
      r.maximumFractionDigits = hi ?? Math.max(maxF, lo);
    } else if(notation == 'compact') {
      Object.assign(r, { minimumFractionDigits: 0, maximumFractionDigits: 0, minimumSignificantDigits: 1, maximumSignificantDigits: 2 });
    } else {
      Object.assign(r, { minimumFractionDigits: minF, maximumFractionDigits: maxF });
    }

    const g = o.useGrouping,
      grouping = g === undefined ? (notation == 'compact' ? 'min2' : 'auto') : g === true ? 'always' : g === false ? false : String(g);

    if(![false, 'always', 'auto', 'min2'].includes(grouping)) throw new RangeError(`Value ${grouping} out of range for Intl.NumberFormat options property useGrouping`);

    r.useGrouping = grouping;
    r.notation = notation;
    if(notation == 'compact') r.compactDisplay = pick(o, 'compactDisplay', ['short', 'long'], 'short', name);
    r.signDisplay = pick(o, 'signDisplay', ['auto', 'never', 'always', 'exceptZero', 'negative'], 'auto', name);

    const precision = notation == 'compact' && r.maximumFractionDigits === 0 && r.maximumSignificantDigits === 2;

    this.#r = Object.assign(r, { roundingIncrement: 1, roundingMode: 'halfExpand', roundingPriority: precision ? 'morePrecision' : 'auto', trailingZeroDisplay: 'auto' });
    this.#digits = {
      minI: r.minimumIntegerDigits,
      minF: r.minimumFractionDigits,
      maxF: r.maximumFractionDigits,
      minS: r.minimumSignificantDigits,
      maxS: precision ? undefined : r.maximumSignificantDigits,
    };
  }

  get format() {
    return (this.#format ??= x => join(this.formatToParts(x)));
  }

  /* -1234.5 as USD: minusSign '-', currency '$', integer '1', group ',', integer '234', decimal '.', fraction '50' */
  formatToParts(x) {
    const r = this.#r,
      parts = [];
    const add = (type, value) => parts.push({ type, value });
    let n = decimal(x),
      int = '',
      frac = '',
      exponent,
      suffix;

    const finite = typeof n.d == 'string',
      nan = Number.isNaN(n.d);

    if(finite && r.style == 'percent' && n.d != '0') n = { ...n, e: n.e + 2 };

    if(!finite) {
    } else if(r.notation == 'compact') {
      // 999999 is 1M, not 1000K: round, then move up a unit when it carried
      for(let k = Math.min(Math.max(Math.floor(n.e / 3), 0), 4); ; k++) {
        const m = { ...n, e: n.e - 3 * k },
          rule = r.roundingPriority == 'auto' ? this.#digits : m.e < 1 ? { minI: 1, minS: 1, maxS: 2 } : { minI: 1, minF: 0, maxF: 0 };

        [int, frac] = digits(m, rule);
        suffix = COMPACT[k];
        if(int.length <= 3 || k == 4) break;
      }
    } else if(r.notation != 'standard') {
      const step = r.notation == 'engineering' ? 3 : 1;

      exponent = n.d == '0' ? 0 : n.e - (((n.e % step) + step) % step);
      [int, frac] = digits({ ...n, e: n.e - exponent }, this.#digits);

      // 9.9996 rounds to 10.000: one step up
      if(int.length > step) [int, frac] = digits({ ...n, e: n.e - (exponent += step) }, this.#digits);
    } else {
      [int, frac] = digits(n, this.#digits);
    }

    const zero = finite && !/[1-9]/.test(int + frac),
      sign = nan ? '' : { auto: n.neg ? '-' : '', always: n.neg ? '-' : '+', exceptZero: zero ? '' : n.neg ? '-' : '+', negative: n.neg && !zero ? '-' : '', never: '' }[r.signDisplay];
    const accounting = r.currencySign == 'accounting' && sign == '-',
      named = r.currencyDisplay == 'name' && CURRENCY_NAME[r.currency];
    const symbol = /symbol/i.test(r.currencyDisplay) && CURRENCY[r.currency];

    if(accounting) add('literal', '(');
    else if(sign) add(sign == '-' ? 'minusSign' : 'plusSign', sign);

    if(symbol) {
      add('currency', r.currencyDisplay == 'narrowSymbol' ? symbol.replace(/^[A-Z]+(?=\$)/, '') : symbol);
    } else if(r.style == 'currency' && !named) {
      add('currency', r.currency);
      add('literal', ' ');
    }

    if(nan) add('nan', 'NaN');
    else if(!finite) add('infinity', '∞');
    else {
      const grouped = r.useGrouping && (r.useGrouping != 'min2' || int.length >= 5);

      (grouped ? int.match(/^\d{1,3}(?=(?:\d{3})*$)|\d{3}/g) : [int]).forEach((group, i) => {
        if(i) add('group', ',');
        add('integer', group);
      });

      if(frac) {
        add('decimal', '.');
        add('fraction', frac);
      }

      if(exponent !== undefined) {
        add('exponentSeparator', 'E');
        if(exponent < 0) add('exponentMinusSign', '-');
        add('exponentInteger', String(Math.abs(exponent)));
      }

      if(suffix && r.compactDisplay == 'long') {
        add('literal', ' ');
        add('compact', suffix[1]);
      } else if(suffix) {
        add('compact', suffix[0]);
      }
    }

    if(r.style == 'percent') add('percentSign', '%');

    if(named) {
      add('literal', ' ');
      add('currency', named[int + frac == '1' ? 0 : 1]);
    }

    if(accounting) add('literal', ')');
    return parts;
  }

  resolvedOptions() {
    return { ...this.#r };
  }
}

/* ---- plural rules ---- */

class PluralRules {
  #r;
  #nf;

  constructor(locales, options) {
    const o = opts(options),
      type = pick(o, 'type', ['cardinal', 'ordinal'], 'cardinal', 'PluralRules');

    this.#nf = new NumberFormat(locales, { ...o, useGrouping: false, notation: 'standard' });

    const { locale, minimumIntegerDigits, minimumFractionDigits, maximumFractionDigits, minimumSignificantDigits, maximumSignificantDigits } = this.#nf.resolvedOptions();

    this.#r = {
      locale,
      type,
      minimumIntegerDigits,
      ...(maximumSignificantDigits === undefined ? { minimumFractionDigits, maximumFractionDigits } : { minimumSignificantDigits, maximumSignificantDigits }),
      pluralCategories: type == 'cardinal' ? ['one', 'other'] : ['one', 'two', 'few', 'other'],
      roundingIncrement: 1,
      roundingMode: 'halfExpand',
      roundingPriority: 'auto',
      trailingZeroDisplay: 'auto',
    };
  }

  /* cardinal: 1 is one, 1.5 and 1.0 (minimumFractionDigits 1) are other
   * ordinal: 1 21 are one, 2 22 two, 3 23 few, 11 12 13 other */
  select(n) {
    const v = Math.abs(Number(n));

    if(this.#r.type == 'cardinal') return this.#nf.format(v).replace(/^0+(?=\d)/, '') == '1' ? 'one' : 'other';

    const i = Math.trunc(v) % 100;
    return i > 10 && i < 14 ? 'other' : ([, 'one', 'two', 'few'][i % 10] ?? 'other');
  }

  resolvedOptions() {
    return { ...this.#r, pluralCategories: [...this.#r.pluralCategories] };
  }
}

/* ---- dates ---- */

const MONTHS = ['January', 'February', 'March', 'April', 'May', 'June', 'July', 'August', 'September', 'October', 'November', 'December'];
const DAYS = ['Sunday', 'Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday', 'Saturday'];

const DATE_STYLE = {
  full: { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' },
  long: { year: 'numeric', month: 'long', day: 'numeric' },
  medium: { year: 'numeric', month: 'short', day: 'numeric' },
  short: { year: '2-digit', month: 'numeric', day: 'numeric' },
};
const TIME_STYLE = {
  full: { hour: 'numeric', minute: '2-digit', second: '2-digit', timeZoneName: 'long' },
  long: { hour: 'numeric', minute: '2-digit', second: '2-digit', timeZoneName: 'short' },
  medium: { hour: 'numeric', minute: '2-digit', second: '2-digit' },
  short: { hour: 'numeric', minute: '2-digit' },
};
const FIELDS = {
  weekday: ['narrow', 'short', 'long'],
  year: ['2-digit', 'numeric'],
  month: ['2-digit', 'numeric', 'narrow', 'short', 'long'],
  day: ['2-digit', 'numeric'],
  hour: ['2-digit', 'numeric'],
  minute: ['2-digit', 'numeric'],
  second: ['2-digit', 'numeric'],
  fractionalSecondDigits: null,
  timeZoneName: ['short', 'long', 'shortOffset', 'longOffset', 'shortGeneric', 'longGeneric'],
};

const pad = (n, w = 2) => String(n).padStart(w, '0');

/* minutes east of UTC as an id: 330 -> '+05:30' */
const offsetId = off => (off < 0 ? '-' : '+') + pad(Math.floor(Math.abs(off) / 60)) + ':' + pad(Math.abs(off) % 60);

/* minutes east of UTC as a zone name: 330 -> 'GMT+5:30', long 'GMT+05:30', 0 -> 'GMT' */
function gmt(off, long) {
  const a = Math.abs(off);

  if(!off) return 'GMT';
  return 'GMT' + (long ? offsetId(off) : (off < 0 ? '-' : '+') + Math.floor(a / 60) + (a % 60 ? ':' + pad(a % 60) : ''));
}

/* the local zone: TZ, else UTC or the offset now ('+02:00') */
function localZone() {
  const off = -new Date().getTimezoneOffset();
  return globalThis.process?.env?.TZ?.replace(/^:/, '') || (off ? offsetId(off) : 'UTC');
}

/* { id, offset(t) }: UTC, a fixed offset like '+05:30', or the local zone */
function timeZone(tz) {
  const local = { id: localZone(), offset: t => -new Date(t).getTimezoneOffset() };
  if(tz === undefined) return local;

  const s = String(tz),
    m = /^([+-])([01]\d|2[0-3])(?::?([0-5]\d))?$/.exec(s);

  if(/^(etc\/)?(utc|gmt)$/i.test(s)) return { id: 'UTC', offset: () => 0 };
  if(m) {
    const off = (m[1] == '-' ? -1 : 1) * (m[2] * 60 + Number(m[3] ?? 0));
    return { id: offsetId(off), offset: () => off };
  }
  if(s == local.id) return local;

  throw new RangeError(`Invalid time zone specified: ${s}`);
}

/* names in a style: long 'March', short 'Mar', narrow 'M' */
const named = (list, style) => list.map(n => (style == 'long' ? n : style == 'short' ? n.slice(0, 3) : n[0]));

class DateTimeFormat {
  #r;
  #c = {};
  #at;
  #zone;
  #format;

  constructor(locales, options) {
    const o = opts(options),
      name = 'DateTimeFormat',
      c = this.#c;
    const dateStyle = pick(o, 'dateStyle', ['full', 'long', 'medium', 'short'], undefined, name),
      timeStyle = pick(o, 'timeStyle', ['full', 'long', 'medium', 'short'], undefined, name);

    for(const [key, list] of Object.entries(FIELDS)) {
      const v = list ? pick(o, key, list, undefined, name) : int(o, key, 1, 3);
      if(v === undefined) continue;

      if(dateStyle || timeStyle) throw new TypeError(`Can't set option ${key} when dateStyle or timeStyle is used`);
      c[key] = v;
    }

    if(dateStyle || timeStyle) Object.assign(c, DATE_STYLE[dateStyle], TIME_STYLE[timeStyle]);
    else if(!Object.keys(c).some(k => k != 'timeZoneName')) Object.assign(c, { year: 'numeric', month: 'numeric', day: 'numeric' });

    c.hourCycle = o.hour12 !== undefined ? (o.hour12 ? 'h12' : 'h23') : pick(o, 'hourCycle', ['h11', 'h12', 'h23', 'h24'], 'h12', name);

    const h12 = c.hourCycle == 'h11' || c.hourCycle == 'h12';

    this.#zone = timeZone(o.timeZone);
    this.#at = dateStyle == 'full' || dateStyle == 'long' || (!dateStyle && c.month == 'long');
    this.#r = { locale: resolve(locales), calendar: 'gregory', numberingSystem: 'latn', timeZone: this.#zone.id };

    if(c.hour) Object.assign(this.#r, { hourCycle: c.hourCycle, hour12: h12 });

    if(dateStyle || timeStyle) Object.assign(this.#r, dateStyle && { dateStyle }, timeStyle && { timeStyle });
    else for(const key in FIELDS) if(c[key] !== undefined) this.#r[key] = key == 'hour' && !h12 ? '2-digit' : c[key];
  }

  get format() {
    return (this.#format ??= date => join(this.formatToParts(date)));
  }

  /* 'Tuesday, March 5, 2024 at 2:07:09 PM UTC' as weekday, literal, month, ... */
  formatToParts(date) {
    const t = date === undefined ? Date.now() : Number(date);
    if(!isFinite(t)) throw new RangeError('Invalid time value');

    const c = this.#c,
      parts = [];
    const add = (type, value) => parts.push({ type, value: String(value) });
    const off = this.#zone.offset(t),
      u = new Date(t + off * 60000);
    const [Y, M, D, W, h, m, s, ms] = ['FullYear', 'Month', 'Date', 'Day', 'Hours', 'Minutes', 'Seconds', 'Milliseconds'].map(k => u['getUTC' + k]());
    // the year of the era without the era: -5 is 6 BC, shown as 6
    const year = c.year == '2-digit' ? (Y > 0 ? Y : 1 - Y) % 100 : Y > 0 ? Y : 1 - Y;

    if(c.weekday) {
      add('weekday', named(DAYS, c.weekday)[W]);
      if(c.year || c.month || c.day) add('literal', ', ');
    }

    if(['narrow', 'short', 'long'].includes(c.month)) {
      // March 5, 2024
      add('month', named(MONTHS, c.month)[M]);

      if(c.day) {
        add('literal', ' ');
        add('day', c.day == '2-digit' ? pad(D) : D);
      }

      if(c.year) {
        add('literal', c.day ? ', ' : ' ');
        add('year', c.year == '2-digit' ? pad(year) : year);
      }
    } else {
      // 3/5/2024
      const fields = [
        ['month', c.month, M + 1],
        ['day', c.day, D],
        ['year', c.year, year],
      ].filter(f => f[1]);

      fields.forEach(([type, style, v], i) => {
        if(i) add('literal', '/');
        add(type, style == '2-digit' ? pad(v) : v);
      });
    }

    if(c.hour || c.minute || c.second) {
      const h12 = c.hourCycle == 'h11' || c.hourCycle == 'h12',
        hv = { h11: h % 12, h12: h % 12 || 12, h23: h, h24: h || 24 }[c.hourCycle];
      const two = style => c.hour || (c.minute && c.second) || style == '2-digit';

      if(parts.length) add('literal', this.#at ? ' at ' : ', ');
      if(c.hour) add('hour', h12 && c.hour == 'numeric' ? hv : pad(hv));

      if(c.minute) {
        if(c.hour) add('literal', ':');
        add('minute', two(c.minute) ? pad(m) : m);
      }

      if(c.second) {
        if(c.hour || c.minute) add('literal', ':');
        add('second', two(c.second) ? pad(s) : s);
      }

      if(c.fractionalSecondDigits) {
        add('literal', '.');
        add('fractionalSecond', pad(ms, 3).slice(0, c.fractionalSecondDigits));
      }

      if(c.hour && h12) {
        add('literal', ' ');
        add('dayPeriod', h < 12 ? 'AM' : 'PM');
      }
    }

    if(c.timeZoneName) {
      const long = c.timeZoneName.startsWith('long');

      add('literal', ' ');
      add('timeZoneName', this.#zone.id == 'UTC' && !c.timeZoneName.endsWith('Offset') ? (long ? 'Coordinated Universal Time' : 'UTC') : gmt(off, long));
    }

    return parts;
  }

  resolvedOptions() {
    return { ...this.#r };
  }
}

/* ---- collation ---- */

/* 'Ä1' -> [['a', '̈', 'upper'], ['1', '', 'digit']]: base, accents, kind */
function letters(s, ignorePunctuation) {
  const out = [];

  for(const [, ch, marks] of String(s)
    .normalize('NFD')
    .matchAll(/(\P{M})(\p{M}*)/gu)) {
    const kind = /[\p{P}\p{S}\s]/u.test(ch) ? 'punct' : /\d/.test(ch) ? 'digit' : ch != ch.toLowerCase() ? 'upper' : 'lower';
    if(kind != 'punct' || !ignorePunctuation) out.push([ch.toLowerCase(), marks, kind]);
  }

  return out;
}

const RANK = { punct: '\x01', digit: '\x02', lower: '\x03', upper: '\x03' };

class Collator {
  #r;
  #compare;

  constructor(locales, options) {
    const o = opts(options),
      name = 'Collator';

    this.#r = {
      locale: resolve(locales),
      usage: pick(o, 'usage', ['sort', 'search'], 'sort', name),
      sensitivity: pick(o, 'sensitivity', ['base', 'accent', 'case', 'variant'], 'variant', name),
      ignorePunctuation: Boolean(o.ignorePunctuation),
      collation: 'default',
      numeric: Boolean(o.numeric),
      caseFirst: pick(o, 'caseFirst', ['upper', 'lower', 'false'], 'false', name),
    };
  }

  /* three levels, compared in turn: letters ('_' < '1' < 'a'), accents (a < á), case (a < A) */
  get compare() {
    const { sensitivity, ignorePunctuation, numeric, caseFirst } = this.#r;

    const primary = l => {
      let key = '';

      for(let i = 0; i < l.length; i++) {
        if(numeric && l[i][2] == 'digit') {
          // a run of digits sorts by value: shorter numbers first
          let run = '';
          while(l[i]?.[2] == 'digit') run += l[i++][0];

          run = run.replace(/^0+(?=\d)/, '');
          key += RANK.digit + String.fromCharCode(48 + run.length) + run;
          i--;
        } else key += RANK[l[i][2]] + l[i][0];
      }

      return key;
    };

    const levels = [primary];
    if(sensitivity == 'accent' || sensitivity == 'variant') levels.push(l => l.map(([, marks]) => marks + '\x00').join(''));
    if(sensitivity == 'case' || sensitivity == 'variant') levels.push(l => l.map(([, , kind]) => ((kind == 'upper') == (caseFirst == 'upper') ? '\x01' : '\x02')).join(''));

    return (this.#compare ??= (a, b) => {
      const x = letters(a, ignorePunctuation),
        y = letters(b, ignorePunctuation);

      for(const level of levels) {
        const p = level(x),
          q = level(y);
        if(p != q) return p < q ? -1 : 1;
      }

      return 0;
    });
  }

  resolvedOptions() {
    return { ...this.#r };
  }
}

/* ---- lists and relative time ---- */

/* separators: between items, before the last of three or more, between two */
const LISTS = {
  conjunction: { long: [', ', ', and ', ' and '], short: [', ', ', & ', ' & '], narrow: [', ', ', ', ', '] },
  disjunction: { long: [', ', ', or ', ' or '], short: [', ', ', or ', ' or '], narrow: [', ', ', or ', ' or '] },
  unit: { long: [', ', ', ', ', '], short: [', ', ', ', ', '], narrow: [' ', ' ', ' '] },
};

class ListFormat {
  #r;

  constructor(locales, options) {
    const o = opts(options);

    this.#r = {
      locale: resolve(locales),
      type: pick(o, 'type', ['conjunction', 'disjunction', 'unit'], 'conjunction', 'ListFormat'),
      style: pick(o, 'style', ['long', 'short', 'narrow'], 'long', 'ListFormat'),
    };
  }

  format(list) {
    return join(this.formatToParts(list));
  }

  /* ['a', 'b', 'c'] -> 'a, b, and c' as element and literal parts */
  formatToParts(list) {
    const items = [...(list ?? [])],
      [middle, last, pair] = LISTS[this.#r.type][this.#r.style];

    for(const item of items) if(typeof item != 'string') throw new TypeError(`Iterable yielded ${item} which is not a string`);

    return items.flatMap((value, i) => {
      const literal = items.length == 2 ? pair : i == items.length - 1 ? last : middle;
      return i
        ? [
            { type: 'literal', value: literal },
            { type: 'element', value },
          ]
        : [{ type: 'element', value }];
    });
  }

  resolvedOptions() {
    return { ...this.#r };
  }
}

/* per unit: short and narrow names (one or one/other), the words for -1 0 1 with numeric auto */
const UNITS = {
  second: ['sec.', 's', [, 'now']],
  minute: ['min.', 'm', [, 'this minute']],
  hour: ['hr.', 'h', [, 'this hour']],
  day: [['day', 'days'], 'd', ['yesterday', 'today', 'tomorrow']],
  week: ['wk.', 'w'],
  month: ['mo.', 'mo'],
  quarter: [['qtr.', 'qtrs.'], 'q'],
  year: ['yr.', 'y'],
};

class RelativeTimeFormat {
  #r;
  #nf;

  constructor(locales, options) {
    const o = opts(options);

    this.#nf = new NumberFormat(locales);
    this.#r = {
      locale: resolve(locales),
      style: pick(o, 'style', ['long', 'short', 'narrow'], 'long', 'RelativeTimeFormat'),
      numeric: pick(o, 'numeric', ['always', 'auto'], 'always', 'RelativeTimeFormat'),
      numberingSystem: 'latn',
    };
  }

  format(value, unit) {
    return join(this.formatToParts(value, unit));
  }

  /* (-1, 'day') -> '1 day ago', or 'yesterday' with numeric auto */
  formatToParts(value, unit) {
    const v = Number(value),
      u = String(unit).replace(/s$/, ''),
      { style, numeric } = this.#r;

    if(!(u in UNITS)) throw new RangeError(`Invalid unit argument for Intl.RelativeTimeFormat.prototype.format() '${unit}'`);
    if(!isFinite(v)) throw new RangeError('Invalid number value');

    const [short, narrow, words] = UNITS[u],
      abbr = [].concat(short);

    if(numeric == 'auto' && (v == 0 || v == 1 || v == -1)) {
      const word = words ? words[v + 1] : `${['last', 'this', 'next'][v + 1]} ${style == 'long' ? u : abbr[0]}`;
      if(word) return [{ type: 'literal', value: word }];
    }

    const number = this.#nf.formatToParts(Math.abs(v)).map(p => ({ ...p, unit: u })),
      one = join(number) == '1';
    const name = style == 'narrow' ? narrow : ' ' + (style == 'long' ? u + (one ? '' : 's') : abbr[one ? 0 : abbr.length - 1]);

    if(v < 0 || Object.is(v, -0)) return [...number, { type: 'literal', value: name + ' ago' }];
    return [{ type: 'literal', value: 'in ' }, ...number, { type: 'literal', value: name }];
  }

  resolvedOptions() {
    return { ...this.#r };
  }
}

/* ---- Intl.Locale ---- */

const KEYWORDS = { calendar: 'ca', collation: 'co', hourCycle: 'hc', caseFirst: 'kf', numeric: 'kn', numberingSystem: 'nu' };

class Locale {
  #base;
  #parts;
  #keys = {};

  /* new Intl.Locale('en-US-u-hc-h23', { region: 'GB' }).toString() is 'en-GB-u-hc-h23' */
  constructor(tag, options) {
    if(typeof tag != 'string' && !(tag instanceof Locale)) throw new TypeError("First argument to Intl.Locale constructor can't be empty or missing");

    const o = opts(options),
      [, lang, script = '', region = '', variants, ext] = TAG.exec(canonical(tag));
    const sub = (name, v) => (o[name] === undefined ? v : '-' + o[name]),
      u = /(?:^|-)u((?:-[a-z\d]{2,8})+)/.exec(ext)?.[1] ?? '';

    for(const [, key, value] of u.matchAll(/-([a-z\d]{2})((?:-[a-z\d]{3,8})*)/g)) this.#keys[key] = value.slice(1) || 'true';
    for(const [name, key] of Object.entries(KEYWORDS)) if(o[name] !== undefined) this.#keys[key] = String(o[name]);

    this.#base = canonical(String(o.language ?? lang) + sub('script', script) + sub('region', region) + variants);
    this.#parts = TAG.exec(this.#base);
  }

  get baseName() {
    return this.#base;
  }

  get language() {
    return this.#parts[1];
  }

  get script() {
    return this.#parts[2]?.slice(1);
  }

  get region() {
    return this.#parts[3]?.slice(1);
  }

  toString() {
    const keys = Object.keys(this.#keys).sort();
    return this.#base + (keys.length ? '-u' + keys.map(k => '-' + k + (this.#keys[k] == 'true' ? '' : '-' + this.#keys[k])).join('') : '');
  }

  // calendar, hourCycle, numeric, ...: the -u- keywords
  static {
    for(const [name, key] of Object.entries(KEYWORDS))
      Object.defineProperty(this.prototype, name, {
        get() {
          const v = this.#keys[key];
          return name == 'numeric' ? v !== undefined && v != 'false' : v;
        },
        configurable: true,
      });

    Object.defineProperty(this.prototype, Symbol.toStringTag, { value: 'Intl.Locale', configurable: true });
  }
}

/* ---- Intl ---- */

const VALUES = { calendar: ['gregory'], collation: [], currency: Object.keys(CURRENCY).sort(), numberingSystem: ['latn'], timeZone: ['UTC'], unit: [] };

function supportedValuesOf(key) {
  if(!(key in VALUES)) throw new RangeError(`Invalid key : ${key}`);
  return [...VALUES[key]];
}

const Intl = define(
  {},
  {
    Collator: callable(service(Collator, 'Collator')),
    DateTimeFormat: callable(service(DateTimeFormat, 'DateTimeFormat')),
    ListFormat: service(ListFormat, 'ListFormat'),
    Locale,
    NumberFormat: callable(service(NumberFormat, 'NumberFormat')),
    PluralRules: service(PluralRules, 'PluralRules'),
    RelativeTimeFormat: service(RelativeTimeFormat, 'RelativeTimeFormat'),
    getCanonicalLocales,
    supportedValuesOf,
  },
);

Object.defineProperty(Intl, Symbol.toStringTag, { value: 'Intl', configurable: true });

export default Intl;
