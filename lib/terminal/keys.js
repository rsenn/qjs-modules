/* terminal/keys.js: decodeKeys() and readKey(): input bytes to key objects.
 * depends on: terminal.js (ttyRead, EAGAIN), terminal/internal.js (sleep, TextDecoder).
 * rule: a key is { name, ctrl, meta, shift, ... } whatever the terminal sent: legacy, kitty or mouse. */

import { EAGAIN, ttyRead } from 'terminal/core';
import { TextDecoder, sleep } from 'terminal/internal';

const CSI_LETTER = { A: 'up', B: 'down', C: 'right', D: 'left', E: 'clear', F: 'end', H: 'home', P: 'f1', Q: 'f2', R: 'f3', S: 'f4', Z: 'tab' };

const CSI_TILDE = {
  1: 'home',
  2: 'insert',
  3: 'delete',
  4: 'end',
  5: 'pageup',
  6: 'pagedown',
  7: 'home',
  8: 'end',
  11: 'f1',
  12: 'f2',
  13: 'f3',
  14: 'f4',
  15: 'f5',
  17: 'f6',
  18: 'f7',
  19: 'f8',
  20: 'f9',
  21: 'f10',
  23: 'f11',
  24: 'f12',
};

function charKey(ch) {
  const key = { sequence: ch, name: undefined, ctrl: false, meta: false, shift: false };
  const code = ch.codePointAt(0);

  if(ch == '\r') key.name = 'return';
  else if(ch == '\n') key.name = 'enter';
  else if(ch == '\t') key.name = 'tab';
  else if(ch == '\b' || ch == '\x7f') key.name = 'backspace';
  else if(ch == '\x1b') key.name = 'escape';
  else if(ch == ' ') key.name = 'space';
  else if(code <= 0x1a) Object.assign(key, { name: String.fromCharCode(code + 96), ctrl: true });
  else if(ch >= 'A' && ch <= 'Z') Object.assign(key, { name: ch.toLowerCase(), shift: true });
  else key.name = ch;

  return key;
}

/* kitty keyboard protocol: key codes that are no letter, and the event types */
const KITTY_NAMES = { 9: 'tab', 13: 'return', 27: 'escape', 32: 'space', 127: 'backspace' };

const KITTY_EVENTS = ['press', 'repeat', 'release'];

const MOUSE_BUTTONS = ['left', 'middle', 'right', 'none'];

const WHEEL = ['wheelup', 'wheeldown', 'wheelleft', 'wheelright'];

/* key of one mouse report, b the button code of the X protocol */
function mouseKey(sequence, b, x, y, release) {
  const wheel = b & 64,
    motion = b & 32;
  const button = wheel ? WHEEL[b & 3] : MOUSE_BUTTONS[b & 3];
  const action = wheel ? 'wheel' : release || button == 'none' ? (motion ? 'move' : 'release') : motion ? 'drag' : 'press';

  return { s: sequence, key: { sequence, name: 'mouse', ctrl: !!(b & 16), meta: !!(b & 8), shift: !!(b & 4), mouse: { button, action, x, y } } };
}

/* the SGR (ESC [ < b ; x ; y M/m) or X10 (ESC [ M bxy) report at i, null when cut off */
function mouseAt(str, i) {
  if(str[i + 2] == 'M') {
    if(i + 6 > str.length) return null;

    const [b, x, y] = [3, 4, 5].map(k => str.charCodeAt(i + k) - 32);
    return mouseKey(str.slice(i, i + 6), b, x, y, false);
  }

  const m = /^\x1b\[<(\d+);(\d+);(\d+)([Mm])/.exec(str.slice(i, i + 32));

  if(!m) return /^\x1b\[<[\d;]*$/.test(str.slice(i)) ? null : { s: str.slice(i, i + 3), key: { sequence: str.slice(i, i + 3), name: undefined, ctrl: false, meta: false, shift: false } };

  return mouseKey(m[0], +m[1], +m[2], +m[3], m[4] == 'm');
}

/* decodeKeys(str, final): decode ANSI/CSI key sequences in a string into key objects.
 *
 * Returns { keys: Array<{s:string, key:{sequence,name,ctrl,meta,shift, ...mouse}}} ,
 *   rest:string } - any unfinished ESC sequence is in rest.
 *
 *   decodeKeys('a\x1b[1;5D').keys.map(k => k.key.name)   // ['a', 'left'], the second with ctrl
 *
 * final: when true, an ESC at the end of str is treated as the escape key,
 *   not the start of a sequence.
 *
 * Supported sequences:
 *   - Single characters, Ctrl-chars (^A..^Z), Enter, Tab, Backspace, Escape
 *   - CSI sequences: Up/Down/Left/Right, Home/End, PageUp/Down, Insert
 *   - CSI ? sequences: cursor movement, screen modes, mouse reporting
 *   - Kitty keyboard protocol: ESC [ code ; mods u
 *   - Bracketed paste: ESC [ 200~ text ESC [ 201~
 *   - Focus events: ESC [ I (focusin), ESC [ O (focusout)
 *   - Mouse reporting (X10 and SGR protocols)
 *   - Double-ESC => escape with meta flag
 *
 * legacy and kitty sequences yield the exact same {name, ctrl, meta, shift} flags.
 * the mapping matrix includes: Esc <=> \x1b[27u, Shift-Tab <=> \x1b[9;2u,
 *   Alt-Tab <=> \x1b[9;3u, Alt-1..4 <=> \x1b[49;3u..\x1b[52;3u,
 *   Alt-0 <=> \x1b[48;3u, Ctrl-letter <=> \x1b[<code>;5u,
 *   Enter <=> \x1b[13u, Backspace <=> \x1b[127u, etc.
 */
export function decodeKeys(str, final = false) {
  const keys = [];
  let i = 0;

  while(i < str.length) {
    let chCode = str.codePointAt(i);

    if(chCode != 0x1b) {
      /* not ESC: single character or surrogate pair */
      if(chCode >= 0xd800 && chCode <= 0xdfff) {
        /* surrogate pair: need 2 UTF-16 units */
        if(i + 1 >= str.length) {
          if(!final) return { keys, rest: str.slice(i) };
          chCode = 0xfffd;
        } else {
          const ch2Code = str.codePointAt(i + 1);
          if(ch2Code >= 0xdc00 && ch2Code <= 0xdfff) {
            i += 2;
            chCode = 0x10000 + ((chCode - 0xd800) << 10) + (ch2Code - 0xdc00);
          } else {
            /* orphan surrogate, treat as replacement char */
            i++;
            chCode = 0xfffd;
          }
        }
      } else {
        i += chCode >= 0x10000 ? 2 : 1;
      }

      keys.push({ s: String.fromCodePoint(chCode), key: charKey(String.fromCodePoint(chCode)) });
      continue;
    }

    /* chCode == 0x1b (ESC) */
    if(i + 1 >= str.length) {
      if(!final) return { keys, rest: str.slice(i) };
      keys.push({ s: '\x1b', key: charKey('\x1b') });
      break;
    }

    const nextCode = str.codePointAt(i + 1);

    if(nextCode == 0x5b && str.startsWith('\x1b[200~', i)) {
      /* bracketed paste: ESC [ 200~ text ESC [ 201~ */
      const end = str.indexOf('\x1b[201~', i + 6);

      if(end < 0) {
        if(!final) return { keys, rest: str.slice(i) };
        break;
      }

      const sequence = str.slice(i, end + 6);
      keys.push({ s: sequence, key: { sequence, name: 'paste', paste: str.slice(i + 6, end), ctrl: false, meta: false, shift: false } });
      i = end + 6;
      continue;
    }

    if(nextCode == 0x5b && (str[i + 2] == '<' || str[i + 2] == 'M')) {
      /* mouse: ESC [ < b ; x ; y M/m (SGR) or ESC [ M b x y (X10) */
      const mouse = mouseAt(str, i);

      if(!mouse) {
        if(!final) return { keys, rest: str.slice(i) };
        break;
      }

      keys.push(mouse);
      i += mouse.s.length;
      continue;
    }

    if(nextCode == 0x5b || nextCode == 0x4f) {
      /* CSI or OCSI sequence: ESC [ or ESC O */
      /* parameter bytes 0-?, then intermediate bytes space to /, then final byte */
      let j = i + 2;

      /* parse parameter bytes: 0x30..0x3f ('0'..'?') */
      while(j < str.length) {
        const c = str.codePointAt(j);
        if(c >= 0x30 && c <= 0x3f) {
          j++;
        } else break;
      }

      /* parse intermediate bytes: 0x20..0x2f (' '..'/') */
      while(j < str.length) {
        const c = str.codePointAt(j);
        if(c >= 0x20 && c <= 0x2f) {
          j++;
        } else break;
      }

      if(j >= str.length) {
        if(!final) return { keys, rest: str.slice(i) };
        break;
      }

      const finalCode = str.codePointAt(j);
      const body = str.slice(i + 2, j);
      const kitty = nextCode == 0x5b && finalCode == 0x75 && /^\d+[\d:;]*$/.test(body);

      if(kitty) {
        /* kitty keyboard: ESC [ code ; mods u */
        /* parse the body: code;mods */
        let codeStr = body,
          modsStr = '';
        const semicolon = body.indexOf(';');
        if(semicolon >= 0) {
          codeStr = body.slice(0, semicolon);
          modsStr = body.slice(semicolon + 1);
        }

        /* parse code: could have colons for modifier groups */
        let cpStr = codeStr;
        const colon = codeStr.indexOf(':');
        if(colon >= 0) {
          cpStr = codeStr.slice(0, colon);
        }
        const cp = cpStr ? parseInt(cpStr, 10) : 0;

        /* modifiers are 1 + a bit set: 1 shift, 2 alt, 4 ctrl. a second group after : is the event type */
        const mod = (modsStr ? parseInt(modsStr.split(':')[0], 10) || 1 : 1) - 1;
        const event = KITTY_EVENTS[parseInt(modsStr.split(':')[1], 10) - 1];
        const name = KITTY_NAMES[cp] ?? String.fromCodePoint(cp);

        keys.push({
          s: str.slice(i, j + 1),
          key: {
            sequence: str.slice(i, j + 1),
            name,
            ctrl: !!(mod & 4),
            meta: !!(mod & 2),
            shift: !!(mod & 1),
            ...(event ? { event } : {}),
          },
        });
        i = j + 1;
        continue;
      }

      /* check for focusin/focusout: ESC [ I or ESC [ O */
      if(nextCode == 0x5b && !body && (finalCode == 0x49 || finalCode == 0x4f)) {
        const sequence = str.slice(i, j + 1);

        keys.push({ s: sequence, key: { sequence, name: finalCode == 0x49 ? 'focusin' : 'focusout', ctrl: false, meta: false, shift: false } });
        i = j + 1;
        continue;
      }

      /* parse SGR/legacy CSI: final byte is ~, letter, or M */
      const params = body
        .split(';')
        .filter(p => p && p != '?')
        .map(Number);
      let name;

      if(finalCode == 0x7e) name = CSI_TILDE[params[0]];
      else name = CSI_LETTER[str[j]] || CSI_LETTER[str.slice(i + 2, j).charAt(0)];

      /* SGR mouse: ESC [ M b x y (urxvt 1015) or ESC [ < b;x;y M/m */
      if((finalCode == 0x4d || finalCode == 0x6d) && params.length >= 3) {
        keys.push(mouseKey(str.slice(i, j + 1), params[0] - 32, params[1], params[2], finalCode == 0x6d));
        i = j + 1;
        continue;
      }

      const mod = (params[1] ?? 1) - 1;
      const sequence = str.slice(i, j + 1);

      keys.push({ s: sequence, key: { sequence, name, ctrl: !!(mod & 4), meta: !!(mod & 2), shift: !!(mod & 1) || finalCode == 0x5a } });
      i = j + 1;
      continue;
    }

    /* ESC followed by a non-CSI/non-kitty sequence: ESC + one character */
    /* This handles: ESC c, ESC 7, etc. - treated as escape with meta */
    const innerCode = str.codePointAt(i + 1);
    const innerCh = String.fromCodePoint(innerCode);
    const key = charKey(innerCh);

    key.meta = true;
    key.sequence = '\x1b' + innerCh;
    keys.push({ s: key.sequence, key });
    i += 1 + (innerCode >= 0x10000 ? 2 : 1);
  }

  return { keys, rest: '' };
}

/* per fd: keys read but not returned yet, and the decoder of a split character */
const keyQueues = new Map();

const KEY_TYPES = {
  up: 'up',
  down: 'down',
  left: 'left',
  right: 'right',
  pageup: 'pageup',
  pagedown: 'pagedown',
  space: 'space',
  return: 'enter',
  enter: 'enter',
  backspace: 'backspace',
  escape: 'escape',
};

/* readKey(fd): one keypress from fd in raw mode, waits until there is one
 *
 *   readKey(0)   // { type: 'up' }, { type: 'char', char: 'ä' }, { type: 'ctrlc' }
 *
 * type is up down left right pageup pagedown space enter backspace ctrlc
 * escape (also any other sequence), char, mouse, or eof. a mouse key has
 * button, action, x, y (1-based) and ctrl, meta, shift, see mouseEnable(). */
export function readKey(fd) {
  let q = keyQueues.get(fd);

  if(!q) keyQueues.set(fd, (q = { keys: [], decoder: new TextDecoder(), buf: new ArrayBuffer(64) }));

  while(!q.keys.length) {
    const n = ttyRead(fd, q.buf, 0, q.buf.byteLength);

    if(n == -EAGAIN) {
      sleep(10);
      continue;
    }

    if(n <= 0) return { type: 'eof' };

    q.keys.push(...decodeKeys(q.decoder.decode(new Uint8Array(q.buf, 0, n), { stream: true }), true).keys);
  }

  const { s, key } = q.keys.shift();

  if(key.ctrl && key.name == 'c') return { type: 'ctrlc' };
  if(key.name == 'mouse') return { type: 'mouse', ...key.mouse, ctrl: key.ctrl, meta: key.meta, shift: key.shift };
  if(s.length > 1 && s[0] == '\x1b') return { type: KEY_TYPES[key.name] ?? 'escape' };
  return KEY_TYPES[key.name] ? { type: KEY_TYPES[key.name] } : { type: 'char', char: s };
}

/* ---- capabilities: ask the terminal what it can do ---- */
