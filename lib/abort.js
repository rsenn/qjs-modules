import { EventTarget } from './events.js';

export class AbortSignal extends EventTarget {
  constructor() {
    super();

    define(this, { aborted: false, onabort: null, reason: undefined });
  }

  /**
   * Dispatch an event
   *
   * @param  {object} event  The event
   */
  dispatchEvent(event) {
    if(event.type === 'abort') {
      this.aborted = true;

      if(typeof this.onabort === 'function') this.onabort.call(this, event);
    }

    super.dispatchEvent(event);
  }

  /**
   * @see {@link https://developer.mozilla.org/zh-CN/docs/Web/API/AbortSignal/throwIfAborted}
   */
  throwIfAborted() {
    const { aborted, reason = 'Aborted' } = this;
    if(!aborted) return;
    throw reason;
  }

  /**
   * @see {@link https://developer.mozilla.org/zh-CN/docs/Web/API/AbortSignal/timeout_static}
   *
   * @param {number} time    The "active" time in milliseconds before the returned {@link AbortSignal} will abort.
   *                         The value must be within range of 0 and {@link Number.MAX_SAFE_INTEGER}.
   * @returns {AbortSignal}  The signal will abort with its {@link AbortSignal.reason} property set to a `TimeoutError` {@link DOMException} on timeout,
   *                         or an `AbortError` {@link DOMException} if the operation was user-triggered.
   */
  static timeout(time) {
    const controller = new AbortController();
    setTimeout(() => controller.abort(new Error(`TimeoutError: This signal is timeout in ${time}ms`)), time);
    return controller.signal;
  }

  /**
   * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/any_static}
   *
   * @param {Iterable<AbortSignal>}  iterable An {@link Iterable} (such as an {@link Array}) of abort signals.
   *
   * @returns {AbortSignal}   - **Already aborted**, if any of the abort signals given is already aborted.
   *                            The returned {@link AbortSignal}'s reason will be already set to the `reason` of the first abort signal that was already aborted.
   *                          - **Asynchronously aborted**, when any abort signal in `iterable` aborts.
   *                            The `reason` will be set to the reason of the first abort signal that is aborted.
   */
  static any(iterable) {
    const controller = new AbortController();

    /** @this AbortSignal */
    function abort() {
      controller.abort(this.reason);
      clean();
    }

    function clean() {
      for(const signal of iterable) signal.removeEventListener('abort', abort);
    }

    for(const signal of iterable)
      if(signal.aborted) {
        controller.abort(signal.reason);
        break;
      } else signal.addEventListener('abort', abort);

    return controller.signal;
  }

  /**
   * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/AbortSignal/abort_static}
   *
   * @param {string} reason   The reason why the operation was aborted, which can be any JavaScript value.
   *                          If not specified, the reason is set to "AbortError"
   *
   * @returns {AbortSignal}   An AbortSignal instance with the AbortSignal.aborted property set to true,
   *                          and AbortSignal.reason set to the specified or default reason value.
   */
  static abort(reason) {
    const controller = new AbortController();
    controller.abort(reason);
    return controller.signal;
  }
}

AbortSignal.prototype[Symbol.toStringTag] = 'AbortSignal';

define(AbortSignal.prototype, { aborted: false, onabort: null, reason: undefined });

export class AbortController {
  constructor() {
    define(this, { signal: new AbortSignal() });
  }

  /**
   * @see {@link https://developer.mozilla.org/en-US/docs/Web/API/AbortController/abort}
   *
   * @param {string} reason   The reason why the operation was aborted, which can be any JavaScript value.
   *                          If not specified, the reason is set to "AbortError"
   */
  abort(reason) {
    const signalReason = normalizeAbortReason(reason);
    this.signal.reason = signalReason;
    this.signal.dispatchEvent(createAbortEvent(signalReason));
  }
}

AbortController.prototype[Symbol.toStringTag] = 'AbortController';

define(AbortController.prototype, { signal: null });

/** @param {any} reason abort reason */
function normalizeAbortReason(reason) {
  if(reason === undefined) {
    if(typeof document === 'undefined') {
      reason = new Error('This operation was aborted');
      reason.name = 'AbortError';
    } else {
      try {
        reason = new DOMException('signal is aborted without reason');

        /* The DOMException does not support setting the name property directly. */
        define(reason, { name: 'AbortError' }, {});
      } catch(err) {
        /* IE 11 does not support calling the DOMException constructor, use a
           regular error object on it instead. */
        reason = new Error('This operation was aborted');
        reason.name = 'AbortError';
      }
    }
  }

  return reason;
}

/** @param {any} reason abort reason */
function createAbortEvent(reason) {
  let event;
  try {
    event = new Event('abort');
  } catch(e) {
    if(typeof document !== 'undefined') {
      if(!document.createEvent) {
        /* For Internet Explorer 8: */
        event = document.createEventObject();
        event.type = 'abort';
      } else {
        /* For Internet Explorer 11: */
        event = document.createEvent('Event');
        event.initEvent('abort', false, false);
      }
    } else {
      /* Fallback where document isn't available: */
      event = {
        type: 'abort',
        bubbles: false,
        cancelable: false,
      };
    }
  }
  event.reason = reason;
  return event;
}

/**
 * Defines properties
 *
 * @param  {object} obj    Destination object on which to define properties
 * @param  {object} props  Source properties
 * @param  {object} opts   Property descriptors
 * @return {object}       The object given in \param obj
 */
function define(obj, props, opts = { writable: true, configurable: true }) {
  for(let prop in props) Object.defineProperty(obj, prop, { value: props[prop], ...opts });
  return obj;
}

export default AbortController;