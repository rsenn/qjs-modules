/* terminal/capabilities.js: which optional features the terminal supports.
 * depends on: terminal.js (ttyWrite, ttyRead, ttySetRaw, mode writers), terminal/internal.js.
 * rule: queryCapabilities() asks with one write and reads the replies in raw mode. */

import { EAGAIN, bracketedPasteDisable, bracketedPasteEnable, focusEventsDisable, focusEventsEnable, graphemeClustersDisable, graphemeClustersEnable, kittyKeyboardDisable, kittyKeyboardEnable, syncOutput, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } from 'terminal/core';
import { CSI, TextDecoder, sleep } from 'terminal/internal';

/* DECRQM for each mode asked about, then the kitty keyboard flags, the terminal's name and version, and
 * the primary device attributes last: they are answered in order, so that answer ends the wait */
const PROBE_MODES = [2026, 2027, 2004, 1004, 1006];

const PROBE = PROBE_MODES.map(m => `${CSI}?${m}$p`).join('') + CSI + '?u' + CSI + '>q' + CSI + 'c';

/* parseCapabilityReplies(str): the answers to queryCapabilities()' questions found in str
 *
 *   parseCapabilityReplies('\x1b[?2026;2$y\x1b[?0u\x1b[?62;4c')
 *     // { modes: { 2026: 2 }, kitty: 0, version: undefined, da1: true, rest: '' }
 *
 * modes: mode number -> 0 unknown, 1 set, 2 reset, 3 always set, 4 always reset (DECRQM).
 * kitty: the keyboard flags now in force, undefined when the protocol did not answer.
 * version: name and version text of the terminal (XTVERSION). da1: the final answer arrived.
 * rest: str without the answers, what the user typed meanwhile. */
export function parseCapabilityReplies(str) {
  const modes = {};
  let kitty,
    version,
    da1 = false;

  const rest = String(str).replace(/\x1b\[\?(\d+);(\d)\$y|\x1b\[\?(\d+)u|\x1bP>\|([^\x1b]*)\x1b\\|\x1b\[\?[\d;]*c/g, (m, mode, state, flags, name) => {
    if(mode !== undefined) modes[mode] = +state;
    else if(flags !== undefined) kitty = +flags;
    else if(name !== undefined) version = name;
    else da1 = true;

    return '';
  });

  return { modes, kitty, version, da1, rest };
}

/* queryCapabilities(f, options): which of the features above the terminal behind f has
 *
 *   const caps = queryCapabilities(std.out)
 *   // { syncOutput: true, bracketedPaste: true, kittyKeyboard: true, truecolor: true, terminal: 'kitty(0.35)', ... }
 *   if(caps.syncOutput) screen = new Screen(std.out, { sync: true })
 *
 * it sends the questions, then reads the answers from the input until the last one came or timeout ms
 * passed. a terminal that answers nothing, or no terminal, gives false everywhere, so every feature stays
 * off. call it once at start-up, before anything else reads the input: what the user typed meanwhile is
 * returned as typeahead, and the answers are not seen by the line editor.
 *
 * options: input (fd of the input, 0), timeout (ms, 300), raw (put the input in raw mode and back, true:
 * pass false when it is in raw mode already), env (where COLORTERM is looked up, process.env).
 *
 * truecolor is read from COLORTERM, no terminal answers that one. */
export function queryCapabilities(f, { input = 0, timeout = 300, raw = true, env = globalThis.process?.env ?? {} } = {}) {
  const fd = typeof f == 'number' ? f : (f.fileno?.() ?? f.fd ?? 1);
  const caps = {
    syncOutput: false,
    graphemeClusters: false,
    bracketedPaste: false,
    focusEvents: false,
    sgrMouse: false,
    kittyKeyboard: false,
    kittyFlags: 0,
    truecolor: /truecolor|24bit/i.test(env.COLORTERM ?? '') || /-direct$/.test(env.TERM ?? ''),
    terminal: undefined,
    answered: false,
    typeahead: '',
  };

  if(!ttyGetWinSize(fd) || !ttyGetWinSize(input)) return caps;

  const decoder = new TextDecoder();
  const buf = new ArrayBuffer(256);
  const end = Date.now() + timeout;
  let text = '',
    got;

  if(raw) ttySetRaw(input);

  try {
    ttyWrite(f, PROBE);

    do {
      const n = ttyRead(input, buf, 0, buf.byteLength);

      if(n == -EAGAIN) sleep(5);
      else if(n <= 0) break;
      else text += decoder.decode(new Uint8Array(buf, 0, n), { stream: true });

      got = parseCapabilityReplies(text);
    } while(!got?.da1 && Date.now() < end);
  } finally {
    if(raw) ttySetRaw(input, true);
  }

  got = parseCapabilityReplies(text);

  const on = m => (got.modes[m] ?? 0) != 0;

  return Object.assign(caps, {
    syncOutput: on(2026),
    graphemeClusters: on(2027),
    bracketedPaste: on(2004),
    focusEvents: on(1004),
    sgrMouse: on(1006),
    kittyKeyboard: got.kitty !== undefined,
    kittyFlags: got.kitty ?? 0,
    terminal: got.version,
    answered: got.da1,
    typeahead: got.rest,
  });
}

/* enableCapabilities(f, caps, options): switches on what caps says the terminal has, returns a function
 * that switches it off again (call it at exit)
 *
 *   const restore = enableCapabilities(std.out, queryCapabilities(std.out), { kitty: true })
 *
 * options, each true to ask for it: paste (bracketed paste, on by default), graphemes (grapheme
 * clusters, on by default), focus (focus events), kitty (kitty keyboard, flags 1). the last two are off
 * by default, since the code reading the keys has to understand what they change.
 * synchronized output is not switched on here, it is per frame: new Screen(f, { sync: caps.syncOutput }) */
export function enableCapabilities(f, caps, { paste = true, graphemes = true, focus = false, kitty = false } = {}) {
  const undo = [];

  if(paste && caps.bracketedPaste) {
    bracketedPasteEnable(f);
    undo.push(() => bracketedPasteDisable(f));
  }

  if(graphemes && caps.graphemeClusters) {
    graphemeClustersEnable(f);
    undo.push(() => graphemeClustersDisable(f));
  }

  if(focus && caps.focusEvents) {
    focusEventsEnable(f);
    undo.push(() => focusEventsDisable(f));
  }

  if(kitty && caps.kittyKeyboard) {
    kittyKeyboardEnable(f);
    undo.push(() => kittyKeyboardDisable(f));
  }

  return () => {
    for(const f of undo.reverse()) f();
    undo.length = 0;
  };
}
