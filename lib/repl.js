/*
 * QuickJS Read Eval Print Loop
 *
 * Copyright (c) 2017-2020 Fabrice Bellard
 * Copyright (c) 2017-2020 Charlie Gordon
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
import { isatty, platform, SIGINT, signal, SIGTERM } from 'os';
import { basename, extname, gethome } from 'path';
import { isArray, stripAnsi, types, unique } from 'util';
import inspect from 'inspect';
import { setReadHandler } from 'io';
import { cursorHorizontalAbsolute, cursorPosition, EAGAIN, eraseInDisplay, mousetrackingDisable, mousetrackingEnable, readKey, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } from 'terminal';
import { evalScript, exit, gc, in as stdin, loadFile, loadScript, open as fopenSync, out as stdout, getenv } from 'std';

const readBufSize = 512;

/* XXX: use preprocessor ? */
let configNumcalc = false;
const hasJscalc = typeof Fraction === 'function';
const hasBignum = typeof BigFloat === 'function';
/* stdout is not a tty: line mode, no escapes, no colors, a script drives it */
const pipedOutput = !isatty(stdout.fileno());
const supportsAnsi = !platform.startsWith('win') && !pipedOutput;

const colors = supportsAnsi
  ? {
      none: '\x1b[0m',
      black: '\x1b[30m',
      red: '\x1b[31m',
      green: '\x1b[32m',
      yellow: '\x1b[33m',
      blue: '\x1b[34m',
      magenta: '\x1b[35m',
      cyan: '\x1b[36m',
      white: '\x1b[37m',
      gray: '\x1b[30;1m',
      grey: '\x1b[30;1m',
      brightRed: '\x1b[31;1m',
      brightGreen: '\x1b[32;1m',
      brightYellow: '\x1b[33;1m',
      brightBlue: '\x1b[34;1m',
      brightMagenta: '\x1b[35;1m',
      brightCyan: '\x1b[36;1m',
      brightWhite: '\x1b[37;1m',
      errorBg: '\x1b[37;41m',
      underline: '\x1b[4m',
      inverse: '\x1b[7m',
    }
  : {
      none: '',
      black: '',
      red: '',
      green: '',
      yellow: '',
      blue: '',
      magenta: '',
      cyan: '',
      white: '',
      gray: '',
      grey: '',
      brightRed: '',
      brightGreen: '',
      brightYellow: '',
      brightBlue: '',
      brightMagenta: '',
      brightCyan: '',
      brightWhite: '',
      errorBg: '',
      underline: '',
      inverse: '',
    };

let readlineKeys = null;
let readlineState = null;

let hexMode = false;
let evalMode = 'std';

const styles = {
  default: 'brightGreen',
  comment: 'brightGreen',
  string: 'brightCyan',
  regex: 'brightMagenta',
  number: 'green',
  keyword: 'brightRed',
  function: 'brightYellow',
  type: 'brightRed',
  identifier: 'brightYellow',
  error: 'red',
  mismatch: 'errorBg',
  result: 'white',
  errorMsg: 'brightRed',
};

let clipBoard = '';
let prec = null;
let expBits = null;
let log2_10 = null;

let utf8_state = 0;
let utf8_val = 0;

let termFd = null;

/* current X position of the cursor in the terminal */
let termCursorX = 0;

const setRaw = once(fd => ttySetRaw(fd));
/* stdout output, translated for a Win32 console without VT */
const puts = s => ttyWrite(stdout, s);
const putBanner = once(() => puts('QuickJS - Type "\\h" for help\n'));

let searchState = 0;

function colorHTML(s) {
  return s.replace(/#([0-9a-fA-F]{6}|[0-9a-fA-F]{3})\b/g, (match, str) => {
    if(str.length === 3) str = str.split('').reduce((s, c) => s + c + c, '');

    const [r, g, b] = [...str.matchAll(/../gm)].map(m => +('0x' + m[0]));

    /* standard perceived luminance formula (ITU-R BT.601) */
    const luminance = 0.299 * r + 0.587 * g + 0.114 * b;

    /* choose white text for dark backgrounds, black text for light backgrounds */
    const fg = luminance < 128 ? '\x1b[38;2;255;255;255m' /* White foreground */ : '\x1b[38;2;0;0;0m'; /* Black foreground */

    /* \x1b[48;2;...m sets the truecolor background */
    return `${fg}\x1b[48;2;${r};${g};${b}m${match}\x1b[0m`;
  });
}

export class REPL {
  pstate = '';
  prompt = '';
  plen = 0;
  ps1 = '> ';
  ps2 = '  ... ';
  utf8 = true;
  showTime = false;
  showColors = !pipedOutput;
  piped = pipedOutput;
  pipeLine = '';
  pipeQueue = [];
  pipeBusy = false;
  pipeDraining = false;
  pipeDone = false;
  promptShown = false;
  eof = false;
  evalStartTime = undefined;
  evalTime = 0;
  renderRow = 0; /* terminal row of the cursor, counted from the prompt row */
  editorMode = false; /* \editor: Enter inserts a newline, ^D evaluates */
  pasting = null; /* bracketed paste: the text so far, null outside a paste */
  lastResult = undefined; /* what _ reads */
  lastError = undefined; /* what _error reads */

  mexpr = '';
  level = 0;
  cmd = '';
  cursorPos = 0;
  lastCmd = '';
  lastCursorPos = 0;
  history = [];
  historyIndex = null;
  thisFun = null;
  lastFun = null;
  quoteFlag = false;
  out = stdout;
  readBuf = new Uint8Array(readBufSize);
  readlineCallback = null;
  searchIndex = -1;
  searchPattern = '';
  searchMatches = [];

  cleanupHandlers = new Set();
  modules = new Set();

  debug = /\brepl\b/gi.test(getenv('DEBUG')) ? (...args) => console.log('REPL', ...args) : () => {};

  constructor(name, showBanner = true) {
    let commands = {
      /* command table */
      '\x01': this.beginningOfLine /* ^A - bol */,
      '\x02': this.backwardChar /* ^B - backward-char */,
      '\x03': this.controlC /* ^C - this.abort */,
      '\x04': this.controlD /* ^D - delete-char or exit */,
      '\x05': this.endOfLine /* ^E - eol */,
      '\x06': this.forwardChar /* ^F - forward-char */,
      '\x07': this.abort /* ^G - bell */,
      '\x08': this.backwardDeleteChar /* ^H - backspace */,
      '\x09': this.completion /* ^I - history-search-backward */,
      '\x0a': this.acceptLine /* ^J - newline */,
      '\x0b': this.killLine /* ^K - delete to end of line */,
      '\x0d': this.acceptLine /* ^M - enter */,
      '\x0c': this.controlL /* ^L - clear screen */,
      '\x0e': this.historyNext /* ^N - down */,
      '\x10': this.historyPrevious /* ^P - up */,
      '\x11': this.quotedInsert /* ^Q - quoted-this.insert */,
      '\x12': this.reverseSearch /* ^R - reverse-search */,
      '\x13': this.forwardSearch /* ^S - search */,
      '\x14': this.transposeChars /* ^T - transpose */,
      '\x15': this.deleteAll /* ^U - delete all */,
      '\x18': this.reset /* ^X - cancel */,
      '\x19': this.yank /* ^Y - this.yank */,
      '\x1b': this.escape,
      '\x1bOA': this.historyPrevious /* ^[OA - up */,
      '\x1bOB': this.historyNext /* ^[OB - down */,
      '\x1bOC': this.forwardChar /* ^[OC - right */,
      '\x1bOD': this.backwardChar /* ^[OD - left */,
      '\x1bOF': this.forwardWord /* ^[OF - ctrl-right */,
      '\x1bOH': this.backwardWord /* ^[OH - ctrl-left */,
      '\x1b[200~': this.pasteStart /* ^[[200~ - bracketed paste begins */,
      '\x1b[1;5C': this.forwardWord /* ^[[1;5C - ctrl-right */,
      '\x1b[1;5D': this.backwardWord /* ^[[1;5D - ctrl-left */,
      '\x1b[1~': this.beginningOfLine /* ^[[1~ - bol */,
      '\x1b[3~': this.deleteChar /* ^[[3~ - delete */,
      '\x1b[4~': this.endOfLine /* ^[[4~ - eol */,
      '\x1b[5~': this.historySearchBackward /* ^[[5~ - page up */,
      '\x1b[6~': this.historySearchForward /* ^[[5~ - page down */,
      '\x1b[A': this.historyPrevious /* ^[[A - up */,
      '\x1b[B': this.historyNext /* ^[[B - down */,
      '\x1b[C': this.forwardChar /* ^[[C - right */,
      '\x1b[D': this.backwardChar /* ^[[D - left */,
      '\x1b[F': this.endOfLine /* ^[[F - end */,
      '\x1b[H': this.beginningOfLine /* ^[[H - home */,
      '\x1b\x08': this.backwardKillMismatch /* M-C-h - kill from a mismatch, or a word */,
      '\x1b\x7f': this.backwardKillMismatch /* M-C-? - kill from a mismatch, or a word */,
      '\x1bb': this.backwardWord /* M-b - this.backwardWord */,
      '\x1bd': this.killWord /* M-d - this.killWord */,
      '\x1bf': this.forwardWord /* M-f - this.backwardWord */,
      '\x1bk': this.backwardKillLine /* M-k - this.backwardKillLine */,
      '\x1bl': this.downcaseWord /* M-l - this.downcaseWord */,
      '\x1bt': this.transposeWords /* M-t - this.transposeWords */,
      '\x1bu': this.upcaseWord /* M-u - this.upcaseWord */,
      '\x7f': this.backwardDeleteChar /* ^? - delete */,
    };

    this.commands = commands;
    this.directives = { ...this.directives };

    if(typeof name == 'string') this.ps1 = name + this.ps1;

    if(globalThis.console) this.inspectOptions ??= globalThis.console.options;

    this.termInit();

    signal(SIGTERM, () => this.exit(0));

    if(loadModule) this.modulesLoad();
    this.showBanner = showBanner;
  }

  loadSaveOptions(filename = this.makeFilename('config')) {
    loadSaveObject(this, 'inspectOptions', filename);
  }

  saveOptions(filename = this.makeFilename('config')) {
    return saveFile(
      filename,
      '(' +
        inspect(this.inspectOptions, {
          colors: false,
          reparseable: true,
          compact: false,
          maxArrayLength: Infinity,
          maxStringLength: Infinity,
        }) +
        ')\n',
    );
  }

  addCleanupHandler(fn) {
    this.cleanupHandlers.add(fn);
  }

  exit(exitCode = 0) {
    for(const handler of this.cleanupHandlers) handler.call(this);

    this.running = false;
    exit(exitCode);
  }

  abort() {
    this.cmd = '';
    this.cursorPos = 0;
    return -2;
  }

  /* Enter: a newline while the input is incomplete, else submit it */
  acceptLine() {
    if(!this.piped && !searchState) {
      const indent = this.editorMode ? '' : this.continuation();

      if(indent !== null) return void this.insert('\n' + indent);

      this.cursorPos = this.cmd.length;
      this.render(false);
    }

    puts('\n');
    termCursorX = 0;
    this.renderRow = 0;
    return -1;
  }

  /* the indentation of a new line, or null when Enter submits.
   *
   * an unclosed bracket, template or block comment continues, a bracket
   * mismatch or an open quote never completes and submits: '"abc', 'f(]'
   */
  continuation() {
    const [state, , , { mismatch }] = this.colorizeJs(this.cmd);

    if(!state || mismatch >= 0 || /['"]/.test(state)) return null;

    const [before] = this.colorizeJs(this.cmd.slice(0, this.cursorPos));

    return /[`/]$/.test(before) ? '' : '  '.repeat(before.replace(/[^([{$]/g, '').length);
  }

  alert() {}

  backwardChar() {
    if(this.cursorPos > 0) {
      this.cursorPos--;

      while(isTrailingSurrogate(this.cmd.charAt(this.cursorPos))) this.cursorPos--;
    }
  }

  backwardDeleteChar() {
    this.deleteCharDir(-1);
  }

  backwardKillLine() {
    this.killRegion(this.lineStart(), this.cursorPos, -1);
  }

  /* with a bracket mismatch kills from it to the end, else a word back */
  backwardKillMismatch() {
    const { mismatch } = this.colorizeJs(this.cmd)[3];

    if(mismatch < 0) return this.backwardKillWord();

    this.killRegion(mismatch, this.cmd.length, 1);
  }

  backwardKillWord() {
    this.killRegion(this.skipWordBackward(this.cursorPos), this.cursorPos, -1);
  }

  backwardWord() {
    this.cursorPos = this.skipWordBackward(this.cursorPos);
  }

  beginningOfLine() {
    this.cursorPos = this.lineStart();
  }

  /* start and end of the buffer line holding pos: 'ab\ncd', 4 -> 3, 5 */
  lineStart(pos = this.cursorPos) {
    return pos > 0 ? this.cmd.lastIndexOf('\n', pos - 1) + 1 : 0;
  }

  lineEnd(pos = this.cursorPos) {
    const end = this.cmd.indexOf('\n', pos);

    return end < 0 ? this.cmd.length : end;
  }

  /* moves the cursor one buffer line up or down at the same column,
   * false on the first or last line
   */
  lineMove(dir) {
    const start = this.lineStart(),
      col = this.cursorPos - start;
    let target;

    if(dir < 0) {
      if(start == 0) return false;
      target = this.lineStart(start - 1);
    } else {
      const end = this.lineEnd();

      if(end == this.cmd.length) return false;
      target = end + 1;
    }

    this.cursorPos = Math.min(target + col, this.lineEnd(target));
    return true;
  }

  bigfloatToString(a, radix) {
    let s = null;

    if(!BigFloat.isFinite(a)) {
      /* NaN, Infinite */
      if(evalMode !== 'math') return 'BigFloat(' + a.toString() + ')';
      else return a.toString();
    } else {
      if(a == 0) {
        if(1 / a < 0) s = '-0';
        else s = '0';
      } else {
        if(radix == 16) {
          let s = null;

          if(a < 0) {
            a = -a;
            s = '-';
          } else {
            s = '';
          }

          s += '0x' + a.toString(16);
        } else {
          s = a.toString();
        }
      }

      if(typeof a === 'bigfloat' && evalMode !== 'math') {
        s += 'l';
      } else if(evalMode !== 'std' && s.indexOf('.') < 0 && ((radix == 16 && s.indexOf('p') < 0) || (radix == 10 && s.indexOf('e') < 0))) {
        /* add a decimal point so that the floating point type is visible */
        s += '.0';
      }

      return s;
    }
  }

  bigintToString(a, radix) {
    let s = null;

    if(radix == 16) {
      let s = null;

      if(a < 0) {
        a = -a;
        s = '-';
      } else {
        s = '';
      }

      s += '0x' + a.toString(16);
    } else {
      s = a.toString();
    }

    if(evalMode === 'std') s += 'n';
    return s;
  }

  cmdReadlineStart() {
    this.readlineStart(this.dupstr('    ', this.level), this.readlineHandleCmd);
  }

  cmdStart(showBanner = true) {
    if(showBanner) putBanner();

    this.defineLastValues();

    if(hasBignum) {
      log2_10 = Math.log(10) / Math.log(2);
      prec = 113;
      expBits = 15;

      if(hasJscalc) {
        evalMode = 'math';

        /* XXX: numeric mode should always be the default ? */
        globalThis.algebraicMode = configNumcalc;
      }
    }

    this.cmdReadlineStart();
  }

  /* tokenizes str for highlighting: [state, level, styles, brackets].
   *
   *   state     the open brackets, quotes and comments at the end: '(`$'
   *   level     the number of open brackets
   *   styles    a style name per UTF-16 unit
   *   brackets  { mismatch, enclosing, match }, offsets or -1
   *
   * mismatch is the first closer without its opener, enclosing the
   * innermost opener still open at cursor, match the opener of a closer
   * right before cursor.
   */
  colorizeJs(str, cursor = -1) {
    let i,
      c,
      start,
      n = str.length,
      style,
      state = '',
      level = 0,
      primary,
      canRegex = 1,
      r = [],
      openPos = [],
      mismatch = -1,
      enclosing = -1,
      match = -1;

    /* pos: where the opener starts, kept in openPos next to state */
    function pushState(c, pos = start) {
      state += c;
      openPos.push(pos);
    }

    function lastState(c) {
      return state.substring(state.length - 1);
    }

    function innermostOpen() {
      for(let k = state.length - 1; k >= 0; k--) if('([{$'.includes(state[k])) return openPos[k];

      return -1;
    }

    function popState(c) {
      c = lastState();
      state = state.substring(0, state.length - 1);
      openPos.pop();
      return c;
    }

    function parseBlockComment() {
      style = 'comment';
      pushState('/');

      for(i++; i < n - 1; i++) {
        if(str[i] == '*' && str[i + 1] == '/') {
          i += 2;
          popState('/');
          break;
        }
      }
    }

    function parseLineComment() {
      style = 'comment';

      for(i++; i < n; i++) if(str[i] == '\n') break;
    }

    /* a template stops at ${ with state '$', the matching } resumes it */
    function parseString(delim, resume = false) {
      style = 'string';
      if(!resume) pushState(delim);

      while(i < n) {
        if((c = str[i++]) == '\n' && delim != '`') {
          style = 'error';
          continue;
        }

        if(c == '\\') {
          if(i >= n) break;
          i++;
        } else if(c == delim) {
          popState();
          break;
        } else if(delim == '`' && c == '$' && str[i] == '{') {
          i++;
          level++;
          pushState('$', i - 2);
          break;
        }
      }

      canRegex = lastState() == '$' ? 1 : 0;
    }

    function parseRegex() {
      style = 'regex';
      pushState('/');

      while(i < n) {
        if((c = str[i++]) == '\n') {
          style = 'error';
          continue;
        }

        if(c == '\\') {
          if(i < n) i++;

          continue;
        }

        if(lastState() == '[') {
          if(c == ']') popState();

          // ECMA 5: ignore '/' inside char classes
          continue;
        }

        if(c == '[') {
          pushState('[', i - 1);
          if(str[i] == '[' || str[i] == ']') i++;
          continue;
        }

        if(c == '/') {
          popState();
          while(i < n && isWord(str[i])) i++;
          break;
        }
      }
    }

    function parseNumber() {
      style = 'number';

      while(i < n && (isWord(str[i]) || (str[i] == '.' && (i == n - 1 || str[i + 1] != '.')))) i++;
    }

    const jsKeywords =
      '|' +
      'break|case|catch|continue|debugger|default|delete|do|' +
      'else|finally|for|function|if|in|instanceof|new|' +
      'return|switch|this|throw|try|typeof|while|with|' +
      'class|const|enum|import|export|extends|super|' +
      'implements|interface|let|package|private|protected|' +
      'public|static|yield|' +
      'undefined|null|true|false|Infinity|NaN|' +
      'eval|arguments|' +
      'await|';

    const jsNoRegex = '|this|super|undefined|null|true|false|Infinity|NaN|arguments|',
      jsTypes = '|void|var|';

    function parseIdentifier() {
      canRegex = 1;

      while(i < n && isWord(str[i])) i++;

      const w = '|' + str.substring(start, i) + '|';

      if(jsKeywords.indexOf(w) >= 0) {
        style = 'keyword';

        if(jsNoRegex.indexOf(w) >= 0) canRegex = 0;

        return;
      }

      let i1 = i;
      while(i1 < n && str[i1] == ' ') i1++;

      if(i1 < n && str[i1] == '(') {
        style = 'function';
        return;
      }

      if(jsTypes.indexOf(w) >= 0) {
        style = 'type';
        return;
      }

      style = 'identifier';
      canRegex = 0;
    }

    function setStyle(from, to) {
      while(r.length < from) r.push('default');
      while(r.length < to) r.push(style);
    }

    for(i = 0; i < n;) {
      style = null;
      start = i;

      if(enclosing < 0 && cursor >= 0 && i >= cursor) {
        enclosing = innermostOpen();
        cursor = -1;
      }

      switch ((c = str[i++])) {
        case ' ':
        case '\t':
        case '\r':
        case '\n':
          continue;

        case '+':
        case '-':
          if(i < n && str[i] == c) {
            i++;
            continue;
          }

          canRegex = 1;
          continue;

        case '/':
          if(i < n && str[i] == '*') {
            // block comment
            parseBlockComment();
            break;
          }

          if(i < n && str[i] == '/') {
            // line comment
            parseLineComment();
            break;
          }

          if(canRegex) {
            parseRegex();
            canRegex = 0;
            break;
          }

          canRegex = 1;
          continue;

        case "'":
        case '"':
        case '`':
          parseString(c);
          break;

        case '(':
        case '[':
        case '{':
          canRegex = 1;
          level++;
          pushState(c);
          continue;

        case ')':
        case ']':
        case '}':
          canRegex = 0;

          if(level > 0 && isBalanced(lastState(), c)) {
            level--;

            if(start == cursor - 1) match = openPos[openPos.length - 1];

            if(popState() == '$') {
              parseString('`', true);
              break;
            }

            continue;
          }

          if(mismatch < 0) mismatch = start;

          style = 'error';
          break;

        default:
          if(isDigit(c)) {
            parseNumber();
            canRegex = 0;
            break;
          }

          if(isWord(c) || c == '$') {
            parseIdentifier();
            break;
          }

          canRegex = 1;
          continue;
      }

      if(style) setStyle(start, i);
    }

    setStyle(n, n);

    if(cursor >= 0) enclosing = innermostOpen();

    return [state, level, r, { mismatch, enclosing, match }];
  }

  /* adds the bracket highlights to the styles of colorizeJs.
   *
   *   mismatch   'mismatch' from the first mismatch to the end
   *   enclosing  '+u', underline on the innermost open opener
   *   match      '+r', inverse on a closer before the cursor and its opener
   */
  bracketStyles(styles, { mismatch, enclosing, match }, cursor) {
    const r = [...styles];

    if(mismatch >= 0) {
      for(let k = mismatch; k < r.length; k++) r[k] = 'mismatch';

      return r;
    }

    if(enclosing >= 0) r[enclosing] += '+u';

    if(match >= 0) {
      r[match] += '+r';
      r[cursor - 1] += '+r';
    }

    return r;
  }

  completion() {
    const res = this.getCompletions(this.cmd, this.cursorPos);
    const { tab } = res;

    if(tab.length === 0) return;

    const s = tab[0];
    let len = s.length;

    /* add the chars which are identical in all the completions */
    for(let i = 1; i < tab.length; i++) {
      const t = tab[i];

      for(let j = 0; j < len; j++) {
        if(t[j] !== s[j]) {
          len = j;
          break;
        }
      }
    }

    for(let i = res.pos; i < len; i++) this.insert(s[i]);

    if(this.lastFun === this.completion && tab.length == 1) {
      /* append parentheses to function names */
      const m = res.ctx[tab[0]];

      if(typeof m == 'function') {
        this.insert('(');
        if(m.length == 0) this.insert(')');
      } else if(typeof m == 'object') {
        this.insert('.');
      }
    }

    /* show the possible completions */
    if(this.lastFun === this.completion && tab.length >= 2) {
      this.showCompletions(tab);

      /* show a new this.prompt */
      this.readlinePrintPrompt();
    }
  }

  showCompletions(tab) {
    let maxWidth = 0;

    for(let i = 0; i < tab.length; i++) maxWidth = Math.max(maxWidth, tab[i].length);

    maxWidth += 2;

    const nCols = Math.max(1, Math.floor((this.columns + 1) / maxWidth));
    const nRows = Math.ceil(tab.length / nCols);

    this.moveBelow();
    stdout.flush();

    /* display the sorted list column-wise */
    for(let row = 0; row < nRows; row++) {
      for(let col = 0; col < nCols; col++) {
        let i = col * nRows + row;

        if(i >= tab.length) break;

        let s = tab[i];

        if(col != nCols - 1) s = s.padEnd(maxWidth);

        puts(s);
      }

      puts('\n');
    }
  }

  /* moves the terminal cursor to a new line below the edit buffer */
  moveBelow() {
    const pos = this.cursorPos;

    this.cursorPos = this.cmd.length;
    if(!this.piped) this.render(false);
    this.cursorPos = pos;

    puts('\n');
    termCursorX = 0;
    this.renderRow = 0;
  }

  /* ^C: drops the input, on an empty line tells how to exit, a second ^C
   * there exits
   */
  controlC() {
    if(this.cmd != '' || this.mexpr != '' || this.editorMode) {
      this.moveBelow();
      this.cmd = this.mexpr = this.pstate = '';
      this.cursorPos = this.level = 0;
      this.editorMode = false;
      this.thisFun = null;
      this.cmdReadlineStart();
    } else if(this.lastFun === this.controlC) {
      puts('\n');

      this.exit(0);
      return -3;
    } else {
      puts('\n(To exit, press Ctrl+C again or Ctrl+D or type \\q)\n');
      termCursorX = 0;
      this.renderRow = 0;
      this.readlinePrintPrompt();
    }
  }

  controlD() {
    if(this.editorMode) {
      this.editorMode = false;
      this.moveBelow();
      return -1;
    }

    if(this.cmd.length == 0) {
      puts('\n');
      return -3; /* exit read eval this.print loop */
    } else {
      this.deleteCharDir(1);
    }
  }

  /* clears the screen, then prints the prompt and the line again */
  controlL() {
    eraseInDisplay(stdout, 2);
    cursorPosition(stdout, 1, 1);
    termCursorX = 0;
    this.renderRow = 0;
    this.readlinePrintPrompt();
  }

  deleteChar() {
    this.deleteCharDir(1);
  }

  deleteCharDir(dir) {
    let start = this.cursorPos;

    if(dir < 0) {
      start--;
      while(isTrailingSurrogate(this.cmd.charAt(start))) start--;
    }

    let end = start + 1;

    while(isTrailingSurrogate(this.cmd.charAt(end))) end++;

    if(start >= 0 && start < this.cmd.length) {
      if(this.lastFun === this.killRegion) {
        this.killRegion(start, end, dir);
      } else {
        this.cmd = this.cmd.substring(0, start) + this.cmd.substring(end);
        this.cursorPos = start;
      }
    }
  }

  deleteAll() {
    this.killRegion(0, this.cmd.length, 1);
  }

  downcaseWord() {
    const end = this.skipWordForward(this.cursorPos);
    this.cmd = this.cmd.substring(0, this.cursorPos) + this.cmd.substring(this.cursorPos, end).toLowerCase() + this.cmd.substring(end);
  }

  dupstr(str, count) {
    let res = '';
    while(count-- > 0) res += str;
    return res;
  }

  endOfLine() {
    this.cursorPos = this.lineEnd();
  }

  /* erases the prompt and every row of the edit buffer */
  readlineRemovePrompt() {
    if(this.piped) return void (this.promptShown = false);

    puts((this.renderRow > 0 ? `\x1b[${this.renderRow}A` : '') + '\r\x1b[J');

    termCursorX = 0;
    this.renderRow = 0;
  }

  printResult(result) {
    if(result !== this['$']) this.resultId = (this.resultId | 0) + 1;

    if(types.isPromise(result)) this.printPromise(result);
    else this.printStatus([colors[styles.result], result, colors.none]);

    if(typeof result == 'unknown') result = undefined;

    this['$'] = result;
    this.lastResult = result;
  }

  printPromise(result, fmt = id => `${colors.gray}<${colors.red}result${colors.gray}#${colors.cyan}${id}${colors.gray}>${colors.none}`) {
    const id = this.resultId;
    const banner = fmt(id);

    if(result !== this['$'])
      result
        .then(value => {
          this._ = value;
          this.lastResult = value;
          this.printStatus([banner, ` resolved to:\n`, value]);
        })
        .catch(value => {
          this['!'] = value;
          this.printStatus([banner, ` rejected (`, value, `)`]);
        });

    this.printStatus(() => console.log(`${banner} -> pending Promise`));
  }

  extractDirective(a) {
    let pos;

    if(a[0] !== '\\') return '';

    for(pos = 1; pos < a.length; pos++) if(!isAlpha(a[pos])) break;

    return a.substring(1, pos);
  }

  forwardChar() {
    if(this.cursorPos < this.cmd.length) {
      this.cursorPos++;
      while(isTrailingSurrogate(this.cmd.charAt(this.cursorPos))) this.cursorPos++;
    }
  }

  forwardWord() {
    this.cursorPos = this.skipWordForward(this.cursorPos);
  }

  getCompletions(line, pos) {
    if(line.startsWith('\\')) {
      //console.log('getCompletions', { line,pos});
      let directive;

      if((directive = this.getDirective(line.slice(1, line.indexOf(' '))))) {
        let [fn, help, complete] = directive;

        //console.log('getCompletions', { fn, help, complete });
        return (typeof complete == 'function' && complete(line, pos)) || { tab: [], pos: 0 };
      }
    }

    let obj,
      j,
      paren = null;

    const s = this.getContextWord(line, pos);
    const ctxObj = this.getContextObject(line, pos - s.length);
    let r = [];

    /* enumerate properties from object and its prototype chain,
       add non-numeric regular properties with s as e prefix */
    for(let i = 0, obj = ctxObj; i < 10 && obj !== null && obj !== void 0; i++) {
      try {
        const props = Object.getOwnPropertyNames(obj);

        /* add non-numeric regular properties */
        for(j = 0; j < props.length; j++) {
          const prop = props[j];
          if(typeof prop == 'string' && '' + +prop != prop && prop.startsWith(s)) r.push(prop);
        }
      } catch(e) {}

      obj = Object.getPrototypeOf(obj);
    }

    if(0)
      if(r.length > 1) {
        /* sort list with internal names last and remove duplicates */
        function symcmp(a, b) {
          if(a[0] != b[0]) {
            if(a[0] == '_') return 1;
            if(b[0] == '_') return -1;
          }

          if(a < b) return -1;
          if(a > b) return +1;
          return 0;
        }

        r.sort(this.symcmp);

        for(let i = (j = 1); i < r.length; i++) if(r[i] != r[i - 1]) r[j++] = r[i];

        r.length = j;
      }

    r = unique(r);

    /* 'tab' = list of completions, 'pos' = cursor position inside
       the completions */
    return { tab: r, pos: s.length, ctx: ctxObj };
  }

  getContextObject(line, pos) {
    try {
      let obj,
        base,
        c = null;

      if(pos <= 0 || ' ~!%^&*(-+={[|:;,<>?/'.indexOf(line[pos - 1]) >= 0) return globalThis;
      if(pos >= 2 && line[pos - 1] === '.') {
        pos--;
        obj = {};

        switch ((c = line[pos - 1])) {
          case "'":
          case '"':
            return 'a';

          case ']':
            return [];

          case '}':
            return {};

          case '/':
            return / /;

          default:
            if(isWord(c)) {
              base = this.getContextWord(line, pos);
              if(['true', 'false', 'null', 'this'].includes(base) || !isNaN(+base)) return eval(base);
              obj = this.getContextObject(line, pos - base.length);
              if(obj === null || obj === void 0) return obj;
              if(obj === globalThis && obj[base] === void 0) return eval(base);
              else return obj[base];
            }

            return {};
        }
      }
    } catch(e) {}

    return void 0;
  }

  getContextWord(line, pos) {
    let s = '';

    while(pos > 0 && isWord(line[pos - 1])) {
      pos--;
      s = line[pos] + s;
    }

    return s;
  }

  handleByte(c) {
    if(!this.utf8) {
      this.handleChar(c);
    } else if(utf8_state !== 0 && c >= 0x80 && c < 0xc0) {
      utf8_val = (utf8_val << 6) | (c & 0x3f);
      utf8_state--;

      if(utf8_state === 0) this.handleChar(utf8_val);
    } else if(c >= 0xc0 && c < 0xf8) {
      utf8_state = 1 + (c >= 0xe0) + (c >= 0xf0);
      utf8_val = c & ((1 << (6 - utf8_state)) - 1);
    } else {
      utf8_state = 0;
      this.handleChar(c);
    }
  }

  handleChar(c1) {
    if(this.piped) return this.pipeChar(c1);

    const c = String.fromCodePoint(c1);

    if(this.pasting !== null) return this.pasteChar(c);

    for(;;) {
      switch (readlineState) {
        case 0:
          if(c == '\x1b') {
            /* '^[' - ESC */
            readlineKeys = c;
            readlineState = 1;
          } else {
            this.handleKey(c);
          }

          break;

        case 1 /* '^[ */:
          readlineKeys += c;

          if(c == '[') {
            readlineState = 2;
          } else if(c == 'O') {
            readlineState = 3;
          } else {
            this.handleKey(readlineKeys);
            readlineState = 0;
          }

          break;

        case 2 /* '^[[' - CSI */:
          readlineKeys += c;

          if(c == '<') {
            readlineState = 4;
          } else if(!(c == ';' || (c >= '0' && c <= '9'))) {
            this.handleKey(readlineKeys);
            readlineState = 0;
          }

          break;

        case 3 /* '^[O' - ESC2 */:
          readlineKeys += c;
          this.handleKey(readlineKeys);
          readlineState = 0;
          break;

        case 4:
          if(!(c == ';' || (c >= '0' && c <= '9') || c == 'M' || c == 'm')) {
            this.handleMouse(readlineKeys);
            readlineState = 0;
            readlineKeys = '';
            continue;
          } else {
            readlineKeys += c;
          }

          break;
      }

      break;
    }
  }

  /* bracketed paste: ESC [200~ starts it, the text is inserted as is */
  pasteStart() {
    this.pasting = '';
  }

  /* collects pasted text up to ESC [201~, then inserts it, CR LF as LF */
  pasteChar(c) {
    this.pasting += c;

    if(!this.pasting.endsWith('\x1b[201~')) return;

    const text = this.pasting.slice(0, -6).replace(/\r\n?/g, '\n');

    this.pasting = null;
    this.insert(text);
    this.lastFun = this.insert;
    this.update();
  }

  /* piped: collects chars into lines, one queued command per newline */
  pipeChar(c1) {
    if(c1 == 10) {
      this.pipeQueue.push(this.pipeLine.replace(/\r$/, ''));
      this.pipeLine = '';
      this.pipeDrain();
    } else if(c1 != 3) {
      this.pipeLine += String.fromCodePoint(c1);
    }
  }

  /* piped: runs queued commands in order, one at a time (await waits) */
  pipeDrain() {
    if(this.pipeDraining) return;

    this.pipeDraining = true;

    while(!this.pipeBusy && this.pipeQueue.length) {
      this.promptShown = false;
      this.readlineCallback.call(this, this.pipeQueue.shift());
    }

    this.pipeDraining = false;

    if(this.eof && !this.pipeBusy && !this.pipeQueue.length && !this.pipeDone) {
      this.pipeDone = true;
      this.running = false;

      for(const handler of this.cleanupHandlers) handler.call(this);
    }
  }

  handleMouse(keys) {
    this.debug('handleMouse', { keys });

    const [button, x, y, cmd] = [...keys.matchAll(/([0-9]+|[A-Za-z]+)/g)].map(p => p[1]).map(p => (!isNaN(+p) ? +p : p));
    let press = cmd == 'm';

    this.debug('handleMouse', { button, x, y, press });
  }

  handleCmd(expr) {
    let colorstate,
      cmd = null;

    if(expr === null) {
      expr = '';
      return false;
    }

    if(expr === '?') {
      this.help();
      return false;
    }

    cmd = this.cmd = this.extractDirective(expr);

    if(cmd.length > 0) {
      if(!this.handleDirective(cmd, expr)) return false;

      expr = expr.substring(cmd.length + 1);
    }

    if(expr === '') return false;

    if(this.mexpr) expr = this.mexpr + '\n' + expr;
    colorstate = this.colorizeJs(expr);
    this.pstate = colorstate[0];
    this.level = colorstate[1];

    /* piped input continues here, a terminal in acceptLine */
    if(this.piped && this.pstate && colorstate[3].mismatch < 0 && !/['"]/.test(this.pstate)) {
      this.mexpr = expr;
      return false;
    }

    this.pstate = '';

    this.mexpr = '';

    const is_async = /\bawait\b/.test(expr);

    if(expr != '') this.historyAdd(expr);

    if(hasBignum) {
      BigFloatEnv.setPrec(() => this.evalAndPrintStart(expr, is_async), prec, expBits);
    } else {
      this.evalAndPrintStart(expr, is_async);
    }

    this.level = 0;

    /* run the garbage collector after each command */
    //gc();

    return true;
  }

  evalAndPrintStart(expr, is_async) {
    if(expr == '') return;

    try {
      if(evalMode === 'math') expr = '"use math"; void 0;' + expr;
      this.evalStartTime = Date.now();
      if(is_async) this.pipeBusy = true;

      /* eval as a script */
      let result = globalThis.evalBuf ? evalBuf(expr, { backtrace_barrier: true, async: is_async }) : evalScript(expr, { backtrace_barrier: true, async: is_async });

      if(is_async) {
        /* result is a promise of { value } */
        result.then(
          ({ value }) => this.printEvalResult(value),
          err => this.printEvalError(err),
        );
      } else {
        this.printEvalResult(result);
      }
    } catch(error) {
      this.printEvalError(error);
    }
  }

  printEvalResult(result) {
    this.evalTime = Date.now() - this.evalStartTime;
    puts(colors[styles.result]);
    const s = inspect(result, this.piped ? { ...this.inspectOptions, colors: false } : this.inspectOptions);

    print(colorHTML(s));
    puts('\n');
    puts(colors.none);

    /* set the last result */
    this._ = result;
    this.lastResult = result;

    this.handleCmdEnd();
  }

  /* prints an error as Node does: 'Uncaught TypeError: x' and the frames
   * below the REPL input, 'Uncaught 7' for a value that is not an Error
   */
  printEvalError(error) {
    let text;

    if(error instanceof Error) {
      const frames = (error.stack ?? '').split('\n').filter(line => line.trim() != '' && !/^\s*at (<eval> \()?<(input|evalScript)>/.test(line));

      text = [error.message ? `${error.name}: ${error.message}` : error.name, ...frames].join('\n');
    } else {
      text = inspect(error, { colors: false });
    }

    this.lastError = error;

    puts(colors[styles.errorMsg]);
    puts('Uncaught ' + text + '\n');
    puts(colors.none);

    this.handleCmdEnd();
  }

  /* _ and _error read the last result and error, assigning one stops that.
   *
   *   > _ = 5
   *   Expression assignment to _ now disabled.
   */
  defineLastValues() {
    for(const [name, key] of [
      ['_', 'lastResult'],
      ['_error', 'lastError'],
    ]) {
      const desc = Object.getOwnPropertyDescriptor(globalThis, name);

      if(desc && !desc.configurable) continue;

      Object.defineProperty(globalThis, name, {
        configurable: true,
        get: () => this[key],
        set: value => {
          Object.defineProperty(globalThis, name, { value, writable: true, configurable: true });
          puts(`Expression assignment to ${name} now disabled.\n`);
        },
      });
    }
  }

  handleCmdEnd() {
    this.level = 0;
    this.pipeBusy = false;

    /* run the garbage collector after each command */
    gc();
    this.cmdReadlineStart();

    if(this.piped) this.pipeDrain();
  }

  hasDirective(cmd) {
    const { directives = {} } = this;
    return cmd in directives;
  }

  getDirective(cmd) {
    const { directives = {} } = this;
    return directives[cmd];
  }

  handleDirective(cmd, expr) {
    let param,
      prec1,
      expBits1 = null;

    //console.log('handleDirective', { cmd, expr });

    this.historyAdd(expr);

    if(this.hasDirective(cmd)) {
      const paramStr = expr.substring(cmd.length + 1).trim();
      const re = /'(\\'|[^'])*'|"(\\"|[^"])*"|`(\\`|[^`])*`|([^\s]+)/g;
      const params = [];
      let match;

      while((match = re.exec(paramStr))) {
        if(/^['"`]/.test(match[0])) match[0] = match[0].slice(1, -1);
        params.push(match[0]);
      }

      const d = this.getDirective(cmd);

      if(d) {
        const [fn, help] = d;
        fn.call(this, ...params);
      }

      return false;
    } else if(cmd === 'h' || cmd === '?' || cmd == 'help') {
      this.help();
    } else if(cmd === 'load') {
      let filename = expr.substring(cmd.length + 1).trim();
      if(filename.lastIndexOf('.') <= filename.lastIndexOf('/')) filename += '.js';
      loadScript(filename);
      return false;
    } else if(cmd === 'x') {
      hexMode = true;
    } else if(cmd === 'd') {
      hexMode = false;
    } else if(cmd === 't') {
      this.showTime = !this.showTime;
    } else if(hasBignum && cmd === 'p') {
      param = expr
        .substring(cmd.length + 1)
        .trim()
        .split(' ');
      if(param.length === 1 && param[0] === '') {
        puts('BigFloat precision=' + prec + ' bits (~' + Math.floor(prec / log2_10) + ' digits), exponent size=' + expBits + ' bits\n');
      } else if(param[0] === 'f16') {
        prec = 11;
        expBits = 5;
      } else if(param[0] === 'f32') {
        prec = 24;
        expBits = 8;
      } else if(param[0] === 'f64') {
        prec = 53;
        expBits = 11;
      } else if(param[0] === 'f128') {
        prec = 113;
        expBits = 15;
      } else {
        prec1 = parseInt(param[0]);
        if(param.length >= 2) expBits1 = parseInt(param[1]);
        else expBits1 = BigFloatEnv.expBitsMax;
        if(Number.isNaN(prec1) || prec1 < BigFloatEnv.precMin || prec1 > BigFloatEnv.precMax) {
          puts('Invalid precision\n');
          return false;
        }

        if(Number.isNaN(expBits1) || expBits1 < BigFloatEnv.expBitsMin || expBits1 > BigFloatEnv.expBitsMax) {
          puts('Invalid exponent bits\n');
          return false;
        }

        prec = prec1;
        expBits = expBits1;
      }

      return false;
    } else if(hasBignum && cmd === 'digits') {
      param = expr.substring(cmd.length + 1).trim();
      prec1 = Math.ceil(parseFloat(param) * log2_10);

      if(prec1 < BigFloatEnv.precMin || prec1 > BigFloatEnv.precMax) {
        puts('Invalid precision\n');
        return false;
      }

      prec = prec1;
      expBits = BigFloatEnv.expBitsMax;
      return false;
    } else if(hasBignum && cmd === 'mode') {
      param = expr.substring(cmd.length + 1).trim();
      if(param === '') {
        puts('Running mode=' + evalMode + '\n');
      } else if(param === 'std' || param === 'math') {
        evalMode = param;
      } else {
        puts('Invalid mode\n');
      }

      return false;
    } else if(cmd === 'editor') {
      this.editorMode = !this.piped;
      if(this.editorMode) puts('// Entering editor mode (Ctrl+D to finish, Ctrl+C to cancel)\n');
      return false;
    } else if(cmd === 'break') {
      this.mexpr = this.pstate = '';
      this.level = 0;
      return false;
    } else if(cmd === 'clear') {
      eraseInDisplay(stdout, 2);
      cursorPosition(stdout, 1, 1);
    } else if(cmd === 'q') {
      this.exit(0);
    } else if(hasJscalc && cmd === 'a') {
      algebraicMode = true;
    } else if(hasJscalc && cmd === 'n') {
      algebraicMode = false;
    } else {
      puts('Unknown directive: ' + cmd + '\n');
      return false;
    }

    return true;
  }

  handleKey(keys) {
    let fun = null;

    if(this.quoteFlag) {
      if(ucsLength(keys) === 1) this.insert(keys);
      this.quoteFlag = false;
    } else if((fun = this.commands[keys])) {
      this.thisFun = fun;
      // let fn = fun ?? (this.thisFun = (...args) => fun.call(this, ...args));
      switch (fun.call(this, keys)) {
        case -1:
          this.readlineCallback.call(this, this.cmd);
          return;
        case -2:
          this.readlineCallback.call(this, null);
          return;
        case -3:
          /* uninstall a Ctrl-C signal handler */
          signal(SIGINT, null);
          /* uninstall the stdin read handler */
          //setReadHandler(termFd, null);

          this.exit(0);
          return;
        default:
          if(
            searchState &&
            [
              this.acceptLine,
              this.backwardChar,
              this.backwardDeleteChar,
              this.backwardKillLine,
              this.backwardKillWord,
              this.backwardWord,
              this.beginningOfLine,
              this.deleteChar,
              this.forwardChar,
              this.forwardWord,
              this.killLine,
              this.killWord,
            ].indexOf(fun) == -1
          ) {
            const histcmd = this.history[this.searchIndex];

            //this.readlineCallback = this.readlineHandleCmd;
            searchState = 0;
            //this.cmd = histcmd;
            cursorHorizontalAbsolute(stdout, 1);
            eraseInDisplay(stdout, 0);

            this.cursorPos = histcmd?.length ?? 0;
            this.readlineStart(histcmd, this.readlineHandleCmd);
            this.update();
            return;
          }

          break;
      }

      this.lastFun = this.thisFun;
    } else if(ucsLength(keys) === 1 && keys >= ' ') {
      this.insert(keys);
      this.lastFun = this.insert;
    } else {
      this.alert(); /* beep! */
    }

    this.cursorPos = this.cursorPos < 0 ? 0 : this.cursorPos > this.cmd.length ? this.cmd.length : this.cursorPos;
    this.update();
  }

  help() {
    let o = '';
    const sel = n => (n ? '*' : ' ');
    const putline = s => (o += s + '\n');

    putline(
      'Command line directives:\n' +
        '\\h               this help\n' +
        '\\x              ' +
        sel(hexMode) +
        'hexadecimal number display\n' +
        '\\d              ' +
        sel(!hexMode) +
        'decimal number display\n' +
        '\\t              ' +
        sel(this.showTime) +
        'toggle timing display\n' +
        '\\clear           clear the terminal\n' +
        '\\editor          enter editor mode, Ctrl+D evaluates, Ctrl+C cancels\n' +
        '\\break           drop the lines of an unfinished input',
    );

    if(hasJscalc) putline('\\a              ' + sel(algebraicMode) + 'algebraic mode\n' + '\\n         ' + sel(!algebraicMode) + 'numeric mode');

    if(hasBignum) {
      putline("\\p [m [e]]       set the BigFloat precision to 'm' bits\n" + "\\digits n        set the BigFloat precision to 'ceil(n*log2(10))' bits");

      if(!hasJscalc) putline('\\mode [std|math] change the running mode (current = ' + evalMode + ')');
    }

    if(this.directives)
      for(const cmd in this.directives) {
        let [fn, help = ''] = this.directives[cmd];

        /*let args = getFunctionArguments(fn);
        let cmdline = cmd + ' ' + args.reduce((acc, arg) => '[' + (arg + ((acc && acc.trim()) != '' ? ' ' + acc.trim() : '')).trim() + ']', '');*/

        putline('\\' + cmd.padEnd(16) + help);
      }

    if(!configNumcalc) putline('\\q               exit');

    o = o.trimEnd().replace(/[\r\n\t\s]*$/g, '');
    this.printStatus(o);
  }

  makeFilename(type = 'history') {
    const me = scriptArgs[0] ?? 'qjsm';
    const base = basename(me, extname(me));
    const home = getenv('HOME') ?? gethome();

    return `${home}/.${base}_${type}`;
  }

  historyFile() {
    return this.makeFilename('history');
  }

  historyLoad(file) {
    const { history } = this;

    if((file ??= this.historyFile())) {
      this.addCleanupHandler(() => this.historySave(file));

      const data = loadFile(file);

      if(typeof data == 'string') {
        const cmds = data
          .split(/\n/g)
          .reduce((a, i) => {
            const last = a[a.length - 1];

            if(last && last.endsWith('\\')) {
              a[a.length - 1] = last.slice(0, -1) + '\n' + i;
            } else {
              a.push(i);
            }

            return a;
          }, [])
          .filter(s => s.trim() !== '');
        history.splice(0, history.length, ...cmds);

        return (this.historyIndex = history.length);
      }
    }
  }

  historySave(file, maxEntries = 1000) {
    if(this.historyWritten) return;

    if((file ??= this.historyFile())) {
      const cmds = this.history.filter(cmd => (cmd + '').trim() != '');
      const data =
        cmds
          .slice(-maxEntries)
          .map(e => e.replaceAll('\n', '\\\n'))
          .join('\n') + '\n';

      let r = saveFile(file, data);

      this.historyWritten = true;

      this.debug(`wrote ${cmds.length} history entries to '${file}'`);

      return cmds.length;
    }
  }

  async modulesLoad(file) {
    const data = loadFile(file ?? this.makeFilename('modules'));

    if(typeof data == 'string') {
      let modules = new Set(data.split(/\n/g).filter(line => line != ''));

      for(const module of modules) {
        this.printStatus(`Autoloading module: ${module}`);

        try {
          await loadModule(module);
          this.modules.add(module);
        } catch(e) {
          this.printStatus(`ERROR: ${e.message + '\n' + e.stack}`);
        }
      }
    }
  }

  modulesSave(file = this.makeFilename('modules')) {
    const f = fopenSync(file, 'w+');

    for(const module of this.modules) f.puts(module + '\n');

    f.close();
  }

  historyClear() {
    const { history } = this;

    history.splice(0, history.length);

    this.historyIndex = history.length;
  }

  historyAdd(str) {
    const { history } = this;
    const last = history[history.length - 1];

    if(str && str !== '' && str !== last) {
      history.push(str);
      delete this.historyWritten;
    }

    return (this.historyIndex = history.length);
  }

  historySearch(dir) {
    const { history } = this;
    const pos = this.cursorPos;

    for(let i = 1; i <= history.length; i++) {
      const index = (history.length + i * dir + this.historyIndex) % history.length;

      if(history[index].substring(0, pos) == this.cmd.substring(0, pos)) {
        this.historyIndex = index;
        this.cmd = history[index];
        return;
      }
    }
  }

  historySearchBackward() {
    return this.historySearch(-1);
  }

  historySearchForward() {
    return this.historySearch(1);
  }

  /* Up: the line above, on the first line the previous entry */
  historyPrevious() {
    const { history } = this;

    if(this.lineMove(-1)) return;

    if(this.historyIndex > 0) {
      if(this.historyIndex == history.length) if (this.cmd != '') history.push(this.cmd);

      this.historyIndex--;
      this.cmd = history[this.historyIndex];
      this.cursorPos = this.cmd.length;
    }
  }

  /* Down: the line below, on the last line the next entry */
  historyNext() {
    const { history } = this;

    if(this.lineMove(1)) return;

    if(this.historyIndex < history.length - 1) {
      this.historyIndex++;
      this.cmd = history[this.historyIndex];
      this.cursorPos = this.cmd.length;
    }
  }

  insert(str) {
    if(str) {
      this.cmd = this.cmd.substring(0, this.cursorPos) + str + this.cmd.substring(this.cursorPos);
      this.cursorPos += str.length;
    }
  }

  /* ^K: kills to the end of the buffer line, at its end the newline */
  killLine() {
    const end = this.lineEnd();

    this.killRegion(this.cursorPos, end == this.cursorPos && end < this.cmd.length ? end + 1 : end, 1);
  }

  killRegion(start, end, dir) {
    const s = this.cmd.substring(start, end);
    if(this.lastFun !== this.killRegion) clipBoard = s;
    else if(dir < 0) clipBoard = s + clipBoard;
    else clipBoard = clipBoard + s;

    this.cmd = this.cmd.substring(0, start) + this.cmd.substring(end);
    if(this.cursorPos > end) this.cursorPos -= end - start;
    else if(this.cursorPos > start) this.cursorPos = start;
    this.thisFun = this.killRegion;
  }

  killWord() {
    this.killRegion(this.cursorPos, this.skipWordForward(this.cursorPos), 1);
  }

  numberToString(a, radix) {
    let s = null;

    if(!isFinite(a))
      /* NaN, Infinite */
      return a.toString();

    if(a == 0) {
      if(1 / a < 0) s = '-0';
      else s = '0';
    } else {
      if(radix == 16 && a === Math.floor(a)) {
        let s = null;

        if(a < 0) {
          a = -a;
          s = '-';
        } else {
          s = '';
        }

        s += '0x' + a.toString(16);
      } else {
        s = a.toString();
      }
    }

    return s;
  }

  print(a) {
    const stack = [];

    function printRec(a) {
      let n,
        i,
        keys,
        key,
        type,
        s = null;

      type = typeof a;

      if(type === 'object') {
        if(a === null) {
          puts(a);
        } else if(stack.indexOf(a) >= 0) {
          puts('[circular]');
        } else if(
          hasJscalc &&
          (a instanceof Fraction || a instanceof Complex || a instanceof Mod || a instanceof Polynomial || a instanceof PolyMod || a instanceof RationalFunction || a instanceof Series)
        ) {
          puts(a.toString());
        } else {
          stack.push(a);

          if(Array.isArray(a)) {
            n = a.length;
            puts('[ ');

            for(i = 0; i < n; i++) {
              if(i !== 0) puts(', ');

              if(i in a) {
                printRec(a[i]);
              } else {
                puts('<empty>');
              }

              if(i > 20) {
                puts('...');
                break;
              }
            }

            puts(' ]');
          } else if(Object.__getClass(a) === 'RegExp') {
            puts(a.toString());
          } else {
            keys = Object.keys(a);
            n = keys.length;
            puts('{ ');

            for(i = 0; i < n; i++) {
              if(i !== 0) puts(', ');
              key = keys[i];
              puts(key, ': ');
              printRec(a[key]);
            }

            puts(' }');
          }

          stack.pop(a);
        }
      } else if(type === 'string') {
        s = a.__quote();

        if(s.length > 79) s = s.substring(0, 75) + '..."';

        puts(s);
      } else if(type === 'number') {
        puts(this.numberToString(a, hexMode ? 16 : 10));
      } else if(type === 'bigint') {
        puts(this.bigintToString(a, hexMode ? 16 : 10));
      } else if(type === 'bigfloat') {
        puts(this.bigfloatToString(a, hexMode ? 16 : 10));
      } else if(type === 'bigdecimal') {
        puts(a.toString() + 'm');
      } else if(type === 'symbol') {
        puts(String(a));
      } else if(type === 'function') {
        puts('function ' + a.name + '()');
      } else {
        puts(a);
      }
    }

    printRec(a);
  }

  show(result) {
    const opts = { ...this.inspectOptions };

    if(this.piped) opts.colors = false;

    let out = inspect(result, opts);

    for(let i = 0; i == 0 || (opts.maxArrayLength != Infinity && opts.maxArrayLength > 10); ++i) {
      let [lines, columns] = LinesAndColumns(out);

      if(lines < this.lines) break;
      if(opts.depth == Infinity) opts.depth = 4;
      //else if(opts.maxArrayLength == Infinity) opts.maxArrayLength = Math.ceil(this.lines / 2);
      else if(columns < this.columns && opts.compact !== false && (typeof opts.compact != 'number' || opts.compact <= 3)) opts.compact = (opts.compact | 0) + 1;
      else if(opts.maxArrayLength !== Infinity) opts.maxArrayLength >>>= 1;
      out = inspect(result, opts);
    }

    function LinesAndColumns(str) {
      let k = 0,
        rows = 0,
        n = str.length;

      for(let i = 0, j = 0; i < n;) {
        if((j = str.indexOf('\n', i)) == -1) break;

        let w = j - i;
        ++rows;

        //ret.push(w);
        if(k < w) k = w;
        i = j + 1;
      }

      return [rows, k];
    }

    return out;
  }

  printStatus(fn, prompt = !this.evalRunning) {
    const print = str => printPaged.call(this, str, Infinity);

    if(typeof fn == 'string') {
      const str = fn + (fn.endsWith('\n') ? '' : '\n');
      fn = () => print(str);
    } else if(typeof fn != 'function') {
      let a = Array.isArray(fn) ? fn : [...fn];
      fn = () =>
        print(
          a
            .map((item, i) => (typeof item == 'string' ? item : this.show(item)))
            .reduce((acc, item) => (acc += (stripAnsi(acc).trim() == '' || /\s$/g.test(acc) || acc.endsWith('\n') ? '' : ' ') + `${item}`), ''),
        );
    }

    if(typeof fn != 'function') throw new Error(`repl.printStatus not a function fn=${fn}`);

    this.readlineRemovePrompt();

    try {
      fn();
    } catch(e) {
      console.log('printStatus ERROR:', e, e.stack);
    }

    if(prompt) this.readlinePrintPrompt();
  }

  printFunction(logFn) {
    return (...args) => this.printStatus(() => logFn(...args));
  }

  quotedInsert() {
    this.quoteFlag = true;
  }

  reverseSearch() {
    if(searchState == 0) this.cmd = '';
    searchState--;
    this.readlineCallback = this.searchCb;

    try {
      this.update();
    } catch(error) {
      console.log('ERROR:', error.message + '\n' + error.stack);
    }

    return -2;
  }

  forwardSearch() {
    if(searchState == 0) this.cmd = '';
    searchState++;
    this.readlineCallback = this.searchCb;
    //this.debug('forwardSearch', { searchState, termCursorX });
    this.update();
    return -2;
  }

  searchCb(pattern) {
    if(pattern !== null) {
      const histcmd = this.history[this.searchIndex];
      //this.debug('searchCb', { pattern, histcmd,cmd }, this.history.slice(-2));
      searchState = 0;
      this.readlineCallback = this.readlineHandleCmd;
      this.cmd = '';
      this.readlineHandleCmd(histcmd ?? '');

      this.update();
    }
  }

  readlineHandleCmd(expr) {
    //if(expr.trim() !== '') this.historyAdd(expr.trim());

    if(!this.handleCmd(expr)) this.cmdReadlineStart();
  }

  readlinePrintPrompt() {
    if(this.piped) {
      if(this.promptShown) return;

      this.promptShown = true;
      if(this.out) ttyWrite(this.out, this.prompt);
      return;
    }

    if(termCursorX > 0 || this.renderRow > 0) this.readlineRemovePrompt();

    const width = textWidth(this.prompt);

    if(this.out) ttyWrite(this.out, this.prompt);
    termCursorX = width % this.columns;
    this.renderRow = Math.floor(width / this.columns);
    this.lastCmd = '';
    this.lastCursorPos = 0;
  }

  readlineStart(defstr, cb) {
    this.cmd = defstr || '';
    this.cursorPos = this.cmd.length;
    this.historyIndex = this.history.length;
    this.readlineCallback = cb;

    this.prompt = this.pstate;

    if(this.mexpr) {
      this.prompt += this.dupstr(' ', this.plen - this.prompt.length);
      this.prompt += this.ps2;
    } else {
      if(this.showTime) {
        const t = this.evalTime / 1000;
        this.prompt += t.toFixed(6) + ' ';
      }

      this.plen = this.prompt.length;
      this.prompt += this.ps1;
    }

    this.readlinePrintPrompt();
    this.update();

    readlineState = 0;
  }

  reset() {
    this.cmd = '';
    this.cursorPos = 0;
  }

  async run(inputHandler) {
    this.running = true;

    if(inputHandler) this.handleInput = inputHandler;

    this.debug('run');

    await this.termInit();

    if(supportsAnsi) {
      puts('\x1b[?2004h'); /* bracketed paste on */
      this.addCleanupHandler(() => puts('\x1b[?2004l'));
    }

    this.cmdStart(this.showBanner);

    do {
      await waitRead(termFd);
      await this.termReadHandler();
    } while(this.running);

    this.readlineRemovePrompt();
  }

  sigintHandler() {
    if(this.piped) return this.exit(0);

    /* send Ctrl-C to readline */
    this.handleByte(3);
  }

  skipWordBackward(pos) {
    while(pos > 0 && !isWord(this.cmd.charAt(pos - 1))) pos--;
    while(pos > 0 && isWord(this.cmd.charAt(pos - 1))) pos--;

    return pos;
  }

  skipWordForward(pos) {
    while(pos < this.cmd.length && !isWord(this.cmd.charAt(pos))) pos++;
    while(pos < this.cmd.length && isWord(this.cmd.charAt(pos))) pos++;

    return pos;
  }

  termInit() {
    let tab = null;
    termFd = stdin.fileno();

    /* get the terminal size */
    this.columns = 80;

    if(isatty(termFd)) {
      if((tab = ttyGetWinSize(termFd))) {
        this.columns = tab[0];
        this.lines = tab[1];
      }

      /* set the TTY to raw mode */
      if(!this.piped) setRaw(termFd);
    }

    /* install a Ctrl-C signal handler */
    signal(SIGINT, arg => this.sigintHandler(arg));

    /* install a handler to read stdin */
    //setReadHandler(termFd, () => this.termReadHandler());
  }

  mouseTracking(enable = true) {
    (enable ? mousetrackingEnable : mousetrackingDisable)(stdout);
  }

  setupMouseTracking() {
    this.mouseTracking(true);

    this.addCleanupHandler(() => {
      this.mouseTracking(false);
      console.log('Mouse tracking disabled');
    });
  }

  termReadHandler() {
    const l = ttyRead(termFd, this.readBuf.buffer, 0, this.readBuf.length);

    if(l == -EAGAIN) return;

    if(this.piped && l <= 0) {
      this.eof = true;
      this.running = false;
      return this.pipeDrain();
    }

    for(let i = 0; i < l; i++) this.handleByte(this.readBuf[i]);
  }

  transposeChars() {
    let pos = this.cursorPos;

    if(this.cmd.length > 1 && pos > 0) {
      if(pos == this.cmd.length) pos--;
      this.cmd = this.cmd.substring(0, pos - 1) + this.cmd.substring(pos, pos + 1) + this.cmd.substring(pos - 1, pos) + this.cmd.substring(pos + 1);
      this.cursorPos = pos + 1;
    }
  }

  transposeWords() {
    const p1 = this.skipWordBackward(this.cursorPos);
    const p2 = this.skipWordForward(p1);
    const p4 = this.skipWordForward(this.cursorPos);
    const p3 = this.skipWordBackward(p4);

    if(p1 < p2 && p2 <= this.cursorPos && this.cursorPos <= p3 && p3 < p4) {
      this.cmd = this.cmd.substring(0, p1) + this.cmd.substring(p3, p4) + this.cmd.substring(p2, p3) + this.cmd.substring(p1, p2);
      this.cursorPos = p4;
    }
  }

  upcaseWord() {
    const end = this.skipWordForward(this.cursorPos);
    this.cmd = this.cmd.substring(0, this.cursorPos) + this.cmd.substring(this.cursorPos, end).toUpperCase() + this.cmd.substring(end);
  }

  update() {
    if(!this.out || this.piped) return;

    let colorize = this.showColors;

    if(searchState) {
      let cmdLine = this.cmd;
      const { history } = this;
      const re = new RegExp((this.searchPattern = cmdLine.replace(/([\[\]\(\)\?\+\*])/g, '\\$1')), 'i');
      const num = searchState > 0 ? searchState - 1 : searchState;
      //this.searchIndex = this.history.findLastIndex(c => re.test(c) && --num == 0);
      let historySearch = rotateLeft([...history.entries()], this.historyIndex);

      this.searchMatches.splice(0, this.searchMatches.length, ...historySearch.filter(([i, c]) => re.test(c)));

      //num = searchState > 0 ? searchState - 1 : searchState;
      const match = this.searchMatches[num < 0 ? this.searchMatches.length + num : num];

      // onsole.log("this.searchMatches", this.searchMatches.length, num, match);

      const [histidx = -1, entry = ''] = match || [];
      const histcmd = entry.replaceAll('\n', '⏎'); /* ⏎ for a newline, one row */
      const histdir = searchState > 0 ? 'forward' : 'reverse';
      const histpos = searchState < 0 ? historySearch.indexOf(match) - historySearch.length : historySearch.indexOf(match);
      this.searchIndex = histidx;
      let lineStart = `(${histdir}-searchState[${histpos}])\``;
      cmdLine = `${lineStart}${this.cmd}': ${histcmd}`;
      colorize = false;
      const start = cmdLine.length - histcmd.length - 3 - this.cmd.length + this.cursorPos;

      this.paintLine(cmdLine, start);
      this.lastCmd = cmdLine;
      this.lastCursorPos = this.cursorPos;
      return;
    }

    if(this.cmd != this.lastCmd || this.cursorPos != this.lastCursorPos) this.render();
  }

  /* draws text in place of the prompt and buffer rows, the cursor at index */
  paintLine(text, index) {
    const { columns } = this;
    const width = ucsLength(text),
      at = ucsLength(text.slice(0, index));
    const endRow = Math.floor(width / columns),
      row = Math.floor(at / columns),
      col = at % columns;
    let s = (this.renderRow > 0 ? `\x1b[${this.renderRow}A` : '') + '\r\x1b[J' + text;

    if(width > 0 && width % columns == 0) s += ' \x08';

    if(endRow > row) s += `\x1b[${endRow - row}A`;

    s += '\r' + (col > 0 ? `\x1b[${col}C` : '');

    ttyWrite(this.out, s);

    this.renderRow = row;
    termCursorX = col;
  }

  /* redraws the prompt and every line of the buffer, then places the cursor.
   *
   * each buffer line takes floor((prefix + text) / columns) + 1 rows, a
   * line that fills its last row exactly gets ' \b' so the terminal wraps
   * as counted, a later line is prefixed with ps2. Without cursorMarks the
   * brackets around the cursor are not marked, as for submitted input.
   */
  render(cursorMarks = true) {
    const { out, columns, cmd, cursorPos } = this;
    const prefixes = [this.prompt, this.dupstr(' ', this.plen) + this.ps2];
    const offset = this.mexpr ? this.mexpr.length + 1 : 0;
    const str = this.mexpr ? this.mexpr + '\n' + cmd : cmd;
    let kinds = null,
      s = (this.renderRow > 0 ? `\x1b[${this.renderRow}A` : '') + '\r\x1b[J',
      row = 0,
      pos = 0,
      cursorRow = 0,
      cursorCol = 0,
      endRow = 0;

    if(this.showColors) {
      const [, , styleNames, brackets] = this.colorizeJs(str, cursorMarks ? offset + cursorPos : -1);

      kinds = this.bracketStyles(styleNames, brackets, offset + cursorPos);
    }

    for(const [k, line] of cmd.split('\n').entries()) {
      const prefix = prefixes[k ? 1 : 0];
      const pw = textWidth(prefix);
      const width = pw + ucsLength(line);

      if(k) s += '\n';

      s += prefix + (kinds ? colorText(str, offset + pos, offset + pos + line.length, kinds) : line).replaceAll('\t', ' ');

      if(width > 0 && width % columns == 0) s += ' \x08';

      if(cursorPos >= pos && cursorPos <= pos + line.length) {
        const w = pw + ucsLength(line.slice(0, cursorPos - pos));

        cursorRow = row + Math.floor(w / columns);
        cursorCol = w % columns;
      }

      endRow = row + Math.floor(width / columns);
      row = endRow + 1;
      pos += line.length + 1;
    }

    if(endRow > cursorRow) s += `\x1b[${endRow - cursorRow}A`;

    s += '\r' + (cursorCol > 0 ? `\x1b[${cursorCol}C` : '');

    ttyWrite(out, s);

    this.renderRow = cursorRow;
    termCursorX = cursorCol;
    this.lastCmd = cmd;
    this.lastCursorPos = cursorPos;
  }

  yank() {
    this.insert(clipBoard);
  }

  escape() {}
}

Object.assign(REPL.prototype, {
  [Symbol.toStringTag]: 'REPL',
  //handleInput: REPL.prototype.evalAndPrint,
  directives: loadModule
    ? {
        i: [
          async function i(m) {
            try {
              await loadModule(m);

              this.modules.add(m);
              this.modulesSave();
            } catch(error) {
              //console.log('ERROR loading module: ' + error.message + '\n' + error.stack);
              throw error;
            }
          },
          'import a module',
        ],
      }
    : {},
});

function isAlpha(c) {
  return typeof c === 'string' && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
}

function isDigit(c) {
  return typeof c === 'string' && c >= '0' && c <= '9';
}

function isWord(c) {
  return typeof c === 'string' && (isAlpha(c) || isDigit(c) || c == '_' || c == '$');
}

function ucsLength(str) {
  let len = 0,
    strLen = str.length;

  /* we never count the trailing surrogate to have the
       following property: ucsLength(str) =
       ucsLength(str.substring(0, a)) + ucsLength(str.substring(a,
       str.length)) for 0 <= a <= str.length */
  for(let i = 0; i < strLen; i++) {
    const c = str.charCodeAt(i);

    if(c < 0xdc00 || c >= 0xe000) len++;
  }

  return len;
}

function rotateLeft(array, n) {
  array.push(...array.splice(0, n));

  return array;
}

function isTrailingSurrogate(c) {
  if(typeof c !== 'string') return false;

  const d = c.codePointAt(0); /* can be NaN if empty string */

  return d >= 0xdc00 && d < 0xe000;
}

function isBalanced(a, b) {
  switch (a + b) {
    case '()':
    case '[]':
    case '{}':
    case '$}':
      return true;
  }

  return false;
}

/* str from start to end with the escapes of styleNames, one style name
 * per unit, a name may carry +u (underline) and +r (inverse)
 */
function colorText(str, start, end, styleNames) {
  let s = '';

  for(let i, j = start; j < end;) {
    const style = styleNames[(i = j)];

    while(++j < end && styleNames[j] == style) continue;

    const [name, ...attrs] = (style ?? 'default').split('+');

    s += colors[styles[name] || 'default'];
    for(const attr of attrs) s += attr == 'u' ? colors.underline : colors.inverse;
    s += str.substring(i, j) + colors.none;
  }

  return s;
}

/* columns a string takes in the terminal, escapes not counted */
function textWidth(str) {
  return ucsLength(stripAnsi(str));
}

function printPaged(str, numLines = Infinity) {
  const lines = str.split('\n');
  const totalLines = lines.length;

  if(lines[lines.length - 1] === '') lines.pop();

  while(lines.length >= numLines) {
    let pos = totalLines - lines.length;
    let list = lines.splice(0, numLines - 1);
    let s = list.join('\n') + '\n';

    puts(s);
    s = `(${pos}-${pos + list.length - 1}/${totalLines}) more?`;
    puts(colors.brightRed + s + colors.none);
    this.cursorPos = s.length;

    let key = waitkey();

    this.readlineRemovePrompt();

    if(key < 0) return;

    if(key > 0) break;
  }

  let s = lines.join('\n') + '\n';

  puts(s);

  /* q at the end of input, so waitkey() stops */
  function getbyte() {
    const key = readKey(0);

    return { char: key.char?.codePointAt(0), space: 32, enter: 13, eof: 113 }[key.type];
  }

  function waitkey() {
    for(;;) {
      let b = getbyte();

      if([65, 97].indexOf(b) != -1) return 1;
      if([32, 13, 10, 121, 89, 106, 74].indexOf(b) != -1) return 0;
      if([113, 81, 115, 83].indexOf(b) != -1) return -1;
    }
  }
}

function saveFile(filename, data) {
  let f, r;

  if((f = fopenSync(filename, 'w+'))) {
    f.puts(data);
    r = f.tell();
    f.close();
  }

  return r;
}

function observeProperties(target = {}, obj, fn = (prop, value) => {}, opts = {}) {
  opts = { enumerable: false, ...opts };

  for(const prop of Object.getOwnPropertyNames(obj)) {
    Object.defineProperty(target, prop, {
      ...opts,
      get: () => obj[prop],
      set: value => {
        let r = fn(prop, value, obj);
        if(r !== false) obj[prop] = value;
        if(typeof r == 'function') r(prop, value, obj);
      },
    });
  }
  return target;
}

function loadSaveObject(parent, key, filename) {
  let data,
    origObj = parent[key];

  try {
    data = loadScript(filename);
    Object.assign(origObj, data);
  } catch(e) {}

  parent[key] = observeProperties({}, origObj, (prop, value, obj) => (prop, value, obj) => saveFile(filename, '(' + inspect(obj, { colors: false, reparseable: true, compact: 1 }) + ')\n'), {
    enumerable: true,
  });
}

function waitRead(fd) {
  return new Promise(resolve => setReadHandler(fd, () => (setReadHandler(fd, null), resolve(fd))));
}

function define(obj, ...args) {
  for(const props of args) {
    const desc = Object.getOwnPropertyDescriptors(props);

    for(const prop in desc) {
      try {
        delete obj[prop];
      } catch(e) {}

      try {
        Object.defineProperty(obj, prop, desc[prop]);
      } catch(e) {
        throw new TypeError(`property '${prop}' is not configurable`);
      }
    }
  }

  return obj;
}

function once(fn, thisArg, memoFn) {
  let ret,
    ran = false;

  return function(...args) {
    if(!ran) {
      ran = true;
      ret = fn.apply(thisArg || this, args);
    } else if(typeof memoFn == 'function') {
      ret = memoFn(ret);
    }

    return ret;
  };
}

export async function loadModule(moduleName) {
  for(const [modifier, name] of [...moduleName.matchAll(/([*!=,]?)([^*!=,]+),?/g)].map(m => m.slice(1))) {
    let module,
      base = basename(name, extname(name));

    try {
      module = await import(name);
    } catch(error) {
      console.log(`\rloadModule(${moduleName}) ERROR`, error);
      throw error;
    }

    switch (modifier) {
      case '*': {
        Object.assign(globalThis, module);
        break;
      }
      case '!': {
        module.default();
        break;
      }
      case '=': {
        globalThis[base] = module.default;
        break;
      }
      default: {
        globalThis[base] = module;
        break;
      }
    }
  }
}

export class REPLServer {
  #eval = null;
  #repl = null;

  constructor(options = {}) {
    this.#eval = options.eval;
    this.#repl = new REPL(options.prompt);
    this.#repl.run();
  }

  defineCommand(keyword, { help, action }) {
    this.#repl.directives[keyword] = [action, help];
  }

  displayPrompt(preserveCursor) {
    this.#repl.readlinePrintPrompt();
  }

  clearBufferedCommand() {
    this.#repl.mexpr = '';
  }

  setupHistory(historyPath, callback) {
    this.#repl.historyLoad(historyPath);
  }
}
