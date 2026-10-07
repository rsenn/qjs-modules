/* perf_hooks.js: Node's `perf_hooks` (also Bun's, Deno's): W3C High Resolution Time and User Timing.
 * depends on: misc (clock), timers (observer delivery).
 * rule: no native code beyond the clock; not implemented: timerify, histograms,
 *       monitorEventLoopDelay, eventLoopUtilization, resource timing.
 *
 * ```js
 * import { performance, PerformanceObserver } from 'perf_hooks';
 *
 * performance.mark('start');
 * ...                                         // work
 * performance.measure('work', 'start');       // PerformanceMeasure { duration: ms }
 * new PerformanceObserver(list => console.log(list.getEntries())).observe({ entryTypes: ['measure'] });
 * ```
 *
 * | export                                        | notes                                       |
 * | --------------------------------------------- | ------------------------------------------- |
 * | performance                                   | now(), timeOrigin, mark(), measure(), getEntries*(), clear*(), toJSON() |
 * | Performance, PerformanceEntry, PerformanceMark, PerformanceMeasure | the entry classes     |
 * | PerformanceObserver, PerformanceObserverEntryList | observe({ entryTypes } or { type, buffered }) |
 * | now                                           | `performance.now`, kept for older callers   |
 */
import { getPerformanceCounter } from 'misc';
import { setTimeout } from 'timers';

const origin = getPerformanceCounter();
const timeOrigin = Date.now();

export function now() {
  return getPerformanceCounter() - origin;
}

const nodeError = (Type, code, message) => Object.assign(new Type(message), { code });
const missingArgs = name => nodeError(TypeError, 'ERR_MISSING_ARGS', `The "${name}" argument must be specified`);
const invalidOptions = message => nodeError(TypeError, 'ERR_PERFORMANCE_MEASURE_INVALID_OPTIONS', message);
const clone = v => (v === undefined ? null : typeof structuredClone == 'function' ? structuredClone(v) : v);

export class PerformanceEntry {
  #name;
  #type;
  #start;
  #duration;
  #detail;

  constructor(name, type, start, duration, detail = null) {
    this.#name = name;
    this.#type = type;
    this.#start = start;
    this.#duration = duration;
    this.#detail = detail;
  }

  get name() {
    return this.#name;
  }

  get entryType() {
    return this.#type;
  }

  get startTime() {
    return this.#start;
  }

  get duration() {
    return this.#duration;
  }

  get detail() {
    return this.#detail;
  }

  toJSON() {
    return { name: this.name, entryType: this.entryType, startTime: this.startTime, duration: this.duration, detail: this.detail };
  }

  get [Symbol.toStringTag]() {
    return this.constructor.name;
  }
}

/* new PerformanceMark(name, { startTime = now(), detail }): a named timestamp; not recorded unless made by mark() */
export class PerformanceMark extends PerformanceEntry {
  constructor(name, options = {}) {
    if(arguments.length == 0) throw missingArgs('name');

    const { startTime = now(), detail } = options ?? {};

    if(typeof startTime != 'number') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "options.startTime" property must be of type number');
    if(startTime < 0) throw nodeError(TypeError, 'ERR_PERFORMANCE_INVALID_TIMESTAMP', `${startTime} is not a valid timestamp`);

    super(String(name), 'mark', startTime, 0, clone(detail));
  }
}

export class PerformanceMeasure extends PerformanceEntry {}

const byStart = (a, b) => a.startTime - b.startTime;

export class PerformanceObserverEntryList {
  #entries;

  constructor(entries) {
    this.#entries = [...entries].sort(byStart);
  }

  getEntries() {
    return [...this.#entries];
  }

  getEntriesByName(name, type) {
    if(arguments.length == 0) throw missingArgs('name');

    return this.#entries.filter(e => e.name == name && (type === undefined || e.entryType == type));
  }

  getEntriesByType(type) {
    if(arguments.length == 0) throw missingArgs('type');

    return this.#entries.filter(e => e.entryType == type);
  }
}

const buffer = [];
const observers = new Set();
const supported = Object.freeze(['mark', 'measure']);

export class PerformanceObserver {
  #callback;
  #types = new Set();
  #queue = [];
  #scheduled = false;

  static get supportedEntryTypes() {
    return supported;
  }

  constructor(callback) {
    if(typeof callback != 'function') throw nodeError(TypeError, 'ERR_INVALID_ARG_TYPE', 'The "callback" argument must be of type function');

    this.#callback = callback;
  }

  /* observe({ entryTypes: ['mark'] }) replaces the types; observe({ type, buffered }) adds one */
  observe(options) {
    if(options === null || typeof options != 'object') throw missingArgs('options');

    const { entryTypes, type, buffered } = options;

    if(entryTypes === undefined && type === undefined) throw missingArgs('options.entryTypes');
    if(entryTypes !== undefined && type !== undefined) throw nodeError(TypeError, 'ERR_INVALID_ARG_VALUE', "The property 'options.entryTypes' can not be set with options.type");

    if(entryTypes !== undefined) {
      this.#types = new Set([...entryTypes].filter(t => supported.includes(t)));
    } else if(supported.includes(type)) {
      this.#types.add(type);

      if(buffered) this.#queue.push(...buffer.filter(e => e.entryType == type && !this.#queue.includes(e)));
    }

    if(this.#types.size) observers.add(this);
    else observers.delete(this);

    if(this.#queue.length) this.#schedule();
  }

  disconnect() {
    this.#types.clear();
    this.#queue = [];
    observers.delete(this);
  }

  takeRecords() {
    const records = this.#queue;

    this.#queue = [];

    return records;
  }

  /* called by the timeline for every new mark or measure */
  static notify(entry) {
    for(const o of observers) o.#push(entry);
  }

  #push(entry) {
    if(!this.#types.has(entry.entryType)) return;

    this.#queue.push(entry);
    this.#schedule();
  }

  #schedule() {
    if(this.#scheduled) return;

    this.#scheduled = true;

    setTimeout(() => {
      this.#scheduled = false;

      if(this.#queue.length) this.#callback(new PerformanceObserverEntryList(this.takeRecords()), this);
    }, 0);
  }

  get [Symbol.toStringTag]() {
    return 'PerformanceObserver';
  }
}

const record = entry => {
  buffer.push(entry);
  PerformanceObserver.notify(entry);

  return entry;
};

/* the time of a mark name (the latest one), or a number as given */
function timeOf(v, positional) {
  if(typeof v == 'number' && !positional) return v;

  const name = String(v);

  for(let i = buffer.length - 1; i >= 0; i--) if(buffer[i].entryType == 'mark' && buffer[i].name == name) return buffer[i].startTime;

  throw Object.assign(new SyntaxError(`The "${name}" performance mark has not been set`), { code: 12 });
}

export class Performance {
  now() {
    return now();
  }

  get timeOrigin() {
    return timeOrigin;
  }

  /* mark(name, { startTime, detail }): record and return a PerformanceMark */
  mark(name, options) {
    if(arguments.length == 0) throw missingArgs('name');

    return record(new PerformanceMark(name, options));
  }

  /* measure(name, startMark?, endMark?) or measure(name, { start, end, duration, detail }): a PerformanceMeasure
   *
   * ```js
   * performance.measure('m', 'a', 'b')               // from mark a to mark b
   * performance.measure('m', { start: 10, duration: 5 })  // 10 .. 15
   * ```
   *
   * a name is a mark (SyntaxError if unset), a number a timestamp; start defaults to 0, end to now().
   */
  measure(name, startOrOptions, endMark) {
    if(arguments.length == 0) throw missingArgs('name');

    let start, end, duration, detail, positional = true;

    if(startOrOptions !== null && typeof startOrOptions == 'object') {
      if(endMark !== undefined) throw invalidOptions("Can't set endMark when options is an object");

      ({ start, end, duration, detail } = startOrOptions);
      positional = false;

      if(start !== undefined && end !== undefined && duration !== undefined) throw invalidOptions("Can't set start, end and duration together");
      if(start === undefined && end === undefined && duration !== undefined) throw invalidOptions('Must set start or end with duration');
    } else {
      start = startOrOptions;
      end = endMark;
    }

    const endTime = end !== undefined ? timeOf(end, positional) : start !== undefined && duration !== undefined ? timeOf(start, positional) + duration : now();
    const startTime = start !== undefined ? timeOf(start, positional) : duration !== undefined && end !== undefined ? endTime - duration : 0;

    return record(new PerformanceMeasure(String(name), 'measure', startTime, endTime - startTime, clone(detail)));
  }

  getEntries() {
    return [...buffer].sort(byStart);
  }

  getEntriesByName(name, type) {
    if(arguments.length == 0) throw missingArgs('name');

    return this.getEntries().filter(e => e.name == name && (type === undefined || e.entryType == type));
  }

  getEntriesByType(type) {
    if(arguments.length == 0) throw missingArgs('type');

    return this.getEntries().filter(e => e.entryType == type);
  }

  clearMarks(name) {
    clear('mark', name);
  }

  clearMeasures(name) {
    clear('measure', name);
  }

  toJSON() {
    return { timeOrigin };
  }

  get [Symbol.toStringTag]() {
    return 'Performance';
  }
}

function clear(type, name) {
  for(let i = buffer.length - 1; i >= 0; i--) if(buffer[i].entryType == type && (name === undefined || buffer[i].name == String(name))) buffer.splice(i, 1);
}

export const performance = new Performance();

export default performance;
