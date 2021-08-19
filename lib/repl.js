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
'use strip';

import * as std from 'std';
import * as os from 'os';
//import * as fs from './fs.js';
import inspect from 'inspect';
import path from 'path';
import * as Terminal from './terminal.js';

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

var fs;

/* XXX: use preprocessor ? */
var config_numcalc = typeof os.open === 'undefined';
var has_jscalc = typeof Fraction === 'function';
var has_bignum = typeof BigFloat === 'function';

var colors = {
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
  bright_red: '\x1b[31;1m',
  bright_green: '\x1b[32;1m',
  bright_yellow: '\x1b[33;1m',
  bright_blue: '\x1b[34;1m',
  bright_magenta: '\x1b[35;1m',
  bright_cyan: '\x1b[36;1m',
  bright_white: '\x1b[37;1m'
};

var readline_keys = null;
var readline_state = null;
var readline_cb = null;

var hex_mode = false;
var eval_mode = 'std';

var styles = {
  default: 'bright_green',
  comment: 'bright_green',
  string: 'bright_cyan',
  regex: 'bright_magenta',
  number: 'green',
  keyword: 'bright_red',
  function: 'bright_yellow',
  type: 'bright_red',
  identifier: 'bright_yellow',
  error: 'red',
  result: 'white',
  error_msg: 'bright_red'
};

var history = [];
var clip_board = '';
var prec = null;
var expBits = null;
var log2_10 = null;

var utf8_state = 0;
var utf8_val = 0;

var term_fd = null;
var term_read_buf = null;
var term_width = null;
/* current X position of the cursor in the terminal */
var term_cursor_x = 0;

var tty_set_raw = Util.once(fd => os.ttySetRaw(fd));
var put_banner = Util.once(() => std.puts('QuickJS - Type "\\h" for this.help\n'));

export class REPL {
  pstate = '';
  prompt = '';
  plen = 0;
  ps1 = '> ';
  ps2 = '  ... ';
  utf8 = true;
  show_time = false;
  show_colors = true;
  eval_time = 0;

  mexpr = '';
  level = 0;
  cmd = '';
  cursor_pos = 0;
  last_cmd = '';
  last_cursor_pos = 0;
  history_index = null;
  this_fun = null;
  last_fun = null;
  quote_flag = false;
  out = std.out;

  constructor(name, show_banner = true) {
    if('fs' in globalThis) this.fs = globalThis.fs;

    this.commands = define(
      {},
      {
        /* command table */ '\x01': this.beginningOfLine /* ^A - bol */,
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
        '\x0e': this.nextHistory /* ^N - down */,
        '\x10': this.previousHistory /* ^P - up */,
        '\x11': this.quotedInsert /* ^Q - quoted-this.insert */,
        '\x12': this.alert /* ^R - reverse-search */,
        '\x13': this.alert /* ^S - search */,
        '\x14': this.transposeChars /* ^T - transpose */,
        '\x18': this.reset /* ^X - cancel */,
        '\x19': this.yank /* ^Y - this.yank */,
        '\x1bOA': this.previousHistory /* ^[OA - up */,
        '\x1bOB': this.nextHistory /* ^[OB - down */,
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
        '\x1b[A': this.previousHistory /* ^[[A - up */,
        '\x1b[B': this.nextHistory /* ^[[B - down */,
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
      }
    );

    if(typeof name == 'string') this.ps1 = name + this.ps1;

    this.termInit();

    this.cmdStart(show_banner);
  }

  abort() {
    this.cmd = '';
    this.cursor_pos = 0;
    return -2;
  }

  acceptLine() {
    std.puts('\n');
    this.historyAdd(this.cmd);
    return -1;
  }

  alert() {}

  backwardChar() {
    if(this.cursor_pos > 0) {
      this.cursor_pos--;
      while(isTrailingSurrogate(this.cmd.charAt(this.cursor_pos))) this.cursor_pos--;
    }
  }

  backwardDeleteChar() {
    this.deleteCharDir(-1);
  }

  backwardKillLine() {
    this.killRegion(0, this.cursor_pos, -1);
  }

  backwardKillWord() {
    this.killRegion(this.skipWordBackward(this.cursor_pos), this.cursor_pos, -1);
  }

  backwardWord() {
    this.cursor_pos = this.skipWordBackward(this.cursor_pos);
  }

  beginningOfLine() {
    this.cursor_pos = 0;
  }

  bigfloatToString(a, radix) {
    var s = null;
    if(!BigFloat.isFinite(a)) {
      /* NaN, Infinite */
      if(eval_mode !== 'math') {
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
      if(typeof a === 'bigfloat' && eval_mode !== 'math') {
        s += 'l';
      } else if(
        eval_mode !== 'std' &&
        s.indexOf('.') < 0 &&
        ((radix == 16 && s.indexOf('p') < 0) || (radix == 10 && s.indexOf('e') < 0))
      ) {
        /* add a decimal point so that the floating point type
                   is visible */
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
    if(eval_mode === 'std') s += 'n';
    return s;
  }

  cmdReadlineStart() {
    this.readlineStart(this.dupstr('    ', this.level), this.readlineHandleCmd);
  }

  cmdStart(show_banner = true) {
    if(show_banner) put_banner();

    if(has_bignum) {
      log2_10 = Math.log(10) / Math.log(2);
      prec = 113;
      expBits = 15;
      if(has_jscalc) {
        eval_mode = 'math';
        /* XXX: numeric mode should always be the default ? */
        globalThis.algebraicMode = config_numcalc;
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
      can_regex = 1;
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

    var js_keywords =
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

    var js_no_regex = '|this|super|undefined|null|true|false|Infinity|NaN|arguments|';
    var js_types = '|void|var|';

    function parseIdentifier() {
      can_regex = 1;

      while(i < n && isWord(str[i])) i++;

      var w = '|' + str.substring(start, i) + '|';

      if(js_keywords.indexOf(w) >= 0) {
        style = 'keyword';
        if(js_no_regex.indexOf(w) >= 0) can_regex = 0;
        return;
      }

      var i1 = i;
      while(i1 < n && str[i1] == ' ') i1++;

      if(i1 < n && str[i1] == '(') {
        style = 'function';
        return;
      }

      if(js_types.indexOf(w) >= 0) {
        style = 'type';
        return;
      }

      style = 'identifier';
      can_regex = 0;
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
          can_regex = 1;
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
          if(can_regex) {
            parseRegex();
            can_regex = 0;
            break;
          }
          can_regex = 1;
          continue;
        case "'":
        case '"':
        case '`':
          parseString(c);
          can_regex = 0;
          break;
        case '(':
        case '[':
        case '{':
          can_regex = 1;
          level++;
          pushState(c);
          continue;
        case ')':
        case ']':
        case '}':
          can_regex = 0;
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
            can_regex = 0;
            break;
          }
          if(isWord(c) || c == '$') {
            parseIdentifier();
            break;
          }
          can_regex = 1;
          continue;
      }
      if(style) setStyle(start, i);
    }
    setStyle(n, n);
    return [state, level, r];
  }

  completion() {
    var tab,
      res,
      s,
      i,
      j,
      len,
      t,
      max_width,
      col,
      n_cols,
      row,
      n_rows = null;
    res = this.getCompletions(this.cmd, this.cursor_pos);
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
    if(this.last_fun === this.completion && tab.length == 1) {
      /* append parentheses to function names */
      var m = res.ctx[tab[0]];
      if(typeof m == 'function') {
        this.insert('(');
        if(m.length == 0) this.insert(')');
      } else if(typeof m == 'object') {
        this.insert('.');
      }
    }
    /* show the possible completions */
    if(this.last_fun === this.completion && tab.length >= 2) {
      max_width = 0;
      for(i = 0; i < tab.length; i++) max_width = Math.max(max_width, tab[i].length);
      max_width += 2;
      n_cols = Math.max(1, Math.floor((term_width + 1) / max_width));
      n_rows = Math.ceil(tab.length / n_cols);
      std.puts('\n');
      /* display the sorted list column-wise */
      for(row = 0; row < n_rows; row++) {
        for(col = 0; col < n_cols; col++) {
          i = col * n_rows + row;
          if(i >= tab.length) break;
          s = tab[i];
          if(col != n_cols - 1) s = s.padEnd(max_width);
          std.puts(s);
        }
        std.puts('\n');
      }
      /* show a new this.prompt */
      this.readlinePrintPrompt();
    }
  }

  controlC() {
    std.puts('\x1b[12i');
    if(this.last_fun === this.controlC) {
      std.puts('\n');
      if(typeof this.cleanup == 'function') this.cleanup();
      std.exit(0);
    } else {
      std.puts('\n(Press Ctrl-C again to quit)\n');
      this.readlinePrintPrompt();
    }
  }

  controlD() {
    if(this.cmd.length == 0) {
      std.puts('\n');
      return -3; /* exit read eval this.print loop */
    } else {
      this.deleteCharDir(1);
    }
  }

  deleteChar() {
    this.deleteCharDir(1);
  }

  deleteCharDir(dir) {
    var start,
      end = null;

    start = this.cursor_pos;
    if(dir < 0) {
      start--;
      while(isTrailingSurrogate(this.cmd.charAt(start))) start--;
    }
    end = start + 1;
    while(isTrailingSurrogate(this.cmd.charAt(end))) end++;

    if(start >= 0 && start < this.cmd.length) {
      if(this.last_fun === this.killRegion) {
        this.killRegion(start, end, dir);
      } else {
        this.cmd = this.cmd.substring(0, start) + this.cmd.substring(end);
        this.cursor_pos = start;
      }
    }
  }

  downcaseWord() {
    var end = this.skipWordForward(this.cursor_pos);
    this.cmd =
      this.cmd.substring(0, this.cursor_pos) +
      this.cmd.substring(this.cursor_pos, end).toLowerCase() +
      this.cmd.substring(end);
  }

  dupstr(str, count) {
    var res = '';
    while(count-- > 0) res += str;
    return res;
  }

  endOfLine() {
    this.cursor_pos = this.cmd.length;
  }

  readlineRemovePrompt() {
    printCsi(1, 'K');
    printCsi(1, 'G');
  }

  evalAndPrint(expr) {
    var result = null;
    const inspectOptions = /*console.options || */ {
      customInspect: true,
      compact: 1,
      depth: 10,
      hideKeys: ['loc', 'range']
    };
    try {
      if(eval_mode === 'math') expr = '"use math"; void 0;' + expr;
      var now = new Date().getTime();
      /* eval as a script */
      result = std.evalScript(expr, { backtrace_barrier: true });
      this.eval_time = new Date().getTime() - now;
      this.printStatus(
        colors[styles.result] +
          (typeof result == 'string' ? result : inspect(result, inspectOptions)) +
          colors.none,
        false
      );
      /* set the last result */
      globalThis._ = result;
    } catch(error) {
      let output = '';
      globalThis.lastError = error;
      output += colors[styles.error_msg];
      if(error instanceof Error) {
        output += 'Error: ';

        output += error.message;

        if(error.stack) output += error.stack;
      } else {
        output += 'Throw: ';

        if(error?.message) output += error.message;
      }
      output = output.trimEnd();

      output += colors.none;
      output += '\n';
      this.printStatus(output, false);
    }
  }

  extractDirective(a) {
    var pos = null;
    if(a[0] !== '\\') return '';
    for(pos = 1; pos < a.length; pos++) {
      if(!isAlpha(a[pos])) break;
    }
    return a.substring(1, pos);
  }

  forwardChar() {
    if(this.cursor_pos < this.cmd.length) {
      this.cursor_pos++;
      while(isTrailingSurrogate(this.cmd.charAt(this.cursor_pos))) this.cursor_pos++;
    }
  }

  forwardWord() {
    this.cursor_pos = this.skipWordForward(this.cursor_pos);
  }

  getCompletions(line, pos) {
    var s,
      obj,
      ctx_obj,
      r,
      i,
      j,
      paren = null;

    s = this.getContextWord(line, pos);
    ctx_obj = this.getContextObject(line, pos - s.length);
    r = [];
    /* enumerate properties from object and its prototype chain,
           add non-numeric regular properties with s as e prefix
         */
    for(i = 0, obj = ctx_obj; i < 10 && obj !== null && obj !== void 0; i++) {
      var props = Object.getOwnPropertyNames(obj);
      /* add non-numeric regular properties */
      for(j = 0; j < props.length; j++) {
        var prop = props[j];
        if(typeof prop == 'string' && '' + +prop != prop && prop.startsWith(s)) r.push(prop);
      }
      obj = Object.getPrototypeOf(obj);
    }
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
    /* 'tab' = list of completions, 'pos' = cursor position inside
           the completions */
    return { tab: r, pos: s.length, ctx: ctx_obj };
  }

  getContextObject(line, pos) {
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
            if(['true', 'false', 'null', 'this'].includes(base) || !isNaN(+base))
              return eval(base);
            obj = this.getContextObject(line, pos - base.length);
            if(obj === null || obj === void 0) return obj;
            if(obj === globalThis && obj[base] === void 0) return eval(base);
            else return obj[base];
          }
          return {};
      }
    }
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
      switch (readline_state) {
        case 0:
          if(c == '\x1b') {
            /* '^[' - ESC */
            readline_keys = c;
            readline_state = 1;
          } else {
            this.handleKey(c);
          }
          break;

        case 1 /* '^[ */:
          readline_keys += c;
          if(c == '[') {
            readline_state = 2;
          } else if(c == 'O') {
            readline_state = 3;
          } else {
            this.handleKey(readline_keys);
            readline_state = 0;
          }
          break;

        case 2 /* '^[[' - CSI */:
          readline_keys += c;
          if(c == '<') {
            readline_state = 4;
          } else if(!(c == ';' || (c >= '0' && c <= '9'))) {
            this.handleKey(readline_keys);
            readline_state = 0;
          }
          break;

        case 3 /* '^[O' - ESC2 */:
          readline_keys += c;
          this.handleKey(readline_keys);
          readline_state = 0;
          break;

        case 4:
          if(!(c == ';' || (c >= '0' && c <= '9') || c == 'M' || c == 'm')) {
            this.handleMouse(readline_keys);
            readline_state = 0;
            readline_keys = '';
            continue;
          } else {
            readline_keys += c;
          }
          break;
      }
      break;
    }
  }

  handleMouse(keys) {
    this.debug('handleMouse', { keys });

    const [button, x, y, cmd] = [...Util.matchAll(/([0-9]+|[A-Za-z]+)/g, keys)]
      .map(p => p[1])
      .map(p => (!isNaN(+p) ? +p : p));
    let press = cmd == 'm';

    this.debug('handleMouse', { button, x, y, press });
  }

  handleCmd(expr) {
    var colorstate,
      cmd = null;

    if(expr === null) {
      expr = '';
      return;
    }
    if(expr === '?') {
      this.help();
      return;
    }
    cmd = this.extractDirective(expr);
    if(cmd.length > 0) {
      if(!this.handleDirective(cmd, expr)) return;
      expr = expr.substring(cmd.length + 1);
    }
    if(expr === '') return;

    if(this.mexpr) expr = this.mexpr + '\n' + expr;
    colorstate = this.colorizeJs(expr);
    this.pstate = colorstate[0];
    this.level = colorstate[1];
    if(this.pstate) {
      this.mexpr = expr;
      return;
    }
    this.mexpr = '';

    if(has_bignum) {
      BigFloatEnv.setPrec(this.evalAndPrint.bind(this, expr), prec, expBits);
    } else {
      this.evalAndPrint(expr);
    }
    this.level = 0;

    /* run the garbage collector after each command */
    std.gc();
  }

  handleDirective(cmd, expr) {
    var param,
      prec1,
      expBits1 = null;

    if(this.cmd === 'h' || this.cmd === '?' || this.cmd == 'this.help') {
      this.help();
    } else if(this.cmd === 'load') {
      var filename = expr.substring(cmd.length + 1).trim();
      if(filename.lastIndexOf('.') <= filename.lastIndexOf('/')) filename += '.js';
      std.loadScript(filename);
      return false;
    } else if(this.cmd === 'x') {
      hex_mode = true;
    } else if(this.cmd === 'd') {
      hex_mode = false;
    } else if(this.cmd === 't') {
      this.show_time = !this.show_time;
    } else if(has_bignum && this.cmd === 'p') {
      param = expr
        .substring(this.cmd.length + 1)
        .trim()
        .split(' ');
      if(param.length === 1 && param[0] === '') {
        std.puts(
          'BigFloat precision=' +
            prec +
            ' bits (~' +
            Math.floor(prec / log2_10) +
            ' digits), exponent size=' +
            expBits +
            ' bits\n'
        );
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
          std.puts('Invalid precision\n');
          return false;
        }
        if(
          Number.isNaN(expBits1) ||
          expBits1 < BigFloatEnv.expBitsMin ||
          expBits1 > BigFloatEnv.expBitsMax
        ) {
          std.puts('Invalid exponent bits\n');
          return false;
        }
        prec = prec1;
        expBits = expBits1;
      }
      return false;
    } else if(has_bignum && this.cmd === 'digits') {
      param = expr.substring(this.cmd.length + 1).trim();
      prec1 = Math.ceil(parseFloat(param) * log2_10);
      if(prec1 < BigFloatEnv.precMin || prec1 > BigFloatEnv.precMax) {
        std.puts('Invalid precision\n');
        return false;
      }
      prec = prec1;
      expBits = BigFloatEnv.expBitsMax;
      return false;
    } else if(has_bignum && this.cmd === 'mode') {
      param = expr.substring(this.cmd.length + 1).trim();
      if(param === '') {
        std.puts('Running mode=' + eval_mode + '\n');
      } else if(param === 'std' || param === 'math') {
        eval_mode = param;
      } else {
        std.puts('Invalid mode\n');
      }
      return false;
    } else if(this.cmd === 'clear') {
      std.puts('\x1b[H\x1b[J');
    } else if(this.cmd === 'q') {
      std.exit(0);
    } else if(has_jscalc && this.cmd === 'a') {
      algebraicMode = true;
    } else if(has_jscalc && this.cmd === 'n') {
      algebraicMode = false;
    } else {
      std.puts('Unknown directive: ' + this.cmd + '\n');
      return false;
    }
    return true;
  }

  handleKey(keys) {
    var fun = null;

    if(this.quote_flag) {
      if(ucsLength(keys) === 1) this.insert(keys);
      this.quote_flag = false;
    } else if((fun = this.commands[keys])) {
      this.this_fun = fun;
      switch (fun.call(this, keys)) {
        case -1:
          readline_cb.call(this, this.cmd);
          return;
        case -2:
          readline_cb.call(this, null);
          return;
        case -3:
          /* uninstall a Ctrl-C signal handler */
          os.signal(os.SIGINT, null);
          /* uninstall the stdin read handler */
          os.setReadHandler(term_fd, null);
          return;
      }
      this.last_fun = this.this_fun;
    } else if(ucsLength(keys) === 1 && keys >= ' ') {
      this.insert(keys);
      this.last_fun = this.insert;
    } else {
      this.alert(); /* beep! */
    }

    this.cursor_pos =
      this.cursor_pos < 0
        ? 0
        : this.cursor_pos > this.cmd.length
        ? this.cmd.length
        : this.cursor_pos;
    this.update();
  }

  help() {
    function sel(n) {
      return n ? '*' : ' ';
    }
    std.puts(
      '\\h          this this.help\n' +
        '\\x         ' +
        this.sel(hex_mode) +
        'hexadecimal number display\n' +
        '\\d         ' +
        this.sel(!hex_mode) +
        'decimal number display\n' +
        '\\t         ' +
        this.sel(this.show_time) +
        'toggle timing display\n' +
        '\\clear      clear the terminal\n'
    );
    if(has_jscalc) {
      std.puts(
        '\\a         ' +
          this.sel(algebraicMode) +
          'algebraic mode\n' +
          '\\n         ' +
          this.sel(!algebraicMode) +
          'numeric mode\n'
      );
    }
    if(has_bignum) {
      std.puts(
        "\\p [m [e]]  set the BigFloat precision to 'm' bits\n" +
          "\\digits n   set the BigFloat precision to 'ceil(n*log2(10))' bits\n"
      );
      if(!has_jscalc) {
        std.puts('\\mode [std|math] change the running mode (current = ' + eval_mode + ')\n');
      }
    }
    if(!config_numcalc) {
      std.puts('\\q          exit\n');
    }
  }

  historyFile() {
    const { argv } = process;
    let me = argv[1] ?? argv[0];
    let base = path.basename(me, path.extname(me));
    let home = path.gethome();
    return `${home}/.${base}_history`;
  }

  historyLoad(filename, fail = true) {
    filename ??= this.historyFile();

    /*console.log(`repl.historyLoad`, { filename, fail }, this.fs.existsSync);
     if(!fs.existsSync(filename)) {
      if(fail) throw new Error(`REPL.historyLoad no such filename: ${filename}`);
      return;
    }*/
    const data = this.fs.readFileSync(filename, 'utf-8');
    const lines = data.split(/\n/g);
    history.splice(0, history.length, ...lines);
    return (this.history_index = history.length);
  }

  historySave(filename) {
    filename ??= this.historyFile();
    const lines = history
      .filter(line => (line + '').trim() != '')
      .map(line => line.replace(/\n/g, '\\n'));
    const data = lines.join('\n') + '\n';
    let r = this.fs.writeFileSync(filename, data);
    console.log(`Wrote ${r} bytes to '${filename}'.`);
    return lines.length;
  }

  historyClear() {
    history.splice(0, history.length);
    this.history_index = history.length;
  }

  historyAdd(str) {
    const last = history[history.length - 1];

    if(str && str !== last) history.push(str);

    return (this.history_index = history.length);
  }

  historySearch(dir) {
    var pos = this.cursor_pos;
    for(var i = 1; i <= history.length; i++) {
      var index = (history.length + i * dir + this.history_index) % history.length;
      if(history[index].substring(0, pos) == this.cmd.substring(0, pos)) {
        this.history_index = index;
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

  historySet(entries) {
    if(entries) history.splice(0, history.length, ...entries);
  }

  get history() {
    return history;
  }

  historyPos() {
    return history_index;
  }

  insert(str) {
    if(str) {
      this.cmd = this.cmd.substring(0, this.cursor_pos) + str + this.cmd.substring(this.cursor_pos);
      this.cursor_pos += str.length;
    }
  }

  killLine() {
    this.killRegion(this.cursor_pos, this.cmd.length, 1);
  }

  killRegion(start, end, dir) {
    var s = this.cmd.substring(start, end);
    if(this.last_fun !== this.killRegion) clip_board = s;
    else if(dir < 0) clip_board = s + clip_board;
    else clip_board = clip_board + s;

    this.cmd = this.cmd.substring(0, start) + this.cmd.substring(end);
    if(this.cursor_pos > end) this.cursor_pos -= end - start;
    else if(this.cursor_pos > start) this.cursor_pos = start;
    this.this_fun = this.killRegion;
  }

  killWord() {
    this.killRegion(this.cursor_pos, this.skipWordForward(this.cursor_pos), 1);
  }

  moveCursor(delta) {
    var i,
      l = null;
    if(delta > 0) {
      while(delta != 0) {
        if(term_cursor_x == term_width - 1) {
          std.puts('\n'); /* translated to CRLF */
          term_cursor_x = 0;
          delta--;
        } else {
          l = Math.min(term_width - 1 - term_cursor_x, delta);
          printCsi(l, 'C'); /* right */
          delta -= l;
          term_cursor_x += l;
        }
      }
    } else {
      delta = -delta;
      while(delta != 0) {
        if(term_cursor_x == 0) {
          printCsi(1, 'A'); /* up */
          printCsi(term_width - 1, 'C'); /* right */
          delta--;
          term_cursor_x = term_width - 1;
        } else {
          l = Math.min(delta, term_cursor_x);
          printCsi(l, 'D'); /* left */
          delta -= l;
          term_cursor_x -= l;
        }
      }
    }
  }

  nextHistory() {
    if(this.history_index < history.length - 1) {
      this.history_index++;
      this.cmd = history[this.history_index];
      this.cursor_pos = this.cmd.length;
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

  previousHistory() {
    if(this.history_index > 0) {
      if(this.history_index == history.length) {
        history.push(this.cmd);
      }
      this.history_index--;
      this.cmd = history[this.history_index];
      this.cursor_pos = this.cmd.length;
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
          std.puts(a);
        } else if(stack.indexOf(a) >= 0) {
          std.puts('[circular]');
        } else if(
          has_jscalc &&
          (a instanceof Fraction ||
            a instanceof Complex ||
            a instanceof Mod ||
            a instanceof Polynomial ||
            a instanceof PolyMod ||
            a instanceof RationalFunction ||
            a instanceof Series)
        ) {
          std.puts(a.toString());
        } else {
          stack.push(a);
          if(Array.isArray(a)) {
            n = a.length;
            std.puts('[ ');
            for(i = 0; i < n; i++) {
              if(i !== 0) std.puts(', ');
              if(i in a) {
                this.printRec(a[i]);
              } else {
                std.puts('<empty>');
              }
              if(i > 20) {
                std.puts('...');
                break;
              }
            }
            std.puts(' ]');
          } else if(Object.__getClass(a) === 'RegExp') {
            std.puts(a.toString());
          } else {
            keys = Object.keys(a);
            n = keys.length;
            std.puts('{ ');
            for(i = 0; i < n; i++) {
              if(i !== 0) std.puts(', ');
              key = keys[i];
              std.puts(key, ': ');
              this.printRec(a[key]);
            }
            std.puts(' }');
          }
          stack.pop(a);
        }
      } else if(type === 'string') {
        s = a.__quote();
        if(s.length > 79) s = s.substring(0, 75) + '..."';
        std.puts(s);
      } else if(type === 'number') {
        std.puts(this.numberToString(a, hex_mode ? 16 : 10));
      } else if(type === 'bigint') {
        std.puts(this.bigintToString(a, hex_mode ? 16 : 10));
      } else if(type === 'bigfloat') {
        std.puts(this.bigfloatToString(a, hex_mode ? 16 : 10));
      } else if(type === 'bigdecimal') {
        std.puts(a.toString() + 'm');
      } else if(type === 'symbol') {
        std.puts(String(a));
      } else if(type === 'function') {
        std.puts('function ' + a.name + '()');
      } else {
        std.puts(a);
      }
    }
    this.printRec(a);
  }

  printStatus(fn, prompt = true) {
    const show = arg => this.out.puts(arg);

    if(Array.isArray(fn)) {
      const arr = fn;
      fn = arr.join('');
    } else if(typeof fn != 'function') {
      let str = fn + '';
      if(!str.endsWith('\n')) str += '\n';
      fn = () => show(str);
    }

    this.readlineRemovePrompt();
    printCsi(1, 'J');
    printCsi(1, 'G');
    fn();
    if(prompt) {
      /* printCsi(1, 'J');
      this.readlinePrintPrompt();*/
      this.out.flush();
    }
  }

  printFunction(logFn) {
    return (...args) => this.printStatus(() => logFn(...args));
  }

  quotedInsert() {
    this.quote_flag = true;
  }

  readlineHandleCmd(expr) {
    this.handleCmd(expr);
    this.cmdReadlineStart();
  }

  readlinePrintPrompt() {
    std.puts(this.prompt);
    term_cursor_x = ucsLength(this.prompt) % term_width;
    this.last_cmd = '';
    this.last_cursor_pos = 0;
  }

  readlineStart(defstr, cb) {
    this.cmd = defstr || '';
    this.cursor_pos = this.cmd.length;
    this.history_index = history.length;
    readline_cb = cb;

    this.prompt = this.pstate;

    if(this.mexpr) {
      this.prompt += this.dupstr(' ', this.plen - this.prompt.length);
      this.prompt += this.ps2;
    } else {
      if(this.show_time) {
        var t = Math.round(this.eval_time) + ' ';
        this.eval_time = 0;
        t = this.dupstr('0', 5 - t.length) + t;
        this.prompt += t.substring(0, t.length - 4) + '.' + t.substring(t.length - 4);
      }
      this.plen = this.prompt.length;
      this.prompt += this.ps1;
    }
    this.readlinePrintPrompt();
    this.update();
    readline_state = 0;
  }

  reset() {
    this.cmd = '';
    this.cursor_pos = 0;
  }

  async run() {
    this.debug('run');
    if(!console.options) console.options = {};
    await this.termInit();
    this.cmdStart(true);
    this.running = true;
    do {
      await this.fs.waitRead(term_fd);
      await this.termReadHandler();
    } while(this.running);
  }

  runSync() {
    //this.cmdStart(name);

    os.setReadHandler(std.in.fileno(), () => this.termReadHandler());
    return this;
  }

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
    term_fd = std.in.fileno();

    /* get the terminal size */
    term_width = 80;
    if(os.isatty(term_fd)) {
      if(os.ttyGetWinSize) {
        tab = os.ttyGetWinSize(term_fd);
        if(tab) term_width = tab[0];
      }
      /* set the TTY to raw mode */
      tty_set_raw(term_fd);
    }

    Terminal.mousetrackingEnable();

    /* install a Ctrl-C signal handler */
    os.signal(os.SIGINT, arg => this.sigintHandler(arg));

    /* install a handler to read stdin */
    term_read_buf = new Uint8Array(64);
    os.setReadHandler(term_fd, () => this.termReadHandler());
  }

  termReadHandler() {
    var l,
      i = null;
    l = os.read(term_fd, term_read_buf.buffer, 0, term_read_buf.length);
    for(i = 0; i < l; i++) this.handleByte(term_read_buf[i]);
  }

  transposeChars() {
    var pos = this.cursor_pos;
    if(this.cmd.length > 1 && pos > 0) {
      if(pos == this.cmd.length) pos--;
      this.cmd =
        this.cmd.substring(0, pos - 1) +
        this.cmd.substring(pos, pos + 1) +
        this.cmd.substring(pos - 1, pos) +
        this.cmd.substring(pos + 1);
      this.cursor_pos = pos + 1;
    }
  }

  transposeWords() {
    var p1 = this.skipWordBackward(this.cursor_pos);
    var p2 = this.skipWordForward(p1);
    var p4 = this.skipWordForward(this.cursor_pos);
    var p3 = this.skipWordBackward(p4);

    if(p1 < p2 && p2 <= this.cursor_pos && this.cursor_pos <= p3 && p3 < p4) {
      this.cmd =
        this.cmd.substring(0, p1) +
        this.cmd.substring(p3, p4) +
        this.cmd.substring(p2, p3) +
        this.cmd.substring(p1, p2);
      this.cursor_pos = p4;
    }
  }

  upcaseWord() {
    var end = this.skipWordForward(this.cursor_pos);
    this.cmd =
      this.cmd.substring(0, this.cursor_pos) +
      this.cmd.substring(this.cursor_pos, end).toUpperCase() +
      this.cmd.substring(end);
  }

  update() {
    var i,
      cmd_len = null;
    /* this.cursor_pos is the position in 16 bit characters inside the
           UTF-16 string 'this.cmd' */
    if(this.cmd != this.last_cmd) {
      if(
        !this.show_colors &&
        this.last_cmd.substring(0, this.last_cursor_pos) ==
          this.cmd.substring(0, this.last_cursor_pos)
      ) {
        /* optimize common case */
        std.puts(this.cmd.substring(this.last_cursor_pos));
      } else {
        /* goto the start of the line */
        this.moveCursor(-ucsLength(this.last_cmd.substring(0, this.last_cursor_pos)));
        if(this.show_colors) {
          var str = this.mexpr ? this.mexpr + '\n' + this.cmd : this.cmd;
          var start = str.length - this.cmd.length;
          var colorstate = this.colorizeJs(str);
          printColorText(str, start, colorstate[2]);
        } else {
          std.puts(this.cmd);
        }
      }
      term_cursor_x = (term_cursor_x + ucsLength(this.cmd)) % term_width;
      if(term_cursor_x == 0) {
        /* show the cursor on the next line */
        std.puts(' \x08');
      }
      /* remove the trailing characters */
      std.puts('\x1b[J');
      this.last_cmd = this.cmd;
      this.last_cursor_pos = this.cmd.length;
    }
    if(this.cursor_pos > this.last_cursor_pos) {
      this.moveCursor(ucsLength(this.cmd.substring(this.last_cursor_pos, this.cursor_pos)));
    } else if(this.cursor_pos < this.last_cursor_pos) {
      this.moveCursor(-ucsLength(this.cmd.substring(this.cursor_pos, this.last_cursor_pos)));
    }
    this.last_cursor_pos = this.cursor_pos;
    this.out.flush();
  }

  yank() {
    this.insert(clip_board);
  }
}

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
    str_len = str.length;
  len = 0;
  /* we never count the trailing surrogate to have the
       following property: ucsLength(str) =
       ucsLength(str.substring(0, a)) + ucsLength(str.substring(a,
       str.length)) for 0 <= a <= str.length */
  for(i = 0; i < str_len; i++) {
    c = str.charCodeAt(i);
    if(c < 0xdc00 || c >= 0xe000) len++;
  }
  return len;
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

function printColorText(str, start, style_names) {
  var i,
    j = null;
  for(j = start; j < str.length; ) {
    var style = style_names[(i = j)];
    while(++j < str.length && style_names[j] == style) continue;
    std.puts(colors[styles[style] || 'default']);
    std.puts(str.substring(i, j));
    std.puts(colors['none']);
  }
}

function printCsi(n, code) {
  std.puts('\x1b[' + (n != 1 ? n : '') + code);
  std.out.flush();
}

REPL.prototype.printCsi = printCsi;

export default REPL;
