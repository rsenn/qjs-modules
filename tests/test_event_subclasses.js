import {
  assert,
  eq,
  tests,
} from './tinytest.js';

import {
  Event,
  UIEvent,
  MouseEvent,
  KeyboardEvent,
  FocusEvent,
  InputEvent,
  WheelEvent,
  Touch,
  TouchList,
  TouchEvent,
  PointerEvent,
} from '../lib/dom.js';

export default tests('Event Subclasses', {
  /* ========== UIEvent ========== */
  'UIEvent: extends Event'() {
    const ev = new UIEvent('focus', { bubbles: true });
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    eq(ev.type, 'focus');
    eq(ev.bubbles, true);
  },

  'UIEvent: view and detail'() {
    const view = { name: 'window' };
    const ev = new UIEvent('resize', { view, detail: 42 });
    eq(ev.view, view);
    eq(ev.detail, 42);
  },

  'UIEvent: default values'() {
    const ev = new UIEvent('scroll');
    eq(ev.view, null);
    eq(ev.detail, 0);
  },

  /* ========== MouseEvent ========== */
  'MouseEvent: extends UIEvent'() {
    const ev = new MouseEvent('click');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof MouseEvent);
  },

  'MouseEvent: coordinates'() {
    const ev = new MouseEvent('click', {
      screenX: 100,
      screenY: 200,
      clientX: 50,
      clientY: 75,
    });
    eq(ev.screenX, 100);
    eq(ev.screenY, 200);
    eq(ev.clientX, 50);
    eq(ev.clientY, 75);
    eq(ev.pageX, 50);
    eq(ev.pageY, 75);
    eq(ev.offsetX, 50);
    eq(ev.offsetY, 75);
  },

  'MouseEvent: modifier keys'() {
    const ev = new MouseEvent('click', {
      ctrlKey: true,
      shiftKey: true,
      altKey: false,
      metaKey: true,
    });
    eq(ev.ctrlKey, true);
    eq(ev.shiftKey, true);
    eq(ev.altKey, false);
    eq(ev.metaKey, true);
  },

  'MouseEvent: button and buttons'() {
    const ev = new MouseEvent('mousedown', { button: 2, buttons: 4 });
    eq(ev.button, 2);
    eq(ev.buttons, 4);
  },

  'MouseEvent: relatedTarget'() {
    const target = { id: 'element' };
    const ev = new MouseEvent('mouseover', { relatedTarget: target });
    eq(ev.relatedTarget, target);
  },

  'MouseEvent: getModifierState'() {
    const ev = new MouseEvent('click', { ctrlKey: true, shiftKey: false });
    eq(ev.getModifierState('Control'), true);
    eq(ev.getModifierState('Shift'), false);
    eq(ev.getModifierState('Alt'), false);
    eq(ev.getModifierState('Meta'), false);
    eq(ev.getModifierState('Unknown'), false);
  },

  'MouseEvent: default values'() {
    const ev = new MouseEvent('click');
    eq(ev.screenX, 0);
    eq(ev.screenY, 0);
    eq(ev.clientX, 0);
    eq(ev.clientY, 0);
    eq(ev.ctrlKey, false);
    eq(ev.shiftKey, false);
    eq(ev.altKey, false);
    eq(ev.metaKey, false);
    eq(ev.button, 0);
    eq(ev.buttons, 0);
    eq(ev.relatedTarget, null);
  },

  /* ========== KeyboardEvent ========== */
  'KeyboardEvent: extends UIEvent'() {
    const ev = new KeyboardEvent('keydown');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof KeyboardEvent);
  },

  'KeyboardEvent: key and code'() {
    const ev = new KeyboardEvent('keydown', { key: 'Enter', code: 'Enter' });
    eq(ev.key, 'Enter');
    eq(ev.code, 'Enter');
  },

  'KeyboardEvent: location constants'() {
    eq(KeyboardEvent.DOM_KEY_LOCATION_STANDARD, 0);
    eq(KeyboardEvent.DOM_KEY_LOCATION_LEFT, 1);
    eq(KeyboardEvent.DOM_KEY_LOCATION_RIGHT, 2);
    eq(KeyboardEvent.DOM_KEY_LOCATION_NUMPAD, 3);
  },

  'KeyboardEvent: location property'() {
    const ev = new KeyboardEvent('keydown', { location: KeyboardEvent.DOM_KEY_LOCATION_LEFT });
    eq(ev.location, 1);
  },

  'KeyboardEvent: modifier keys'() {
    const ev = new KeyboardEvent('keydown', {
      ctrlKey: true,
      shiftKey: false,
      altKey: true,
      metaKey: false,
    });
    eq(ev.ctrlKey, true);
    eq(ev.shiftKey, false);
    eq(ev.altKey, true);
    eq(ev.metaKey, false);
  },

  'KeyboardEvent: repeat and isComposing'() {
    const ev = new KeyboardEvent('keydown', { repeat: true, isComposing: true });
    eq(ev.repeat, true);
    eq(ev.isComposing, true);
  },

  'KeyboardEvent: getModifierState'() {
    const ev = new KeyboardEvent('keydown', { ctrlKey: true, altKey: true });
    eq(ev.getModifierState('Control'), true);
    eq(ev.getModifierState('Alt'), true);
    eq(ev.getModifierState('Shift'), false);
    eq(ev.getModifierState('Meta'), false);
  },

  'KeyboardEvent: default values'() {
    const ev = new KeyboardEvent('keydown');
    eq(ev.key, '');
    eq(ev.code, '');
    eq(ev.location, 0);
    eq(ev.ctrlKey, false);
    eq(ev.shiftKey, false);
    eq(ev.altKey, false);
    eq(ev.metaKey, false);
    eq(ev.repeat, false);
    eq(ev.isComposing, false);
  },

  /* ========== FocusEvent ========== */
  'FocusEvent: extends UIEvent'() {
    const ev = new FocusEvent('focus');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof FocusEvent);
  },

  'FocusEvent: relatedTarget'() {
    const target = { id: 'button' };
    const ev = new FocusEvent('blur', { relatedTarget: target });
    eq(ev.relatedTarget, target);
  },

  'FocusEvent: default relatedTarget is null'() {
    const ev = new FocusEvent('focus');
    eq(ev.relatedTarget, null);
  },

  /* ========== InputEvent ========== */
  'InputEvent: extends UIEvent'() {
    const ev = new InputEvent('input');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof InputEvent);
  },

  'InputEvent: data and inputType'() {
    const ev = new InputEvent('input', { data: 'a', inputType: 'insertText' });
    eq(ev.data, 'a');
    eq(ev.inputType, 'insertText');
  },

  'InputEvent: isComposing'() {
    const ev = new InputEvent('input', { isComposing: true });
    eq(ev.isComposing, true);
  },

  'InputEvent: default values'() {
    const ev = new InputEvent('input');
    eq(ev.data, null);
    eq(ev.inputType, '');
    eq(ev.isComposing, false);
  },

  /* ========== WheelEvent ========== */
  'WheelEvent: extends MouseEvent'() {
    const ev = new WheelEvent('wheel');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof MouseEvent);
    assert(ev instanceof WheelEvent);
  },

  'WheelEvent: delta properties'() {
    const ev = new WheelEvent('wheel', { deltaX: 10, deltaY: -20, deltaZ: 5 });
    eq(ev.deltaX, 10);
    eq(ev.deltaY, -20);
    eq(ev.deltaZ, 5);
  },

  'WheelEvent: deltaMode constants'() {
    eq(WheelEvent.DOM_DELTA_PIXEL, 0);
    eq(WheelEvent.DOM_DELTA_LINE, 1);
    eq(WheelEvent.DOM_DELTA_PAGE, 2);
  },

  'WheelEvent: deltaMode property'() {
    const ev = new WheelEvent('wheel', { deltaMode: WheelEvent.DOM_DELTA_LINE });
    eq(ev.deltaMode, 1);
  },

  'WheelEvent: inherits MouseEvent properties'() {
    const ev = new WheelEvent('wheel', { clientX: 100, clientY: 200, ctrlKey: true });
    eq(ev.clientX, 100);
    eq(ev.clientY, 200);
    eq(ev.ctrlKey, true);
  },

  'WheelEvent: default values'() {
    const ev = new WheelEvent('wheel');
    eq(ev.deltaX, 0);
    eq(ev.deltaY, 0);
    eq(ev.deltaZ, 0);
    eq(ev.deltaMode, 0);
  },

  /* ========== Touch ========== */
  'Touch: constructor with properties'() {
    const target = { id: 'div' };
    const touch = new Touch({
      identifier: 1,
      target,
      screenX: 100,
      screenY: 200,
      clientX: 50,
      clientY: 75,
      pageX: 60,
      pageY: 85,
    });
    eq(touch.identifier, 1);
    eq(touch.target, target);
    eq(touch.screenX, 100);
    eq(touch.screenY, 200);
    eq(touch.clientX, 50);
    eq(touch.clientY, 75);
    eq(touch.pageX, 60);
    eq(touch.pageY, 85);
  },

  'Touch: default values'() {
    const touch = new Touch();
    eq(touch.identifier, 0);
    eq(touch.target, null);
    eq(touch.screenX, 0);
    eq(touch.screenY, 0);
    eq(touch.clientX, 0);
    eq(touch.clientY, 0);
    eq(touch.pageX, 0);
    eq(touch.pageY, 0);
  },

  /* ========== TouchList ========== */
  'TouchList: length and item'() {
    const t1 = new Touch({ identifier: 1 });
    const t2 = new Touch({ identifier: 2 });
    const list = new TouchList([t1, t2]);
    eq(list.length, 2);
    eq(list.item(0), t1);
    eq(list.item(1), t2);
    eq(list.item(2), null);
  },

  'TouchList: iterable'() {
    const t1 = new Touch({ identifier: 1 });
    const t2 = new Touch({ identifier: 2 });
    const list = new TouchList([t1, t2]);
    const arr = [...list];
    eq(arr.length, 2);
    eq(arr[0], t1);
    eq(arr[1], t2);
  },

  /* ========== TouchEvent ========== */
  'TouchEvent: extends UIEvent'() {
    const ev = new TouchEvent('touchstart');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof TouchEvent);
  },

  'TouchEvent: touch lists'() {
    const t1 = new Touch({ identifier: 1 });
    const t2 = new Touch({ identifier: 2 });
    const touches = new TouchList([t1, t2]);
    const targetTouches = new TouchList([t1]);
    const changedTouches = new TouchList([t2]);

    const ev = new TouchEvent('touchmove', { touches, targetTouches, changedTouches });
    eq(ev.touches, touches);
    eq(ev.targetTouches, targetTouches);
    eq(ev.changedTouches, changedTouches);
  },

  'TouchEvent: modifier keys'() {
    const ev = new TouchEvent('touchstart', { ctrlKey: true, shiftKey: true });
    eq(ev.ctrlKey, true);
    eq(ev.shiftKey, true);
    eq(ev.altKey, false);
    eq(ev.metaKey, false);
  },

  'TouchEvent: default values'() {
    const ev = new TouchEvent('touchstart');
    assert(ev.touches instanceof TouchList);
    assert(ev.targetTouches instanceof TouchList);
    assert(ev.changedTouches instanceof TouchList);
    eq(ev.touches.length, 0);
    eq(ev.ctrlKey, false);
    eq(ev.shiftKey, false);
    eq(ev.altKey, false);
    eq(ev.metaKey, false);
  },

  /* ========== PointerEvent ========== */
  'PointerEvent: extends MouseEvent'() {
    const ev = new PointerEvent('pointerdown');
    assert(ev instanceof Event);
    assert(ev instanceof UIEvent);
    assert(ev instanceof MouseEvent);
    assert(ev instanceof PointerEvent);
  },

  'PointerEvent: pointer-specific properties'() {
    const ev = new PointerEvent('pointerdown', {
      pointerId: 42,
      width: 100,
      height: 200,
      pressure: 0.5,
      tiltX: 10,
      tiltY: -5,
      pointerType: 'pen',
      isPrimary: true,
    });
    eq(ev.pointerId, 42);
    eq(ev.width, 100);
    eq(ev.height, 200);
    eq(ev.pressure, 0.5);
    eq(ev.tiltX, 10);
    eq(ev.tiltY, -5);
    eq(ev.pointerType, 'pen');
    eq(ev.isPrimary, true);
  },

  'PointerEvent: inherits MouseEvent properties'() {
    const ev = new PointerEvent('pointerdown', {
      clientX: 150,
      clientY: 250,
      button: 1,
    });
    eq(ev.clientX, 150);
    eq(ev.clientY, 250);
    eq(ev.button, 1);
  },

  'PointerEvent: default values'() {
    const ev = new PointerEvent('pointerdown');
    eq(ev.pointerId, 0);
    eq(ev.width, 1);
    eq(ev.height, 1);
    eq(ev.pressure, 0);
    eq(ev.tiltX, 0);
    eq(ev.tiltY, 0);
    eq(ev.pointerType, '');
    eq(ev.isPrimary, false);
  },

  /* ========== Symbol.toStringTag ========== */
  'Event subclasses have correct toStringTag'() {
    eq(new UIEvent('x')[Symbol.toStringTag], 'UIEvent');
    eq(new MouseEvent('x')[Symbol.toStringTag], 'MouseEvent');
    eq(new KeyboardEvent('x')[Symbol.toStringTag], 'KeyboardEvent');
    eq(new FocusEvent('x')[Symbol.toStringTag], 'FocusEvent');
    eq(new InputEvent('x')[Symbol.toStringTag], 'InputEvent');
    eq(new WheelEvent('x')[Symbol.toStringTag], 'WheelEvent');
    eq(new Touch()[Symbol.toStringTag], 'Touch');
    eq(new TouchList()[Symbol.toStringTag], 'TouchList');
    eq(new TouchEvent('x')[Symbol.toStringTag], 'TouchEvent');
    eq(new PointerEvent('x')[Symbol.toStringTag], 'PointerEvent');
  },
});
