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

import { basename, extname, gethome } from 'path';
import { clearLine, clearScreen, define, isArray, moveCursor, setCursorPosition, stripAnsi, types, unique, ttySetRaw } from 'util';
import inspect from 'inspect';
import { setReadHandler } from 'io';
import { isatty, platform, read, SIGINT, signal, SIGTERM, ttyGetWinSize } from 'os';
import process from 'process';
import { evalScript, exit, gc, in as stdin, loadFile, loadScript, open as fopen, out as stdout, puts, getenv } from 'std';

('use strict');
('use strip');

const readBufSize = 512;

/* XXX: use preprocessor ? */
var configNumcalc = false;
var hasJscalc = typeof Fraction === 'function';
var hasBignum = typeof BigFloat === 'function';
var supportsAnsi = !platform.startsWith('win');

var colors = supportsAnsi
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
      brightWhite: '\x1b[37;1m'
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
      brightWhite: ''
    };

const cursorLeft = supportsAnsi
  ? n => moveCursor(1, -1)
  : n => {
      const out = this?.out ?? stdout;
      out.puts('\x08'.repeat(n));
      out.flush();
    };

var readlineKeys = null;
var readlineState = null;

var hexMode = false;
var evalMode = 'std';

var styles = {
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
  result: 'white',
  errorMsg: 'brightRed'
};

var clipBoard = '';
var prec = null;
var expBits = null;
var log2_10 = null;

var utf8_state = 0;
var utf8_val = 0;

var termFd = null;
/* current X position of the cursor in the terminal */
var termCursorX = 0;

var setRaw = once(fd => ttySetRaw(fd));
var putBanner = once(() => puts('QuickJS - Type "\\h" for help\n'));

var searchState = 0;

export class REPL {
  pstate = '';
  prompt = '';
  plen = 0;
  ps1 = '> ';
  ps2 = '  ... ';
  utf8 = true;
  showTime = false;
  showColors = true;
  evalStartTime = undefined;
  evalTime = 0;

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

  modules = new Set();

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
      '\x0e': this.historyNext /* ^N - down */,
      '\x0f': this.controlL /* ^L - kill line */,
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
      '\x1b\x7f': this.backwardKillWord /* M-C-? - this.backwardKillWord */,
      '\x1bb': this.backwardWord /* M-b - this.backwardWord */,
      '\x1bd': this.killWord /* M-d - this.killWord */,
      '\x1bf': this.forwardWord /* M-f - this.backwardWord */,
      '\x1bk': this.backwardKillLine /* M-k - this.backwardKillLine */,
      '\x1bl': this.downcaseWord /* M-l - this.downcaseWord */,
      '\x1bt': this.transposeWords /* M-t - this.transposeWords */,
      '\x1bu': this.upcaseWord /* M-u - this.upcaseWord */,
      '\x7f': this.backwardDeleteChar /* ^? - delete */
    };

    this.commands = commands /*?? define(
      {},
      Object.getOwnPropertyNames(commands).reduce((acc, name) => {
        acc[name] = commands[name].bind(this);
        return acc;
      }, {})
    )*/;

    this.directives = { ...this.directives };

    if(typeof name == 'string') this.ps1 = name + this.ps1;

    if(globalThis.console) this.inspectOptions ??= globalThis.console.options;

    this.termInit();

    signal(SIGTERM, () => this.exit(0));

    if(loadModule) this.modulesLoad();
    this.cmdStart(showBanner);
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
          maxStringLength: Infinity
        }) +
        ')\n'
    );
  }

  addCleanupHandler(fn) {
    let previousHandler = this.cleanup || (() => {});

    if(previousHandler !== fn)
      this.cleanup = () => {
        previousHandler.call(this);
        //console.log('calling cleanup handler', fn);
        fn.call(this);
      };
  }

  exit(exitCode = 0) {
    if(typeof this.cleanup == 'function') this.cleanup();

    ttySetRaw(0, true);
    exit(exitCode);
  }

  abort() {
    this.cmd = '';
    this.cursorPos = 0;
    return -2;
  }

  acceptLine() {
    puts('\n');
    return -1;
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
    this.killRegion(0, this.cursorPos, -1);
  }

  backwardKillWord() {
    this.killRegion(this.skipWordBackward(this.cursorPos), this.cursorPos, -1);
  }

  backwardWord() {
    this.cursorPos = this.skipWordBackward(this.cursorPos);
  }

  beginningOfLine() {
    this.cursorPos = 0;
  }

  bigfloatToString(a, radix) {
    var s = null;
    if(!BigFloat.isFinite(a)) {
      /* NaN, Infinite */
      if(evalMode !== 'math') {
        return 'BigFloat(' + a.toString() + ')';
      } else {
        return a.toString();
      }
    } else {
      if(a == 0) {
        if(1 / a < 0) s = '-0';
        else s = '0';
      } else {
        if(radix == 16) {
          var s = null;
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
    var s = null;
    if(radix == 16) {
      var s = null;
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

  colorizeJs(str) {
    var i,
      c,
      start,
      n = str.length;
    var style,
      state = '',
      level = 0;
    var primary,
      canRegex = 1;
    var r = [];

    function pushState(c) {
      state += c;
    }
    function lastState(c) {
      return state.substring(state.length - 1);
    }
    function popState(c) {
      var c = lastState();
      state = state.substring(0, state.length - 1);
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
      for(i++; i < n; i++) {
        if(str[i] == '\n') {
          break;
        }
      }
    }

    function parseString(delim) {
      style = 'string';
      pushState(delim);
      while(i < n) {
        c = str[i++];
        if(c == '\n') {
          style = 'error';
          continue;
        }
        if(c == '\\') {
          if(i >= n) break;
          i++;
        } else if(c == delim) {
          popState();
          break;
        }
      }
    }

    function parseRegex() {
      style = 'regex';
      pushState('/');
      while(i < n) {
        c = str[i++];
        if(c == '\n') {
          style = 'error';
          continue;
        }
        if(c == '\\') {
          if(i < n) {
            i++;
          }
          continue;
        }
        if(lastState() == '[') {
          if(c == ']') {
            popState();
          }
          // ECMA 5: ignore '/' inside char classes
          continue;
        }
        if(c == '[') {
          pushState('[');
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
      while(i < n && (isWord(str[i]) || (str[i] == '.' && (i == n - 1 || str[i + 1] != '.')))) {
        i++;
      }
    }

    var jsKeywords =
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

    var jsNoRegex = '|this|super|undefined|null|true|false|Infinity|NaN|arguments|';
    var jsTypes = '|void|var|';

    function parseIdentifier() {
      canRegex = 1;

      while(i < n && isWord(str[i])) i++;

      var w = '|' + str.substring(start, i) + '|';

      if(jsKeywords.indexOf(w) >= 0) {
        style = 'keyword';
        if(jsNoRegex.indexOf(w) >= 0) canRegex = 0;
        return;
      }

      var i1 = i;
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

    for(i = 0; i < n; ) {
      style = null;
      start = i;
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
          canRegex = 0;
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
            popState();
            continue;
          }
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
    return [state, level, r];
  }

  completion() {
    var tab, res, s, i, j, len, t;
    res = this.getCompletions(this.cmd, this.cursorPos);
    tab = res.tab;
    if(tab.length === 0) return;
    s = tab[0];
    len = s.length;
    /* add the chars which are identical in all the completions */
    for(i = 1; i < tab.length; i++) {
      t = tab[i];
      for(j = 0; j < len; j++) {
        if(t[j] !== s[j]) {
          len = j;
          break;
        }
      }
    }
    for(i = res.pos; i < len; i++) {
      this.insert(s[i]);
    }
    if(this.lastFun === this.completion && tab.length == 1) {
      /* append parentheses to function names */
      var m = res.ctx[tab[0]];
      if(typeof m == 'function') {
        this.insert('(');
        if(m.length == 0) this.insert(')');
      } else if(typeof m == 'object') {
        this.insert('.');
      }
    }
    //console.log('repl.completion', res.tab.length, this.lastFun);

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

    puts('\n');

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

  controlC() {
    //if(supportsAnsi) printCsi(12, 'i');

    if(this.lastFun === this.controlC) {
      puts('\n');
      if(typeof this.cleanup == 'function') this.cleanup();
      this.exit(0);
    } else {
      puts('\n(Press Ctrl-C again to quit)\n');
      this.readlinePrintPrompt();
    }
  }

  controlD() {
    if(this.cmd.length == 0) {
      puts('\n');
      return -3; /* exit read eval this.print loop */
    } else {
      this.deleteCharDir(1);
    }
  }

  controlL() {
    //puts(`controlL '${Object.getOwnPropertyNames(Object.getPrototypeOf(this)) .filter(n => typeof this[n] != 'function') .join(' ')}'\n` );

    if(this.cmd.length > 0) this.deleteAll();
  }

  deleteChar() {
    this.deleteCharDir(1);
  }

  deleteCharDir(dir) {
    var start,
      end = null;

    start = this.cursorPos;
    if(dir < 0) {
      start--;
      while(isTrailingSurrogate(this.cmd.charAt(start))) start--;
    }
    end = start + 1;
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

    /* this.cursorPos = 0;
    this.cmd = '';
    this.update();*/
  }

  downcaseWord() {
    var end = this.skipWordForward(this.cursorPos);
    this.cmd = this.cmd.substring(0, this.cursorPos) + this.cmd.substring(this.cursorPos, end).toLowerCase() + this.cmd.substring(end);
  }

  dupstr(str, count) {
    var res = '';
    while(count-- > 0) res += str;
    return res;
  }

  endOfLine() {
    this.cursorPos = this.cmd.length;
  }

  readlineRemovePrompt() {
    if(!clearLine(1, 2)) puts('\r' + ' '.repeat(termCursorX));

    if(!setCursorPosition(1, 0)) puts('\r');

    termCursorX = 0;
  }

  printResult(result) {
    if(result !== this['$']) this.resultId = (this.resultId | 0) + 1;

    if(types.isPromise(result)) this.printPromise(result);
    else this.printStatus([colors[styles.result], result, colors.none]);

    if(typeof result == 'unknown') result = undefined;

    this['$'] = result;
    globalThis._ = result;
  }

  printPromise(result, fmt = id => `${colors.gray}<${colors.red}result${colors.gray}#${colors.cyan}${id}${colors.gray}>${colors.none}`) {
    const id = this.resultId;
    const banner = fmt(id);

    if(result !== this['$'])
      result
        .then(value => {
          this._ = value;
          globalThis._ = value;
          this.printStatus([banner, ` resolved to:\n`, value]);
        })
        .catch(value => {
          this['!'] = value;
          this.printStatus([banner, ` rejected (`, value, `)`]);
        });

    this.printStatus(() => console.log(`${banner} -> pending Promise`));
  }

  /* evalAndPrint(expr) {
    var result = null;
    const { inspectOptions } = this;

    try {
      if(evalMode === 'math') expr = '"use math"; void 0;' + expr;
      var now = Date.now();
      this.evalRunning = true;
      // eval as a script
      result = evalScript(expr, { backtraceBarrier: true });
      this.evalTime = Date.now() - now;
      this.evalRunning = false;

      this.printResult(result);

      // set the last result 
    } catch(error) {
      let output = '';
      globalThis.lastError = error;
      output += colors[styles.errorMsg];
      if(error instanceof Error) {
        output += 'Error: ';

        output += error.message;

        if(error.stack) output += error.stack;
      } else {
        output += 'Throw: ';

        if(error?.message) output += error.message;
        else output += inspect(error, { ...inspectOptions, maxArrayLength: Infinity, colors: false });
      }
      output = output.trimEnd();

      output += colors.none;
      output += '\n';
      this.printStatus(output);
    }
  }
*/
  extractDirective(a) {
    var pos = null;
    if(a[0] !== '\\') return '';
    for(pos = 1; pos < a.length; pos++) {
      if(!isAlpha(a[pos])) break;
    }
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
    var s,
      obj,
      ctxObj,
      r,
      i,
      j,
      paren = null;

    s = this.getContextWord(line, pos);
    ctxObj = this.getContextObject(line, pos - s.length);
    r = [];
    /* enumerate properties from object and its prototype chain,
       add non-numeric regular properties with s as e prefix
     */
    for(i = 0, obj = ctxObj; i < 10 && obj !== null && obj !== void 0; i++) {
      var props = Object.getOwnPropertyNames(obj);
      /* add non-numeric regular properties */
      for(j = 0; j < props.length; j++) {
        var prop = props[j];
        if(typeof prop == 'string' && '' + +prop != prop && prop.startsWith(s)) r.push(prop);
      }
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
        for(i = j = 1; i < r.length; i++) {
          if(r[i] != r[i - 1]) r[j++] = r[i];
        }
        r.length = j;
      }
    r = unique(r);
    /* 'tab' = list of completions, 'pos' = cursor position inside
       the completions */
    return { tab: r, pos: s.length, ctx: ctxObj };
  }

  getContextObject(line, pos) {
    try {
      var obj,
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
    var s = '';
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
      if(utf8_state === 0) {
        this.handleChar(utf8_val);
      }
    } else if(c >= 0xc0 && c < 0xf8) {
      utf8_state = 1 + (c >= 0xe0) + (c >= 0xf0);
      utf8_val = c & ((1 << (6 - utf8_state)) - 1);
    } else {
      utf8_state = 0;
      this.handleChar(c);
    }
  }

  handleChar(c1) {
    var c = null;
    c = String.fromCodePoint(c1);
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

  handleMouse(keys) {
    this.debug('handleMouse', { keys });

    const [button, x, y, cmd] = [...keys.matchAll(/([0-9]+|[A-Za-z]+)/g)].map(p => p[1]).map(p => (!isNaN(+p) ? +p : p));
    let press = cmd == 'm';

    this.debug('handleMouse', { button, x, y, press });
  }

  handleCmd(expr) {
    var colorstate,
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
      //console.log('cmd', cmd);
      if(!this.handleDirective(cmd, expr)) return false;
      expr = expr.substring(cmd.length + 1);
    }
    if(expr === '') return false;

    if(this.mexpr) expr = this.mexpr + '\n' + expr;
    colorstate = this.colorizeJs(expr);
    this.pstate = colorstate[0];
    this.level = colorstate[1];

    if(this.pstate) {
      this.mexpr = expr;
      return false;
    }

    this.mexpr = '';

    if(hasBignum) {
      BigFloatEnv.setPrec(this.evalAndPrintStart.bind(this, expr, false), prec, expBits);
    } else {
      this.handleInput(expr);
    }

    this.level = 0;

    /* run the garbage collector after each command */
    //gc();

    return true;
  }

  evalAndPrintStart(expr, is_async) {
    var result;

    try {
      if(evalMode === 'math') expr = '"use math"; void 0;' + expr;
      this.evalStartTime = os.now();
      /* eval as a script */
      result = std.evalScript(expr, { backtrace_barrier: true, async: is_async });
      if(is_async) {
        /* result is a promise */
        result.then(printEvalResult, this.printEvalError);
      } else {
        this.printEvalResult(result);
      }
    } catch(error) {
      this.printEvalError(error);
    }
  }

  printEvalResult(result) {
    this.evalTime = os.now() - this.evalStartTime;
    std.puts(colors[styles.result]);
    print(inspect(result, this.inspectOptions));
    std.puts('\n');
    std.puts(colors.none);
    /* set the last result */
    this._ = result;

    this.handleCmdEnd();
  }

  printEvalError(error) {
    std.puts(colors[styles.error_msg]);
    if(error instanceof Error) {
      console.log(error);
      if(error.stack) {
        std.puts(error.stack);
      }
    } else {
      std.puts('Throw: ');
      console.log(error);
    }
    std.puts(colors.none);

    this.handleCmdEnd();
  }

  handleCmdEnd() {
    this.level = 0;
    /* run the garbage collector after each command */
    std.gc();
    this.cmdReadlineStart();
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
    var param,
      prec1,
      expBits1 = null;
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
      var filename = expr.substring(cmd.length + 1).trim();
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
    } else if(cmd === 'clear') {
      clearScreen(1, 2);
      setCursorPosition(1, 0, 0);
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
    var fun = null;

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
          setReadHandler(termFd, null);

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
              this.killWord
            ].indexOf(fun) == -1
          ) {
            const histcmd = this.history[this.searchIndex];

            //this.readlineCallback = this.readlineHandleCmd;
            searchState = 0;
            //this.cmd = histcmd;
            setCursorPosition(1, 0);
            clearScreen(1, 0);

            this.cursorPos = histcmd.length;
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
      'Command line directives:\n\\h               this help\n' +
        '\\x              ' +
        sel(hexMode) +
        'hexadecimal number display\n' +
        '\\d              ' +
        sel(!hexMode) +
        'decimal number display\n' +
        '\\t              ' +
        sel(this.showTime) +
        'toggle timing display\n' +
        '\\clear           clear the terminal'
    );
    if(hasJscalc) {
      putline('\\a              ' + sel(algebraicMode) + 'algebraic mode\n' + '\\n         ' + sel(!algebraicMode) + 'numeric mode');
    }
    if(hasBignum) {
      putline("\\p [m [e]]       set the BigFloat precision to 'm' bits\n" + "\\digits n        set the BigFloat precision to 'ceil(n*log2(10))' bits");
      if(!hasJscalc) {
        putline('\\mode [std|math] change the running mode (current = ' + evalMode + ')');
      }
    }
    if(this.directives)
      for(let cmd in this.directives) {
        let [fn, help = ''] = this.directives[cmd];
        /*let args = getFunctionArguments(fn);
        let cmdline = cmd + ' ' + args.reduce((acc, arg) => '[' + (arg + ((acc && acc.trim()) != '' ? ' ' + acc.trim() : '')).trim() + ']', '');*/
        putline(
          '\\' +
            cmd /*line*/
              .padEnd(16) +
            help
        );
      }
    if(!configNumcalc) putline('\\q               exit');

    o = o.trimEnd().replace(/[\r\n\t\s]*$/g, '');
    this.printStatus(o);
  }

  makeFilename(type = 'history') {
    const { argv } = process;
    let me = argv[1] ?? argv[0];
    let base = basename(me, extname(me));
    let home = getenv('HOME') ?? gethome();
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
        const cmds = data.split(/\n/g);
        history.splice(0, history.length, ...cmds);
        return (this.historyIndex = history.length);
      }
    }
  }

  historySave(file, maxEntries = 1000) {
    if((file ??= this.historyFile())) {
      const cmds = this.history.filter(cmd => (cmd + '').trim() != '');
      const data = cmds.slice(-maxEntries).join('\n') + '\n';
      let r = saveFile(file, data);
      this.printStatus(`wrote ${cmds.length} history entries to '${file}'`, false);
      return cmds.length;
    }
  }

  async modulesLoad(file) {
    const data = loadFile(file ?? this.makeFilename('modules'));

    if(typeof data == 'string') {
      let modules = new Set(data.split(/\n/g).filter(line => line != ''));

      for(let module of modules) {
        this.printStatus(() => puts(`Autoloading module: ${module}\n`));

        try {
          loadModule(module);
          this.modules.add(module);
        } catch(e) {
          this.printStatus(() => puts(`ERROR: ${e.message + '\n' + e.stack}\n`));
        }
      }
    }
  }

  modulesSave(file = this.makeFilename('modules')) {
    this.printStatus(() => puts(`modulesSave: ${file}\n`));
    const f = fopen(file, 'w+');

    for(let module of this.modules) f.puts(module + '\n');

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

    if(str && str !== last) history.push(str);

    return (this.historyIndex = history.length);
  }

  historySearch(dir) {
    const { history } = this;
    var pos = this.cursorPos;
    for(var i = 1; i <= history.length; i++) {
      var index = (history.length + i * dir + this.historyIndex) % history.length;
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

  historyPrevious() {
    const { history } = this;
    if(this.historyIndex > 0) {
      if(this.historyIndex == history.length) {
        history.push(this.cmd);
      }
      this.historyIndex--;
      this.cmd = history[this.historyIndex];
      this.cursorPos = this.cmd.length;
    }
  }

  historyNext() {
    const { history } = this;
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

  killLine() {
    this.killRegion(this.cursorPos, this.cmd.length, 1);
  }

  killRegion(start, end, dir) {
    var s = this.cmd.substring(start, end);
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

  moveCursor(delta) {
    var i,
      l = null;
    if(delta > 0) {
      while(delta != 0) {
        if(termCursorX == this.columns - 1) {
          puts('\n'); /* translated to CRLF */
          termCursorX = 0;
          delta--;
        } else {
          l = Math.min(this.columns - 1 - termCursorX, delta);
          moveCursor(1, l); /* right */
          delta -= l;
          termCursorX += l;
        }
      }
    } else {
      delta = -delta;
      while(delta != 0) {
        if(termCursorX == 0) {
          moveCursor(1, this.columns - 1, -1); /* up + right */
          delta--;
          termCursorX = this.columns - 1;
        } else {
          l = Math.min(delta, termCursorX);
          moveCursor(1, -l, 0); /* left */
          delta -= l;
          termCursorX -= l;
        }
      }
    }
  }

  numberToString(a, radix) {
    var s = null;
    if(!isFinite(a)) {
      /* NaN, Infinite */
      return a.toString();
    } else {
      if(a == 0) {
        if(1 / a < 0) s = '-0';
        else s = '0';
      } else {
        if(radix == 16 && a === Math.floor(a)) {
          var s = null;
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
  }

  print(a) {
    var stack = [];

    function printRec(a) {
      var n,
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
    let out,
      i,
      opts = { ...this.inspectOptions };

    out = inspect(result, opts);

    for(i = 0; i == 0 || (opts.maxArrayLength != Infinity && opts.maxArrayLength > 10); ++i) {
      let [lines, columns] = LinesAndColumns(out);

      if(lines < this.lines) break;
      if(opts.depth == Infinity) opts.depth = 4;
      //else if(opts.maxArrayLength == Infinity) opts.maxArrayLength = Math.ceil(this.lines / 2);
      else if(columns < this.columns && opts.compact !== false && (typeof opts.compact != 'number' || opts.compact <= 3)) opts.compact = (opts.compact | 0) + 1;
      else if(opts.maxArrayLength !== Infinity) opts.maxArrayLength >>>= 1;
      out = inspect(result, opts);
    }

    function LinesAndColumns(str) {
      let i,
        j,
        k = 0,
        rows = 0,
        n = str.length;
      for(i = j = 0; i < n; ) {
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
            .reduce((acc, item) => (acc += (stripAnsi(acc).trim() == '' || /\s$/g.test(acc) || acc.endsWith('\n') ? '' : ' ') + `${item}`), '')
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
    if(expr.trim() !== '') this.historyAdd(expr);

    if(!this.handleCmd(expr)) this.cmdReadlineStart();
  }

  readlinePrintPrompt() {
    if(termCursorX > 0) this.readlineRemovePrompt();

    this.out?.puts(this.prompt);
    this.out?.flush();
    termCursorX = ucsLength(this.prompt) % this.columns;
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
        var t = this.evalTime / 1000;
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
    if(inputHandler) this.handleInput = inputHandler;

    this.debug('run');

    await this.termInit();
    this.cmdStart(false);
    this.running = true;
    do {
      await waitRead(termFd);
      await this.termReadHandler();
    } while(this.running);
  }

  /*runSync() {
    const fd = stdin.fileno();

    setReadHandler(fd, () => this.termReadHandler());
    this.addCleanupHandler(() => setReadHandler(fd, null));
    return this;
  }*/

  /*stop() {
    const fd = stdin.fileno();
    setReadHandler(fd, null);
    if(this.out) this.out = null;
    return this;
  }*/

  sigintHandler() {
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
    var tab = null;
    termFd = stdin.fileno();

    /* get the terminal size */
    this.columns = 80;
    if(isatty(termFd)) {
      if(ttyGetWinSize) {
        tab = ttyGetWinSize(termFd);
        if(tab) {
          this.columns = tab[0];
          this.lines = tab[1];
        }
      }
      /* set the TTY to raw mode */
      setRaw(termFd);
    }

    /* install a Ctrl-C signal handler */
    signal(SIGINT, arg => this.sigintHandler(arg));

    /* install a handler to read stdin */
    setReadHandler(termFd, () => this.termReadHandler());
  }

  mouseTracking(enable = true) {
    enable ? Terminal.mousetrackingEnable() : Terminal.mousetrackingDisable();
  }

  setupMouseTracking() {
    this.mouseTracking(true);

    this.addCleanupHandler(() => {
      this.mouseTracking(false);
      console.log('Mouse tracking disabled');
    });
  }

  termReadHandler() {
    var l,
      i = null;
    l = read(termFd, this.readBuf.buffer, 0, this.readBuf.length);
    for(i = 0; i < l; i++) this.handleByte(this.readBuf[i]);
  }

  transposeChars() {
    var pos = this.cursorPos;
    if(this.cmd.length > 1 && pos > 0) {
      if(pos == this.cmd.length) pos--;
      this.cmd = this.cmd.substring(0, pos - 1) + this.cmd.substring(pos, pos + 1) + this.cmd.substring(pos - 1, pos) + this.cmd.substring(pos + 1);
      this.cursorPos = pos + 1;
    }
  }

  transposeWords() {
    var p1 = this.skipWordBackward(this.cursorPos);
    var p2 = this.skipWordForward(p1);
    var p4 = this.skipWordForward(this.cursorPos);
    var p3 = this.skipWordBackward(p4);

    if(p1 < p2 && p2 <= this.cursorPos && this.cursorPos <= p3 && p3 < p4) {
      this.cmd = this.cmd.substring(0, p1) + this.cmd.substring(p3, p4) + this.cmd.substring(p2, p3) + this.cmd.substring(p1, p2);
      this.cursorPos = p4;
    }
  }

  upcaseWord() {
    var end = this.skipWordForward(this.cursorPos);
    this.cmd = this.cmd.substring(0, this.cursorPos) + this.cmd.substring(this.cursorPos, end).toUpperCase() + this.cmd.substring(end);
  }

  update() {
    let i,
      out = this.out,
      cmdLen = null,
      cmdLine = this.cmd,
      colorize = this.showColors;

    if(!out) return;

    if(searchState) {
      const { history } = this;
      const re = new RegExp((this.searchPattern = cmdLine.replace(/([\[\]\(\)\?\+\*])/g, '\\$1')), 'i');
      const num = searchState > 0 ? searchState - 1 : searchState;
      //this.searchIndex = this.history.findLastIndex(c => re.test(c) && --num == 0);
      let historySearch = rotateLeft([...history.entries()], this.historyIndex);

      this.searchMatches.splice(0, this.searchMatches.length, ...historySearch.filter(([i, c]) => re.test(c)));

      //num = searchState > 0 ? searchState - 1 : searchState;
      const match = this.searchMatches[num < 0 ? this.searchMatches.length + num : num];

      // onsole.log("this.searchMatches", this.searchMatches.length, num, match);

      const [histidx = -1, histcmd = ''] = match || [];
      const histdir = searchState > 0 ? 'forward' : 'reverse';
      const histpos = searchState < 0 ? historySearch.indexOf(match) - historySearch.length : historySearch.indexOf(match);
      this.searchIndex = histidx;
      let lineStart = `(${histdir}-searchState[${histpos}])\``;
      cmdLine = `${lineStart}${this.cmd}': ${histcmd}`;
      colorize = false;
      const start = cmdLine.length - histcmd.length - 3 - this.cmd.length + this.cursorPos;
      let r = cmdLine.substring(start);

      setCursorPosition(1, 0);
      out.puts(cmdLine);
      clearScreen(1, 0);

      this.lastCmd = cmdLine;
      termCursorX = start;
      this.lastCursorPos = this.cursorPos;
      let nback = ucsLength(r);
      out.flush();
      if(isNaN(nback)) throw new Error(`update nback=${nback}`);
      cursorLeft(nback);
      //this.moveCursor(-ucsLength(r));
      out.flush();
      return;

      /* this.cursorPos is the position in 16 bit characters inside the UTF-16 string 'this.cmd' */
    } else if(this.cmd != this.lastCmd) {
      if(!this.showColors && this.lastCmd.substring(0, this.lastCursorPos) == this.cmd.substring(0, this.lastCursorPos)) {
        /* optimize common case */
        out.puts(this.cmd.substring(this.lastCursorPos));
      } else {
        /* goto the start of the line */
        this.moveCursor(-ucsLength(this.lastCmd.substring(0, this.lastCursorPos)));
        if(this.showColors) {
          var str = this.mexpr ? this.mexpr + '\n' + this.cmd : this.cmd;
          var start = str.length - this.cmd.length;
          var colorstate = this.colorizeJs(str);
          printColorText(str, start, colorstate[2]);
        } else {
          out.puts(this.cmd);
        }
      }
      termCursorX = (termCursorX + ucsLength(this.cmd)) % this.columns;
      if(termCursorX == 0) {
        /* show the cursor on the next line */
        out.puts(' \x08');
      }
      /* remove the trailing characters */
      clearScreen(1, 0);

      this.lastCmd = this.cmd;
      this.lastCursorPos = this.cmd.length;
    }

    out.flush();

    if(this.cursorPos > this.lastCursorPos) {
      this.moveCursor(ucsLength(this.cmd.substring(this.lastCursorPos, this.cursorPos)));
    } else if(this.cursorPos < this.lastCursorPos) {
      this.moveCursor(-ucsLength(this.cmd.substring(this.cursorPos, this.lastCursorPos)));
    }
    this.lastCursorPos = this.cursorPos;
  }

  yank() {
    this.insert(clipBoard);
  }

  escape() {}
}

Object.assign(REPL.prototype, {
  [Symbol.toStringTag]: 'REPL',
  handleInput: REPL.prototype.evalAndPrint,
  directives: loadModule
    ? {
        i: [
          function i(m) {
            try {
              loadModule(m);

              this.modules.add(m);
              this.modulesSave();
            } catch(error) {
              console.log('ERROR loading module: ' + error.message + '\n' + error.stack);
              return;
            }
          },
          'import a module'
        ]
      }
    : {}
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
  var len,
    c,
    i,
    strLen = str.length;
  len = 0;
  /* we never count the trailing surrogate to have the
       following property: ucsLength(str) =
       ucsLength(str.substring(0, a)) + ucsLength(str.substring(a,
       str.length)) for 0 <= a <= str.length */
  for(i = 0; i < strLen; i++) {
    c = str.charCodeAt(i);
    if(c < 0xdc00 || c >= 0xe000) len++;
  }
  return len;
}

function rotateLeft(array, n) {
  array.push(...array.splice(0, n));
  return array;
}

function isTrailingSurrogate(c) {
  var d = null;
  if(typeof c !== 'string') return false;
  d = c.codePointAt(0); /* can be NaN if empty string */
  return d >= 0xdc00 && d < 0xe000;
}

function isBalanced(a, b) {
  switch (a + b) {
    case '()':
    case '[]':
    case '{}':
      return true;
  }
  return false;
}

function printColorText(str, start, styleNames) {
  var i,
    j = null;
  for(j = start; j < str.length; ) {
    var style = styleNames[(i = j)];
    while(++j < str.length && styleNames[j] == style) continue;
    puts(colors[styles[style] || 'default']);
    puts(str.substring(i, j));
    puts(colors['none']);
  }
}

/*function printCsi(n, code) {
  const out = this?.out ?? stdout;
  if(typeof code == 'function') throw new Error(`code: ${code}`);
  if(typeof n == 'function') throw new Error(`n: ${n}`);
  if(Array.isArray(n)) n = n.join(';');

  out.puts('\x1b[' + (n != 1 ? n : '') + code);
  out.flush();
}*/

function printPaged(str, numLines = Infinity) {
  let lines = str.split('\n');
  let totalLines = lines.length;

  if(lines[lines.length - 1] === '') lines.pop();

  while(lines.length >= numLines) {
    let pos = totalLines - lines.length;
    let list = lines.splice(0, numLines - 1);
    let s = list.join('\n') + '\n';
    /*  let color = lastColor(s);
    if(color) s += colors.none;*/

    stdout.puts(s);
    s = `(${pos}-${pos + list.length - 1}/${totalLines}) more?`;
    stdout.puts(colors.brightRed + s + colors.none);
    stdout.flush();
    this.cursorPos = s.length;

    let key = waitkey();

    this.readlineRemovePrompt();

    if(key < 0) return;

    //if(color) printCsi(color, 'm');

    if(key > 0) break;
  }

  let s = lines.join('\n') + '\n';
  /* let color = lastColor(s);
  if(color) s += colors.none;*/
  stdout.puts(s);
  stdout.flush();

  function getbyte() {
    let u8 = new Uint8Array(1);
    let r = read(0, u8.buffer, 0, u8.length);
    if(r > 0) return u8[0];
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

/*function lastColor(str) {
  let lastColor;
  for(let [seq] of str.matchAll(/\x1b\[[\d;]*m/g)) {
    let codes = [...seq.matchAll(/\d+/g)].map(m => +m[0]);
    lastColor = codes[codes.length - 1] !== 0 ? codes : undefined;
  }
  return lastColor;
}*/

//REPL.prototype.printCsi = printCsi;

function saveFile(filename, data) {
  let f, r;

  if((f = fopen(filename, 'w+'))) {
    f.puts(data);
    r = f.tell();
    f.close();
  }
  return r;
}

function observeProperties(target = {}, obj, fn = (prop, value) => {}, opts = {}) {
  opts = { enumerable: false, ...opts };
  for(let prop of Object.getOwnPropertyNames(obj)) {
    Object.defineProperty(target, prop, {
      ...opts,
      get: () => obj[prop],
      set: value => {
        let r = fn(prop, value, obj);
        if(r !== false) obj[prop] = value;
        if(typeof r == 'function') r(prop, value, obj);
      }
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
    enumerable: true
  });
}

function waitRead(fd) {
  return new Promise(resolve => setReadHandler(fd, () => (setReadHandler(fd, null), resolve(fd))));
}

function define(obj, ...args) {
  for(let props of args) {
    let desc = Object.getOwnPropertyDescriptors(props);
    for(let prop in desc) {
      const { value } = desc[prop];
      if(typeof value == 'function') desc[prop].writable = false;
    }
    Object.defineProperties(obj, desc);
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
  for(let [modifier, name] of [...moduleName.matchAll(/([*!,]?)([^*!,]+),?/g)].map(m => m.slice(1))) {
    let module,
      base = basename(name, extname(name));

    try {
      module = await import(name);
    } catch(error) {
      console.log('loadModule ERROR', error.message);
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
    console.log('REPLServer', options);

    this.#eval = options.eval;
    this.#repl = new REPL(options.prompt);

    console.log('REPLServer', this.#repl);

    this.#repl.run();
  }

  defineCommand(keyword, { help, action }) {
    this.#repl.directives[keyword] = [action, help];
  }

  displayPrompt(preserveCursor) {
    this.#repl.readlinePrintPrompt();
  }

  clearBufferedCommand() {
    this.#repl.readlineStart();
  }

  setupHistory(historyPath, callback) {
    this.#repl.historyLoad(historyPath);
  }
}
