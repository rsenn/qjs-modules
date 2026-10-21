// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* the EventTarget, Event and AbortController globals, as in the DOM; on qjsm
 * lib/events.js, lib/dom.js and lib/abort.js through lib/node.js */
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'an abort event is an Event with the signal as target and this'() {
    const c = new AbortController();
    let seen;

    c.signal.addEventListener('abort', function(e) {
      seen = [e instanceof Event, e.target === c.signal, this === c.signal, e.type].join();
    });
    c.abort(new DOMException('stop', 'AbortError'));

    eq('true,true,true,abort', seen);
    eq('AbortError', c.signal.reason.name);
    eq(true, c.signal.aborted);
  },

  'currentTarget is set during dispatch only'() {
    const t = new EventTarget(), e = new Event('x');
    let during;

    t.addEventListener('x', ev => (during = ev.currentTarget));
    t.dispatchEvent(e);

    assert(during === t);
    eq(null, e.currentTarget);
  },

  'dispatchEvent returns false when the default was prevented'() {
    const t = new EventTarget();

    t.addEventListener('x', e => e.preventDefault());

    eq(false, t.dispatchEvent(new Event('x', { cancelable: true })));
    eq(true, t.dispatchEvent(new Event('y', { cancelable: true })));
  },

  'the once option removes the listener after the first event'() {
    const t = new EventTarget();
    let n = 0;

    t.addEventListener('x', () => n++, { once: true });
    t.dispatchEvent(new CustomEvent('x', { detail: 1 }));
    t.dispatchEvent(new CustomEvent('x'));

    eq(1, n);
  },

  'the signal option removes the listener on abort'() {
    const t = new EventTarget(), c = new AbortController();
    let n = 0;

    t.addEventListener('x', () => n++, { signal: c.signal });
    t.dispatchEvent(new Event('x'));
    c.abort();
    t.dispatchEvent(new Event('x'));

    eq(1, n);
  },

  'AbortSignal.any() follows the first signal to abort'() {
    const a = new AbortController(), b = new AbortController();
    const any = AbortSignal.any([a.signal, b.signal]);

    eq(false, any.aborted);
    b.abort('why');
    eq(true, any.aborted);
    eq('why', any.reason);
  },

  'CustomEvent carries detail'() {
    eq(5, new CustomEvent('z', { detail: 5 }).detail);
    eq(null, new CustomEvent('z').detail);
  },
});
