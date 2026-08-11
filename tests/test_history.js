import { assert, eq, tests } from './tinytest.js';
import { Window, History, PopStateEvent, HashChangeEvent } from '../lib/dom.js';

export default tests('History API', {
  'History: initial state'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    eq(history.length, 1, 'Initial history length should be 1');
    eq(history.state, null, 'Initial state should be null');
  },

  'History: pushState adds entry'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, 'Page 1', '/page1');

    eq(history.length, 2, 'History length should be 2 after pushState');
    eq(history.state.page, 1, 'State should be updated');
    eq(win.location.pathname, '/page1', 'Location should be updated');
  },

  'History: pushState with absolute URL'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 2 }, 'Page 2', 'https://example.com/page2');

    eq(history.length, 2);
    eq(win.location.href, 'https://example.com/page2');
  },

  'History: pushState clears forward history'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');
    history.pushState({ page: 3 }, '', '/page3');

    eq(history.length, 4);

    history.back();
    history.back();

    eq(history.state.page, 1);

    // Push new state - should clear forward history
    history.pushState({ page: 4 }, '', '/page4');

    eq(history.length, 3, 'Forward history should be cleared');
    eq(history.state.page, 4);
  },

  'History: replaceState replaces current entry'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');
    history.replaceState({ page: 'replaced' }, '', '/replaced');

    eq(history.length, 2, 'Length should not change after replaceState');
    eq(history.state.page, 'replaced');
    eq(win.location.pathname, '/replaced');
  },

  'History: back() navigates backward'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');

    history.back();

    eq(history.state.page, 1);
    eq(win.location.pathname, '/page1');

    history.back();

    eq(history.state, null);
    eq(win.location.pathname, '/');
  },

  'History: forward() navigates forward'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');

    history.back();
    history.back();

    history.forward();

    eq(history.state.page, 1);
    eq(win.location.pathname, '/page1');

    history.forward();

    eq(history.state.page, 2);
    eq(win.location.pathname, '/page2');
  },

  'History: go(delta) navigates by delta'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');
    history.pushState({ page: 3 }, '', '/page3');

    history.go(-2);

    eq(history.state.page, 1);

    history.go(2);

    eq(history.state.page, 3);
  },

  'History: go(0) does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');

    const stateBefore = history.state;
    history.go(0);
    const stateAfter = history.state;

    eq(stateBefore, stateAfter, 'go(0) should not change state');
  },

  'History: back() at beginning does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    const urlBefore = win.location.href;
    history.back();
    const urlAfter = win.location.href;

    eq(urlBefore, urlAfter, 'back() at beginning should not change URL');
  },

  'History: forward() at end does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');

    const urlBefore = win.location.href;
    history.forward();
    const urlAfter = win.location.href;

    eq(urlBefore, urlAfter, 'forward() at end should not change URL');
  },

  'History: go() with large delta does nothing'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1');

    const stateBefore = history.state;
    history.go(100);
    const stateAfter = history.state;

    eq(stateBefore, stateAfter, 'go() with out-of-bounds delta should not change state');

    history.go(-100);

    eq(history.state, stateBefore, 'go() with negative out-of-bounds delta should not change state');
  },

  'History: popstate event fired on navigation'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    let popstateFired = false;
    let popstateState = null;
    let popstateOldState = null;

    win.addEventListener('popstate', (e) => {
      popstateFired = true;
      popstateState = e.state;
      popstateOldState = e.oldState;
    });

    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');

    history.back();

    assert(popstateFired, 'popstate event should fire');
    eq(popstateState.page, 1, 'popstate state should be new state');
    eq(popstateOldState.page, 2, 'popstate oldState should be previous state');
  },

  'History: popstate event not fired on pushState/replaceState'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    let popstateCount = 0;

    win.addEventListener('popstate', () => {
      popstateCount++;
    });

    history.pushState({ page: 1 }, '', '/page1');
    history.replaceState({ page: 2 }, '', '/page2');

    eq(popstateCount, 0, 'popstate should not fire on pushState/replaceState');
  },

  'History: hashchange event on hash change'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    let hashchangeFired = false;
    let oldURL = '';
    let newURL = '';

    win.addEventListener('hashchange', (e) => {
      hashchangeFired = true;
      oldURL = e.oldURL;
      newURL = e.newURL;
    });

    history.pushState({ page: 1 }, '', '/page1#section');

    assert(hashchangeFired, 'hashchange event should fire');
    eq(oldURL, 'https://example.com/', 'oldURL should be previous URL');
    eq(newURL, 'https://example.com/page1#section', 'newURL should be new URL');
  },

  'History: hashchange event on hash change via go()'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1#section1');
    history.pushState({ page: 2 }, '', '/page1#section2');

    let hashchangeFired = false;

    win.addEventListener('hashchange', () => {
      hashchangeFired = true;
    });

    history.back();

    assert(hashchangeFired, 'hashchange should fire when navigating to different hash');
  },

  'History: no hashchange event when hash unchanged'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.pushState({ page: 1 }, '', '/page1#section');

    let hashchangeCount = 0;

    win.addEventListener('hashchange', () => {
      hashchangeCount++;
    });

    history.pushState({ page: 2 }, '', '/page2#section');

    eq(hashchangeCount, 0, 'hashchange should not fire when hash is unchanged');
  },

  'PopStateEvent: constructor and properties'() {
    const event = new PopStateEvent('popstate', {
      state: { page: 1 },
      oldState: { page: 0 },
    });

    eq(event.type, 'popstate');
    eq(event.state.page, 1);
    eq(event.oldState.page, 0);
  },

  'PopStateEvent: default values'() {
    const event = new PopStateEvent('popstate');

    eq(event.state, null);
    eq(event.oldState, null);
  },

  'HashChangeEvent: constructor and properties'() {
    const event = new HashChangeEvent('hashchange', {
      oldURL: 'https://example.com/',
      newURL: 'https://example.com/#section',
    });

    eq(event.type, 'hashchange');
    eq(event.oldURL, 'https://example.com/');
    eq(event.newURL, 'https://example.com/#section');
  },

  'HashChangeEvent: default values'() {
    const event = new HashChangeEvent('hashchange');

    eq(event.oldURL, '');
    eq(event.newURL, '');
  },

  'History: toStringTag'() {
    const win = new Window({ href: 'https://example.com/' });
    eq(win.history[Symbol.toStringTag], 'History');
  },

  'PopStateEvent: toStringTag'() {
    const event = new PopStateEvent('popstate');
    eq(event[Symbol.toStringTag], 'PopStateEvent');
  },

  'HashChangeEvent: toStringTag'() {
    const event = new HashChangeEvent('hashchange');
    eq(event[Symbol.toStringTag], 'HashChangeEvent');
  },

  'History: complex navigation scenario'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    // Build up history
    history.pushState({ page: 1 }, '', '/page1');
    history.pushState({ page: 2 }, '', '/page2');
    history.pushState({ page: 3 }, '', '/page3');

    eq(history.length, 4);

    // Go back two steps
    history.go(-2);
    eq(history.state.page, 1);

    // Push new state (clears forward history)
    history.pushState({ page: 4 }, '', '/page4');

    eq(history.length, 3);
    eq(history.state.page, 4);

    // Try to go forward (should do nothing)
    history.forward();
    eq(history.state.page, 4);

    // Go back to beginning
    history.go(-2);
    eq(history.state, null);
    eq(win.location.pathname, '/');
  },

  'History: replaceState on initial entry'() {
    const win = new Window({ href: 'https://example.com/' });
    const history = win.history;

    history.replaceState({ initial: true }, '', '/home');

    eq(history.length, 1);
    eq(history.state.initial, true);
    eq(win.location.pathname, '/home');
  },

  'History: multiple windows have independent history'() {
    const win1 = new Window({ href: 'https://example.com/' });
    const win2 = new Window({ href: 'https://example.com/' });

    win1.history.pushState({ win: 1 }, '', '/page1');
    win2.history.pushState({ win: 2 }, '', '/page2');

    eq(win1.history.length, 2);
    eq(win2.history.length, 2);
    eq(win1.history.state.win, 1);
    eq(win2.history.state.win, 2);
  },
});
