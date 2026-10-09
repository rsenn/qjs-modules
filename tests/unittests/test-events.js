// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* lib/events.js: EventEmitter and helpers, behaviour as in Node */
import { EventEmitter, once, on, getEventListeners, listenerCount, setMaxListeners } from 'node:events';
import { assert, eq, tests } from '../../lib/tinytest.js';

tests({
  'on() returns the emitter and handlers get only the emit arguments'() {
    const e = new EventEmitter(), got = [];

    assert(e.on('x', (...a) => got.push(a)) === e);
    assert(e.emit('x', 1, 2));
    eq(JSON.stringify(got), '[[1,2]]');
    assert(!e.emit('nothing'));
  },
  'once() fires one time; prependListener runs first'() {
    const e = new EventEmitter(), order = [];

    e.once('x', () => order.push('once'));
    e.on('x', () => order.push('on'));
    e.prependListener('x', () => order.push('first'));
    e.emit('x');
    e.emit('x');
    eq(order.join(), 'first,once,on,first,on');
  },
  'off(), listeners(), listenerCount(), eventNames()'() {
    const e = new EventEmitter(), f = () => {}, sym = Symbol('s');

    e.on('a', f).once('b', f).on(sym, f);
    eq(e.listenerCount('a'), 1);
    assert(e.listeners('b')[0] === f);
    eq(e.eventNames().length, 3);
    assert(e.off('a', f) === e);
    eq(e.listenerCount('a'), 0);
    eq(listenerCount(e, 'b'), 1);
    assert(getEventListeners(e, sym)[0] === f);
  },
  'emit("error") without a listener throws'() {
    const e = new EventEmitter(), boom = new Error('boom');
    let caught;

    try {
      e.emit('error', boom);
    } catch(x) {
      caught = x;
    }
    assert(caught === boom);
    e.on('error', () => {});
    assert(e.emit('error', boom));
  },
  'newListener and removeListener events'() {
    const e = new EventEmitter(), log = [];

    e.on('newListener', t => log.push('new:' + t));
    e.on('removeListener', t => log.push('rm:' + t));

    const f = () => {};

    e.on('x', f);
    e.off('x', f);
    eq(log.join(), 'new:removeListener,new:x,rm:x');
  },
  'removeAllListeners() with and without a type'() {
    const e = new EventEmitter();

    e.on('a', () => {}).on('b', () => {});
    e.removeAllListeners('a');
    eq(e.eventNames().join(), 'b');
    e.removeAllListeners();
    eq(e.eventNames().length, 0);
  },
  'max listeners'() {
    const e = new EventEmitter();

    eq(e.getMaxListeners(), 10);
    assert(e.setMaxListeners(3) === e);
    eq(e.getMaxListeners(), 3);
    setMaxListeners(20, e);
    eq(e.getMaxListeners(), 20);
  },
  'an object inheriting EventEmitter.prototype works'() {
    const o = Object.setPrototypeOf({}, EventEmitter.prototype);
    let n = 0;

    o.on('x', () => n++);
    o.emit('x');
    eq(n, 1);
  },
  async 'events.once() resolves the argument array'() {
    const e = new EventEmitter();

    setTimeout(() => e.emit('go', 7, 8), 1);
    eq(JSON.stringify(await once(e, 'go')), '[7,8]');
  },
  async 'events.once() rejects on error'() {
    const e = new EventEmitter();

    setTimeout(() => e.emit('error', new Error('bad')), 1);

    let msg;

    try {
      await once(e, 'go');
    } catch(x) {
      msg = x.message;
    }
    eq(msg, 'bad');
  },
  async 'events.on() iterates emissions'() {
    const e = new EventEmitter(), seen = [];

    setTimeout(() => {
      e.emit('n', 1);
      e.emit('n', 2);
    }, 1);

    for await(const [v] of on(e, 'n')) {
      seen.push(v);
      if(v == 2) break;
    }
    eq(seen.join(), '1,2');
  },
});
