/*
 * QuickJS Read Eval Print Loop
 67 * Copyright (c) 2017-2020 Fabrice Bellard
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
import * as Terminal from './terminal.js';
import fs from 'fs';
import { isatty } from 'tty';
import { extendArray } from './lib/misc.js';

('use strip');

extendArray(Array.prototype);

export default function REPL(title = 'QuickJS') {
  /* add 'os' and 'std' bindings */
  /*globalThis.os = os;
  globalThis.std = std;*/
  const { std, os } = globalThis;

  /* close global objects */
  //const { Object, String, Array, Date, Math, isFinite, parseFloat } = globalThis;
  var thisObj = this;
  var output = fs.stdout;
  var input = fs.stdin;

  const puts = str => output.puts(str);
  const flush = () => output.flush();

  /*
  function puts(str) {
    fs.repl.puts(output, str);
  }
  
  function flush() {
   fs.repl.flush(output);
  }*/

  /* XXX: use preprocessor ? */
  var config_numcalc = false; //typeof os.open === 'undefined';
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

  var styles;
  if(config_numcalc) {
    styles = {
      default: 'black',
      comment: 'white',
      string: 'green',
      regex: 'cyan',
      number: 'green',
      keyword: 'blue',
      function: 'gray',
      type: 'bright_magenta',
      identifier: 'yellow',
      error: 'bright_red',
      result: 'black',
      error_msg: 'bright_red'
    };
  } else {
    styles = {
      default: 'bright_cyan',
      comment: 'bright_green',
      string: 'bright_cyan',
      regex: 'bright_magenta',
      number: 'bright_cyan',
      keyword: 'bright_red',
      function: 'bright_yellow',
      type: 'bright_red',
      identifier: 'bright_yellow',
      error: 'red',
      result: 'white',
      error_msg: 'bright_red'
    };
  }
  var repl = this instanceof REPL ? this : {};

  repl.puts = puts;
  repl.flush = flush;

  var running = true;
  var history = [];
  var clip_board = '';
  var prec;
  var expBits;
  var log2_10;

  var pstate = '';
  var prompt = '';
  var plen = 0;
  var ps1;
  if(config_numcalc) ps1 = '> ';
  else ps1 = `${title.replace(/-.*/g, '')} > `;
  var ps2 = '  ... ';
  var utf8 = true;
  var show_time = false;
  var show_colors = true;
  var eval_time = 0;

  var mexpr = '';
  var level = 0;
  //var cmd = '';
  var cursor_pos = 0;
  var last_cmd = '';
  var last_cursor_pos = 0;
  var history_index;
  var this_fun, last_fun;
  var quote_flag = false;
  var search = 0;
  var search_pattern = '';
  var search_index,
    search_matches = [];

  var utf8_state = 0;
  var utf8_val = 0;

  var term_read_buf;
  var term_width;
  /* current X position of the cursor in the terminal */
  var term_cursor_x = 0;

  var readline_keys;
  repl.readline_state = 0;
  var readline_cb;

  var hex_mode = false;
  var eval_mode = 'std';

  var commands = {
    /* command table */ '\x01': beginning_of_line /* ^A - bol */,
    '\x02': backward_char /* ^B - backward-char */,
    '\x03': control_c /* ^C - abort */,
    '\x04': control_d /* ^D - delete-char or exit */,
    '\x05': end_of_line /* ^E - eol */,
    '\x06': forward_char /* ^F - forward-char */,
    '\x07': abort /* ^G - bell */,
    '\x08': backward_delete_char /* ^H - backspace */,
    '\x09': completion /* ^I - history-search-backward */,
    '\x0a': accept_line /* ^J - newline */,
    '\x0b': kill_line /* ^K - delete to end of line */,
    '\x0d': accept_line /* ^M - enter */,
    '\x0e': history_next /* ^N - down */,
    '\x10': history_previous /* ^P - up */,
    '\x11': quoted_insert /* ^Q - quoted-insert */,
    '\x12': reverse_search /* ^R - reverse-search */,
    '\x13': forward_search /* ^S - search */,
    '\x14': transpose_chars /* ^T - transpose */,
    '\x18': reset /* ^X - cancel */,
    '\x19': yank /* ^Y - yank */,
    '\x1bOA': history_previous /* ^[OA - up */,
    '\x1bOB': history_next /* ^[OB - down */,
    '\x1bOC': forward_char /* ^[OC - right */,
    '\x1bOD': backward_char /* ^[OD - left */,
    '\x1bOF': forward_word /* ^[OF - ctrl-right */,
    '\x1bOH': backward_word /* ^[OH - ctrl-left */,
    '\x1b[1;5C': forward_word /* ^[[1;5C - ctrl-right */,
    '\x1b[1;5D': backward_word /* ^[[1;5D - ctrl-left */,
    '\x1b[1~': beginning_of_line /* ^[[1~ - bol */,
    '\x1b[3~': delete_char /* ^[[3~ - delete */,
    '\x1b[4~': end_of_line /* ^[[4~ - eol */,
    '\x1b[5~': history_search_backward /* ^[[5~ - page up */,
    '\x1b[6~': history_search_forward /* ^[[5~ - page down */,
    '\x1b[A': history_previous /* ^[[A - up */,
    '\x1b[B': history_next /* ^[[B - down */,
    '\x1b[C': forward_char /* ^[[C - right */,
    '\x1b[D': backward_char /* ^[[D - left */,
    '\x1b[F': end_of_line /* ^[[F - end */,
    '\x1b[H': beginning_of_line /* ^[[H - home */,
    '\x1b\x7f': backward_kill_word /* M-C-? - backward_kill_word */,
    '\x1bb': backward_word /* M-b - backward_word */,
    '\x1bd': kill_word /* M-d - kill_word */,
    '\x1bf': forward_word /* M-f - backward_word */,
    '\x1bk': backward_kill_line /* M-k - backward_kill_line */,
    '\x1bl': downcase_word /* M-l - downcase_word */,
    '\x1bt': transpose_words /* M-t - transpose_words */,
    '\x1bu': upcase_word /* M-u - upcase_word */,
    '\x7f': backward_delete_char /* ^? - delete */
  };

  let currentCommand = '';

  Object.defineProperties(repl, {
    cmd: {
      get() {
        return currentCommand;
      },
      set(value) {
        currentCommand = value;
      },
      enumerable: false
    },
    history: { value: history, enumerable: false }
  });

  async function term_init() {
    //   repl.debug("term_init");
    var tab;
    this.term_fd = input && input.fileno ? input.fileno() : 1;
    /* get the terminal size */
    term_width = 80;
    if(isatty(this.term_fd)) {
      if(Util.ttyGetWinSize) {
        await Util.ttyGetWinSize(1).then(tab => {
          repl.debug('term_init', { tab });
          term_width = tab[0];
        });
      }
      Util.ttySetRaw(this.term_fd);
      repl.debug('TTY setup done');
    }

    /* install a Ctrl-C signal handler */
    Util.signal('SIGINT', sigint_handler);

    /* install a handler to read stdin */
    term_read_buf = new Uint8Array(1);
    os.setReadHandler(this.term_fd, () => repl.term_read_handler());
    repl.debug('term_init');
    repl.debug('this.term_fd', this.term_fd);
  }

  function sigint_handler() {
    /* send Ctrl-C to readline */
    repl.handle_byte(3);
  }

  function term_read_handler() {
    var l, i;
    repl.debug('term_read_buf', term_read_buf);
    const { buffer } = term_read_buf;
    l = os.read(input, buffer, 0, 1);
    repl.debug('term_read_handler', { l, buffer });

    for(i = 0; i < l; i++) {
      repl.debug('term_read_handler', {
        code: term_read_buf[i],
        char: String.fromCharCode(term_read_buf[i])
      });
      repl.handle_byte(term_read_buf[i]);

      if(!running) break;
    }
  }

  function handle_byte(c) {
    repl.debug('handle_byte', { c, utf8 });
    if(!utf8) {
      repl.handle_char(c);
    } else if(utf8_state !== 0 && c >= 0x80 && c < 0xc0) {
      utf8_val = (utf8_val << 6) | (c & 0x3f);
      utf8_state--;
      if(utf8_state === 0) {
        repl.handle_char(utf8_val);
      }
    } else if(c >= 0xc0 && c < 0xf8) {
      utf8_state = 1 + (c >= 0xe0) + (c >= 0xf0);
      utf8_val = c & ((1 << (6 - utf8_state)) - 1);
    } else {
      utf8_state = 0;
      repl.handle_char(c);
    }
  }

  function is_alpha(c) {
    return typeof c === 'string' && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'));
  }

  function is_digit(c) {
    return typeof c === 'string' && c >= '0' && c <= '9';
  }

  function is_word(c) {
    return typeof c === 'string' && (repl.is_alpha(c) || repl.is_digit(c) || c == '_' || c == '$');
  }

  function ucs_length(str) {
    var len,
      c,
      i,
      str_len = str.length;
    len = 0;
    /* we never count the trailing surrogate to have the
         following property: repl.ucs_length(str) =
         repl.ucs_length(str.substring(0, a)) + repl.ucs_length(str.substring(a,
         str.length)) for 0 <= a <= str.length */
    for(i = 0; i < str_len; i++) {
      c = str.charCodeAt(i);
      if(c < 0xdc00 || c >= 0xe000) len++;
    }
    return len;
  }

  function is_trailing_surrogate(c) {
    var d;
    if(typeof c !== 'string') return false;
    d = c.codePointAt(0); /* can be NaN if empty string */
    return d >= 0xdc00 && d < 0xe000;
  }

  function is_balanced(a, b) {
    switch (a + b) {
      case '()':
      case '[]':
      case '{}':
        return true;
    }
    return false;
  }

  function print_color_text(str, start, style_names) {
    var i, j;
    for(j = start; j < str.length; ) {
      var style = style_names[(i = j)];
      while(++j < str.length && style_names[j] == style) continue;
      repl.puts(colors[styles[style] || 'default']);
      repl.puts(str.substring(i, j));
      repl.puts(colors['none']);
    }
  }

  function print_csi(n, code) {
    repl.puts('\x1b[' + (n != 1 ? n : '') + code);
  }

  /* XXX: handle double-width characters */
  function move_cursor(delta) {
    //if(isNaN(delta)) return;

    var i, l;
    if(delta > 0) {
      while(delta != 0) {
        if(term_cursor_x == term_width - 1) {
          repl.puts('\n'); /* translated to CRLF */
          term_cursor_x = 0;
          delta--;
        } else {
          l = Math.min(term_width - 1 - term_cursor_x, delta);
          repl.print_csi(l, 'C'); /* right */
          delta -= l;
          term_cursor_x += l;
        }
      }
    } else {
      delta = -delta;
      while(delta != 0) {
        if(term_cursor_x == 0) {
          repl.print_csi(1, 'A'); /* up */
          repl.print_csi(term_width - 1, 'C'); /* right */
          delta--;
          term_cursor_x = term_width - 1;
        } else {
          // repl.debug("move_cursor", {delta,term_cursor_x, term_width});
          l = Math.min(delta, term_cursor_x);

          if(isNaN(l)) throw new Error(`move_cursor l=${l}`);
          repl.print_csi(l, 'D'); /* left */
          delta -= l;
          term_cursor_x -= l;
        }
      }
    }
  }

  function update() {
    var i,
      cmd_len,
      cmd_line = repl.cmd,
      colorize = show_colors;

    if(search) {
      const re = new RegExp(
        (search_pattern = cmd_line.replace(/([\(\)\?\+\*])/g, '.' /*'\\$1'*/)),
        'i'
      );
      const num = search > 0 ? search - 1 : search;
      //search_index = history.findLastIndex(c => re.test(c) && --num == 0);
      let history_search = [...history.entries()].rotateLeft(history_index);
      search_matches.splice(
        0,
        search_matches.length,
        ...history_search.filter(([i, c]) => re.test(c))
      );
      //num = search > 0 ? search - 1 : search;
      const match = search_matches.at(num);
      const [histidx = -1, histcmd = ''] = match || [];
      const histdir = search > 0 ? 'forward' : 'reverse';
      const histpos =
        search < 0
          ? history_search.indexOf(match) - history_search.length
          : history_search.indexOf(match);
      search_index = histidx;
      let line_start = `(${histdir}-search[${histpos}])\``;
      cmd_line = `${line_start}${repl.cmd}': ${histcmd}`;
      colorize = false;
      const start = cmd_line.length - histcmd.length - 3 - repl.cmd.length + cursor_pos;
      let r = cmd_line.substring(start);

      repl.puts(`\x1b[1G`);
      repl.puts(cmd_line);
      repl.puts('\x1b[J');
      last_cmd = cmd_line;
      term_cursor_x = start;
      last_cursor_pos = cursor_pos;
      let nback = repl.ucs_length(r);
      if(isNaN(nback)) throw new Error(`update nback=${nback}`);
      repl.puts(`\x1b[${nback}D`);
      //repl.move_cursor(-repl.ucs_length(r));
      repl.flush();
      return;
    } /* cursor_pos is the position in 16 bit characters inside the
           UTF-16 string 'cmd_line' */ else if(cmd_line != last_cmd) {
      if(
        !colorize &&
        last_cmd.substring(0, last_cursor_pos) == cmd_line.substring(0, last_cursor_pos)
      ) {
        /* optimize common case */
        repl.puts(cmd_line.substring(last_cursor_pos));
      } else {
        /* goto the start of the line */
        // repl.debug("last_cmd",last_cmd, last_cursor_pos);
        const leading = last_cmd.substring(0, last_cursor_pos);
        //repl.debug("leading",leading);
        const move_x = -repl.ucs_length(leading);
        //repl.debug("move_x",move_x);
        repl.move_cursor(move_x);

        if(colorize) {
          var str = mexpr ? mexpr + '\n' + cmd_line : cmd_line;
          var start = str.length - cmd_line.length;
          var colorstate = repl.colorize_js(str);
          repl.print_color_text(str, start, colorstate[2]);
        } else {
          repl.puts(cmd_line);
        }
      }
      term_cursor_x = (term_cursor_x + repl.ucs_length(cmd_line)) % term_width;
      if(term_cursor_x == 0) {
        /* show the cursor on the next line */
        repl.puts(' \x08');
      }
      /* remove the trailing characters */
      repl.puts('\x1b[J');
      last_cmd = cmd_line;
      last_cursor_pos = cmd_line.length;
    }
    if(cursor_pos > last_cursor_pos) {
      repl.move_cursor(repl.ucs_length(cmd_line.substring(last_cursor_pos, cursor_pos)));
    } else if(cursor_pos < last_cursor_pos) {
      repl.move_cursor(-repl.ucs_length(cmd_line.substring(cursor_pos, last_cursor_pos)));
    }
    last_cursor_pos = cursor_pos;
    repl.flush();
  }

  /* editing commands */
  function insert(str) {
    if(str) {
      repl.cmd = repl.cmd.substring(0, cursor_pos) + str + repl.cmd.substring(cursor_pos);
      cursor_pos += str.length;
    }
  }

  function quoted_insert() {
    quote_flag = true;
  }

  function abort() {
    repl.cmd = '';
    cursor_pos = 0;
    return -2;
  }

  function alert() {}

  function reverse_search() {
    if(search == 0) repl.cmd = '';
    search--;
    readline_cb = search_cb;
    repl.debug('reverse_search', { search, cursor_pos, term_cursor_x });
    repl.update();
    return -2;
  }
  function forward_search() {
    if(search == 0) repl.cmd = '';
    search++;
    readline_cb = search_cb;
    //repl.debug('forward_search', { search, cursor_pos, term_cursor_x });
    repl.update();
    return -2;
  }

  function search_cb(pattern) {
    if(pattern !== null) {
      const histcmd = history[search_index];
      //repl.debug('search_cb', { pattern, histcmd,cmd }, history.slice(-2));
      search = 0;
      readline_cb = readline_handle_cmd;
      repl.cmd = '';
      repl.readline_handle_cmd(histcmd ?? '');

      repl.update();
    }
  }

  function beginning_of_line() {
    cursor_pos = 0;
  }

  function end_of_line() {
    cursor_pos = repl.cmd.length;
  }

  function forward_char() {
    if(cursor_pos < repl.cmd.length) {
      cursor_pos++;
      while(repl.is_trailing_surrogate(repl.cmd.charAt(cursor_pos))) cursor_pos++;
    }
  }

  function backward_char() {
    if(cursor_pos > 0) {
      cursor_pos--;
      while(repl.is_trailing_surrogate(repl.cmd.charAt(cursor_pos))) cursor_pos--;
    }
  }

  function skip_word_forward(pos) {
    while(pos < repl.cmd.length && !repl.is_word(repl.cmd.charAt(pos))) pos++;
    while(pos < repl.cmd.length && repl.is_word(repl.cmd.charAt(pos))) pos++;
    return pos;
  }

  function skip_word_backward(pos) {
    while(pos > 0 && !repl.is_word(repl.cmd.charAt(pos - 1))) pos--;
    while(pos > 0 && repl.is_word(repl.cmd.charAt(pos - 1))) pos--;
    return pos;
  }

  function forward_word() {
    cursor_pos = repl.skip_word_forward(cursor_pos);
  }

  function backward_word() {
    cursor_pos = repl.skip_word_backward(cursor_pos);
  }

  function accept_line() {
    repl.puts('\n');
    repl.history_add(search ? history[search_index] : repl.cmd);
    repl.debug(
      'accept_line',
      { cmd: repl.cmd, history_index, search, history_length: history.length },
      [...history.entries()].slice(history_index - 3, history_index + 2)
    );
    return -1;
  }

  function history_set(entries) {
    if(entries) history.splice(0, history.length, ...entries);
  }

  function history_get() {
    return history;
  }
  function history_pos() {
    return history_index;
  }

  function history_add(str) {
    //repl.debug('history_add', { str });
    str = str.trim();

    if(str) {
      history.push(str);
    }
    history_index = history.length;
  }

  function num_lines(str) {
    return str.split(/\n/g).length;
  }
  function last_line(str) {
    let lines = str.split(/\n/g);
    return lines[lines.length - 1];
  }

  function history_previous() {
    let num_lines = repl.cmd.split(/\n/g).length;

    if(num_lines > 1) repl.readline_clear();
    search = 0;

    if(history_index > 0) {
      if(history_index == history.length) {
        history.push(repl.cmd);
      }
      history_index--;
      repl.cmd = history[history_index];
      cursor_pos = repl.cmd.length;
    }
  }

  function history_next() {
    let num_lines = repl.cmd.split(/\n/g).length;

    if(num_lines > 1) repl.readline_clear();
    search = 0;

    if(history_index < history.length - 1) {
      history_index++;
      repl.cmd = history[history_index];
      cursor_pos = repl.cmd.length;
    }
  }

  function history_search(dir) {
    var pos = cursor_pos;
    for(var i = 1; i <= history.length; i++) {
      var index = (history.length + i * dir + history_index) % history.length;
      if(history[index].substring(0, pos) == repl.cmd.substring(0, pos)) {
        history_index = index;
        repl.cmd = history[index];
        return;
      }
    }
  }

  function history_search_backward() {
    return repl.history_search(-1);
  }

  function history_search_forward() {
    return repl.history_search(1);
  }

  function delete_char_dir(dir) {
    var start, end;

    start = cursor_pos;
    if(dir < 0) {
      start--;
      while(repl.is_trailing_surrogate(repl.cmd.charAt(start))) start--;
    }
    end = start + 1;
    while(repl.is_trailing_surrogate(repl.cmd.charAt(end))) end++;

    if(start >= 0 && start < repl.cmd.length) {
      if(last_fun === kill_region) {
        repl.kill_region(start, end, dir);
      } else {
        repl.cmd = repl.cmd.substring(0, start) + repl.cmd.substring(end);
        cursor_pos = start;
      }
    }
  }

  function delete_char() {
    repl.delete_char_dir(1);
  }

  function control_d() {
    if(repl.cmd.length == 0) {
      repl.puts('\n');

      (repl.cleanup ?? std.exit)(0);

      return -3; /* exit read eval print loop */
    } else {
      repl.delete_char_dir(1);
    }
  }

  function backward_delete_char() {
    repl.delete_char_dir(-1);
  }

  function transpose_chars() {
    var pos = cursor_pos;
    if(repl.cmd.length > 1 && pos > 0) {
      if(pos == repl.cmd.length) pos--;
      repl.cmd =
        repl.cmd.substring(0, pos - 1) +
        repl.cmd.substring(pos, pos + 1) +
        repl.cmd.substring(pos - 1, pos) +
        repl.cmd.substring(pos + 1);
      cursor_pos = pos + 1;
    }
  }

  function transpose_words() {
    var p1 = repl.skip_word_backward(cursor_pos);
    var p2 = repl.skip_word_forward(p1);
    var p4 = repl.skip_word_forward(cursor_pos);
    var p3 = repl.skip_word_backward(p4);

    if(p1 < p2 && p2 <= cursor_pos && cursor_pos <= p3 && p3 < p4) {
      repl.cmd =
        repl.cmd.substring(0, p1) +
        repl.cmd.substring(p3, p4) +
        repl.cmd.substring(p2, p3) +
        repl.cmd.substring(p1, p2);
      cursor_pos = p4;
    }
  }

  function upcase_word() {
    var end = repl.skip_word_forward(cursor_pos);
    repl.cmd =
      repl.cmd.substring(0, cursor_pos) +
      repl.cmd.substring(cursor_pos, end).toUpperCase() +
      repl.cmd.substring(end);
  }

  function downcase_word() {
    var end = repl.skip_word_forward(cursor_pos);
    repl.cmd =
      repl.cmd.substring(0, cursor_pos) +
      repl.cmd.substring(cursor_pos, end).toLowerCase() +
      repl.cmd.substring(end);
  }

  function kill_region(start, end, dir) {
    var s = repl.cmd.substring(start, end);
    if(last_fun !== kill_region) clip_board = s;
    else if(dir < 0) clip_board = s + clip_board;
    else clip_board = clip_board + s;

    repl.cmd = repl.cmd.substring(0, start) + repl.cmd.substring(end);
    if(cursor_pos > end) cursor_pos -= end - start;
    else if(cursor_pos > start) cursor_pos = start;
    this_fun = kill_region;
  }

  function kill_line() {
    repl.kill_region(cursor_pos, repl.cmd.length, 1);
  }

  function backward_kill_line() {
    repl.kill_region(0, cursor_pos, -1);
  }

  function kill_word() {
    repl.kill_region(cursor_pos, repl.skip_word_forward(cursor_pos), 1);
  }

  function backward_kill_word() {
    repl.kill_region(repl.skip_word_backward(cursor_pos), cursor_pos, -1);
  }

  function yank() {
    repl.insert(clip_board);
  }

  function control_c() {
    if(last_fun === control_c) {
      repl.puts('\n');

      running = false;
      (repl.cleanup ?? std.exit)(0);
    } else {
      repl.puts('\n(Press Ctrl-C again to quit)\n');
      repl.cmd = '';
      repl.readline_print_prompt();
    }
  }

  function reset() {
    repl.cmd = '';
    cursor_pos = 0;
  }

  function get_context_word(line, pos) {
    var s = '';
    while(pos > 0 && repl.is_word(line[pos - 1])) {
      pos--;
      s = line[pos] + s;
    }
    return s;
  }
  function get_context_object(line, pos) {
    var obj, base, c;
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
          return /\ /;
        default:
          if(repl.is_word(c)) {
            base = repl.get_context_word(line, pos);
            if(['true', 'false', 'null', 'this'].includes(base) || !isNaN(+base))
              return eval(base);
            obj = repl.get_context_object(line, pos - base.length);
            if(obj === null || obj === void 0) return obj;
            if(obj === globalThis && obj[base] === void 0) {
              let ret;
              try {
                ret = eval(base);
              } catch(e) {}
              return ret;
            } else return obj[base];
          }
          return {};
      }
    }
    return void 0;
  }

  function get_directory_entries(pathStr = '.', mask = '*') {
    let dir, base;
    let stat = fs.stat(pathStr);
    if(stat && stat?.isDirectory() && pathStr.endsWith('/')) {
      dir = pathStr;
      base = '';
    } else {
      dir = path.dirname(pathStr);
      base = path.basename(pathStr);
    }
    let expr = mask.replace(/\./g, '\\.').replace(/\*/g, '.*');
    expr = (mask.startsWith('*') ? '' : '^') + expr + (mask.endsWith('*') ? '' : '$');
    let re = new RegExp(expr);
    //repl.debug('get_directory_entries:', { dir, base, expr });
    let entries = fs
      .readdir(dir)
      .map(entry => {
        let st = fs.stat(path.join(dir, entry));
        return entry + (st && st.isDirectory() ? '/' : '');
      })
      .sort((a, b) => b.endsWith('/') - a.endsWith('/') || a.localeCompare(b));
    //repl.debug('get_directory_entries:', { entries });
    entries = entries.filter(entry => re.test(entry) || entry.endsWith('/'));
    if(base != '') entries = entries.filter(entry => entry.startsWith(base));
    //repl.debug('get_directory_entries:', { len: entries.length });
    return entries.map(entry => path.join(dir, entry));
  }

  function get_filename_completions(line, pos) {
    let s = line.slice(0, pos).replace(/^\\[^ ]?\s*/, '');

    //  let s = repl.get_context_word(line, pos);
    //repl.debug('get_filename_completions', { line, pos, s });

    let mask = '*';

    if(line.startsWith('\\i')) mask = '*.(js|so)';
    let tab = repl.get_directory_entries(s, mask);
    return { tab, pos: s.length, ctx: {} };
  }

  function get_completions(line, pos) {
    var s, obj, ctx_obj, r, i, j, paren;

    if(/\\[il]/.test(repl.cmd)) return repl.get_filename_completions(line, pos);

    s = repl.get_context_word(line, pos);
    //    repl.print_status('get_completions', { line, pos, repl.cmd, word: s });

    ctx_obj = repl.get_context_object(line, pos - s.length);

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
      r.sort(symcmp);
      for(i = j = 1; i < r.length; i++) {
        if(r[i] != r[i - 1]) r[j++] = r[i];
      }
      r.length = j;
    }
    /* 'tab' = list of completions, 'pos' = cursor position inside
           the completions */
    return { tab: r, pos: s.length, ctx: ctx_obj };
  }

  function completion() {
    var tab, res, s, i, j, len, t, max_width, col, n_cols, row, n_rows;
    res = repl.get_completions(repl.cmd, cursor_pos);
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
      repl.insert(s[i]);
    }
    if(last_fun === completion && tab.length == 1) {
      /* append parentheses to function names */
      var m = res.ctx[tab[0]];
      if(typeof m == 'function') {
        repl.insert('(');
        if(m.length == 0) repl.insert(')');
      } else if(typeof m == 'object') {
        repl.insert('.');
      }
    }
    /* show the possible completions */
    if(last_fun === completion && tab.length >= 2) {
      max_width = 0;
      for(i = 0; i < tab.length; i++) max_width = Math.max(max_width, tab[i].length);
      max_width += 2;
      n_cols = Math.max(1, Math.floor((term_width + 1) / max_width));
      n_rows = Math.ceil(tab.length / n_cols);
      repl.puts('\n');
      /* display the sorted list column-wise */
      for(row = 0; row < n_rows; row++) {
        for(col = 0; col < n_cols; col++) {
          i = col * n_rows + row;
          if(i >= tab.length) break;
          s = tab[i];
          if(col != n_cols - 1) s = s.padEnd(max_width);
          repl.puts(s);
        }
        repl.puts('\n');
      }
      /* show a new prompt */
      repl.readline_print_prompt();
    }
  }

  function dupstr(str, count) {
    var res = '';
    while(count-- > 0) res += str;
    return res;
  }

  function readline_clear() {
    const num_lines = (repl.cmd ?? '').split(/\n/g).length;

    if(num_lines > 1) Terminal.cursorUp(num_lines - 1);

    Terminal.cursorHome();
    Terminal.eraseInDisplay();
    repl.readline_print_prompt();
  }

  function readline_print_prompt() {
    repl.puts('\r\x1b[K');
    repl.puts(prompt);
    term_cursor_x = repl.ucs_length(prompt) % term_width;
    last_cmd = '';
    last_cursor_pos = 0;
  }

  function readline_start(defstr, cb) {
    repl.debug('readline_start', { defstr, cb });
    let a = (defstr || '').split(/\n/g);
    mexpr = a.slice(0, -1).join('\n');
    repl.cmd = a[a.length - 1];

    cursor_pos = repl.cmd.length;
    history_index = history.length;
    readline_cb = cb;

    prompt = pstate;

    if(mexpr) {
      prompt += repl.dupstr(' ', plen - prompt.length);
      prompt += ps2;
    } else {
      if(show_time) {
        var t = Math.round(eval_time) + ' ';
        eval_time = 0;
        t = repl.dupstr('0', 5 - t.length) + t;
        prompt += t.substring(0, t.length - 4) + '.' + t.substring(t.length - 4);
      }
      plen = prompt.length;
      prompt += ps1;
    }
    repl.readline_print_prompt();
    repl.update();
    repl.readline_state = 0;
  }

  function handle_char(c1) {
    var c;
    repl.debug('handle_char', {
      c1,
      state: repl.readline_state,
      readline_keys
    });
    c = String.fromCodePoint(c1);

    for(;;) {
      switch (repl.readline_state) {
        case 0:
          if(c == '\x1b') {
            /* '^[' - ESC */
            readline_keys = c;
            repl.readline_state = 1;
          } else {
            repl.handle_key(c);
          }
          break;
        case 1 /* '^[ */:
          readline_keys += c;
          if(c == '[') {
            repl.readline_state = 2;
          } else if(c == 'O') {
            repl.readline_state = 3;
          } else {
            repl.handle_key(readline_keys);
            repl.readline_state = 0;
          }
          break;
        case 2 /* '^[[' - CSI */:
          readline_keys += c;

          if(c == '<') {
            repl.readline_state = 4;
          } else if(!(c == ';' || (c >= '0' && c <= '9'))) {
            repl.handle_key(readline_keys);
            repl.readline_state = 0;
          }
          break;

        case 3 /* '^[O' - ESC2 */:
          readline_keys += c;
          repl.handle_key(readline_keys);
          repl.readline_state = 0;
          break;

        case 4:
          if(!(c == ';' || (c >= '0' && c <= '9') || c == 'M' || c == 'm')) {
            repl.handle_mouse(readline_keys);

            repl.readline_state = 0;
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

  function handle_mouse(keys) {
    const [button, x, y, cmd] = [...Util.matchAll(/([0-9]+|[A-Za-z]+)/g, keys)]
      .map(p => p[1])
      .map(p => (!isNaN(+p) ? +p : p));
    let press = cmd == 'm';

    repl.debug('handle_mouse', { button, x, y, press });
  }

  function handle_key(keys) {
    var fun;
    repl.debug('handle_key:', { keys });

    if(quote_flag) {
      if(repl.ucs_length(keys) === 1) repl.insert(keys);
      quote_flag = false;
    } else if((fun = commands[keys])) {
      const ret = fun(keys);
      repl.debug('handle_key', {
        keys,
        fun,
        ret,
        cmd: repl.cmd,
        history_index
      });
      this_fun = fun;
      switch (ret) {
        case -1:
          readline_cb(repl.cmd);
          return;
        case -2:
          readline_cb(null);
          return;
        case -3:
          /* uninstall a Ctrl-C signal handler */
          Util.signal('SIGINT', null);
          /* uninstall the stdin read handler */
          fs.setReadHandler(this.term_fd, null);
          return;
        default:
          if(
            search &&
            [
              accept_line,
              backward_char,
              backward_delete_char,
              backward_kill_line,
              backward_kill_word,
              backward_word,
              beginning_of_line,
              delete_char,
              //end_of_line,
              forward_char,
              forward_word,
              kill_line,
              kill_word
            ].indexOf(fun) == -1
          ) {
            const histcmd = history[search_index];

            //readline_cb = readline_handle_cmd;
            search = 0;
            //repl.cmd = histcmd;
            repl.puts(`\x1b[1G`);
            repl.puts(`\x1b[J`);
            cursor_pos = histcmd.length;
            repl.readline_start(histcmd, readline_handle_cmd);
            return;
            //repl.update();
          }
          break;
      }
      last_fun = this_fun;
    } else if(repl.ucs_length(keys) === 1 && keys >= ' ') {
      repl.insert(keys);
      last_fun = insert;
    } else {
      repl.alert(); /* beep! */
    }

    cursor_pos = cursor_pos < 0 ? 0 : cursor_pos > repl.cmd.length ? repl.cmd.length : cursor_pos;
    repl.update();
  }

  function number_to_string(a, radix) {
    var s;
    if(!isFinite(a)) {
      /* NaN, Infinite */
      return a.toString();
    } else {
      if(a == 0) {
        if(1 / a < 0) s = '-0';
        else s = '0';
      } else {
        if(radix == 16 && a === Math.floor(a)) {
          var s;
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

  function bigfloat_to_string(a, radix) {
    var s;
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
          var s;
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

  function bigint_to_string(a, radix) {
    var s;
    if(radix == 16) {
      var s;
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

  function print(a) {
    var stack = [];

    function print_rec(a) {
      var n, i, keys, key, type, s;

      type = typeof a;
      if(type === 'object') {
        if(a === null) {
          repl.puts(a);
        } else if(stack.indexOf(a) >= 0) {
          repl.puts('[circular]');
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
          repl.puts(a.toString());
        } else {
          stack.push(a);
          if(Array.isArray(a)) {
            n = a.length;
            repl.puts('[ ');
            for(i = 0; i < n; i++) {
              if(i !== 0) repl.puts(', ');
              if(i in a) {
                print_rec(a[i]);
              } else {
                repl.puts('<empty>');
              }
              if(i > 20) {
                repl.puts('...');
                break;
              }
            }
            repl.puts(' ]');
          } else if(Object.__getClass(a) === 'RegExp') {
            repl.puts(a.toString());
          } else {
            keys = Object.keys(a);
            n = keys.length;
            repl.puts('{ ');
            for(i = 0; i < n; i++) {
              if(i !== 0) repl.puts(', ');
              key = keys[i];
              repl.puts(key, ': ');
              print_rec(a[key]);
            }
            repl.puts(' }');
          }
          stack.pop(a);
        }
      } else if(type === 'string') {
        s = a.__quote();
        if(s.length > 79) s = s.substring(0, 75) + '..."';
        repl.puts(s);
      } else if(type === 'number') {
        repl.puts(repl.number_to_string(a, hex_mode ? 16 : 10));
      } else if(type === 'bigint') {
        repl.puts(repl.bigint_to_string(a, hex_mode ? 16 : 10));
      } else if(type === 'bigfloat') {
        repl.puts(repl.bigfloat_to_string(a, hex_mode ? 16 : 10));
      } else if(type === 'bigdecimal') {
        repl.puts(a.toString() + 'm');
      } else if(type === 'symbol') {
        repl.puts(String(a));
      } else if(type === 'function') {
        repl.puts('function ' + a.name + '()');
      } else {
        repl.puts(a);
      }
    }
    print_rec(a);
  }

  function extract_directive(a) {
    var pos;
    if(a[0] !== '\\') return '';
    for(pos = 1; pos < a.length; pos++) {
      if(!repl.is_alpha(a[pos])) break;
    }
    return a.substring(1, pos);
  }

  /* return true if the string after cmd can be evaluted as JS */
  function handle_directive(cmd, expr) {
    var param, prec1, expBits1;
    const [, ...args] = expr.split(/\s+/g);

    if(cmd === 'h' || cmd === '?' || cmd == 'help') {
      repl.help();
    } else if(cmd === 'load') {
      var filename = expr.substring(cmd.length + 1).trim();
      if(filename.lastIndexOf('.') <= filename.lastIndexOf('/')) filename += '.js';
      std.loadScript(filename);
      return false;
    } else if(cmd === 'x') {
      hex_mode = true;
    } else if(cmd === 'd') {
      hex_mode = false;
    } else if(cmd === 't') {
      show_time = !show_time;
    } else if(has_bignum && cmd === 'p') {
      param = expr
        .substring(cmd.length + 1)
        .trim()
        .split(' ');
      if(param.length === 1 && param[0] === '') {
        repl.puts(
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
          repl.puts('Invalid precision\n');
          return false;
        }
        if(
          Number.isNaN(expBits1) ||
          expBits1 < BigFloatEnv.expBitsMin ||
          expBits1 > BigFloatEnv.expBitsMax
        ) {
          repl.puts('Invalid exponent bits\n');
          return false;
        }
        prec = prec1;
        expBits = expBits1;
      }
      return false;
    } else if(has_bignum && cmd === 'digits') {
      param = expr.substring(cmd.length + 1).trim();
      prec1 = Math.ceil(parseFloat(param) * log2_10);
      if(prec1 < BigFloatEnv.precMin || prec1 > BigFloatEnv.precMax) {
        repl.puts('Invalid precision\n');
        return false;
      }
      prec = prec1;
      expBits = BigFloatEnv.expBitsMax;
      return false;
    } else if(has_bignum && cmd === 'mode') {
      param = expr.substring(cmd.length + 1).trim();
      if(param === '') {
        repl.puts('Running mode=' + eval_mode + '\n');
      } else if(param === 'std' || param === 'math') {
        eval_mode = param;
      } else {
        repl.puts('Invalid mode\n');
      }
      return false;
    } else if(cmd === 'clear') {
      repl.puts('\x1b[H\x1b[J');
    } else if(cmd === 'q') {
      running = false;
      //(thisObj.exit ?? std.exit)(0);
      return false;
    } else if(cmd === 'i') {
      thisObj
        .importModule(...args)
        .catch(e => {
          repl.print_status(`ERROR importing:`, e);
          done = true;
        })
        .then(({ moduleName, modulePath, module }) => {
          repl.print_status(`imported '${moduleName}' from '${modulePath}':`, module);
          done = true;
        });
      /*while(!done) std.sleep(50);*/

      //    repl.debug("handle_directive", {cmd,module,exports});
      return false;
    } else if(has_jscalc && cmd === 'a') {
      algebraicMode = true;
    } else if(has_jscalc && cmd === 'n') {
      algebraicMode = false;
    } else if(repl.directives[cmd]) {
      const handler = repl.directives[cmd];
      return repl.handler(...args) === false ? false : true;
    } else {
      repl.puts('Unknown directive: ' + cmd + '\n');
      return false;
    }
    return true;
  }

  if(config_numcalc) {
    /* called by the GUI */
    globalThis.execCmd = function(cmd) {
      switch (cmd) {
        case 'dec':
          hex_mode = false;
          break;
        case 'hex':
          hex_mode = true;
          break;
        case 'num':
          algebraicMode = false;
          break;
        case 'alg':
          algebraicMode = true;
          break;
      }
    };
  }

  function help() {
    function sel(n) {
      return n ? '*' : ' ';
    }
    repl.puts(
      '\\h          this help\n' +
        '\\x             ' +
        sel(hex_mode) +
        'hexadecimal number display\n' +
        '\\d             ' +
        sel(!hex_mode) +
        'decimal number display\n' +
        '\\t             ' +
        sel(show_time) +
        'toggle timing display\n' +
        '\\clear              clear the terminal\n'
    );
    if(has_jscalc) {
      repl.puts(
        '\\a             ' +
          sel(algebraicMode) +
          'algebraic mode\n' +
          '\\n             ' +
          sel(!algebraicMode) +
          'numeric mode\n'
      );
    }
    if(has_bignum) {
      repl.puts(
        "\\p [m [e]]       set the BigFloat precision to 'm' bits\n" +
          "\\digits n   set the BigFloat precision to 'ceil(n*log2(10))' bits\n"
      );
      if(!has_jscalc) {
        repl.puts('\\mode [std|math] change the running mode (current = ' + eval_mode + ')\n');
      }
    }
    repl.puts('\\i [module] import module\n');
    if(!config_numcalc) {
      repl.puts('\\q          exit\n');
    }
  }

  function print_status(...args) {
    repl.puts('\x1b[1S');
    repl.puts('\x1b[1F');
    //    repl.puts('\x1b[1G');
    repl.puts('\x1b[J');
    for(let arg of args) repl.show(arg);
    repl.puts('\r\x1b[J');
    repl.readline_print_prompt();
  }

  function eval_and_print(expr) {
    var result;

    try {
      if(eval_mode === 'math') expr = '"use math"; void 0;' + expr;
      var now = new Date().getTime();
      /* eval as a script */

      result = (std?.evalScript ?? eval)(expr, { backtrace_barrier: true });
      eval_time = new Date().getTime() - now;

      repl.print_status(colors[styles.result], result, '\n', colors.none);
      repl.update();
    } catch(error) {
      let output =
        `${error.constructor.name || 'EXCEPTION'}: ` +
        colors[styles.error_msg] +
        error?.message +
        '\n';

      //      repl.puts(error.stack+'');

      if(error instanceof Error || typeof error?.message == 'string') {
        repl.debug((error?.type ?? Util.className(error)) + ': ' + error?.message);
        if(error?.stack) output += error?.stack;
      } else {
        output += 'Throw: ' + error;
      }
      output += colors.none;

      repl.puts(output, '\n\r');
    }

    /* set the last result */
    globalThis._ = repl.result = result;
    if(Util.isPromise(result)) {
      result.then(value => {
        result.resolved = true;
        repl.print_status(
          `Promise resolved to:`,
          Util.typeOf(value),
          console.config({ depth: 1, multiline: true }),
          value
        );
        globalThis.$ = value;
      });
    }
  }

  function cmd_start(title) {
    if(repl.help) repl.puts(`${title} - Type "\\h" for help\n`);
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

    repl.cmd_readline_start();
  }

  function cmd_readline_start() {
    repl.readline_start(repl.dupstr('    ', level), readline_handle_cmd);
  }

  function readline_handle_cmd(expr) {
    let ret = repl.handle_cmd(expr);
    //repl.debug('readline_handle_cmd', { expr, mexpr, ret });
    repl.cmd_readline_start();
  }

  function handle_cmd(expr) {
    var colorstate, command;
    if(expr === null || expr === '') {
      expr = '';
      return -1;
    }
    repl.debug('handle_cmd', { expr, cmd: repl.cmd });
    if(expr === '?') {
      repl.help();
      return -2;
    }
    command = repl.extract_directive(expr);
    if(command.length > 0) {
      if(!repl.handle_directive(command, expr)) return -3;
      expr = expr.substring(command.length + 1);
    }
    if(expr === '') return -4;

    if(mexpr) expr = mexpr + '\n' + expr;
    colorstate = repl.colorize_js(expr);
    pstate = colorstate[0];
    level = colorstate[1];
    if(pstate) {
      mexpr = expr;
      return -5;
    }
    mexpr = '';

    if(has_bignum) {
      BigFloatEnv.setPrec(eval_and_print.bind(null, expr), prec, expBits);
    } else {
      repl.eval_and_print(expr);
    }
    level = 0;
    let histidx = history?.findLastIndex(entry => expr?.startsWith(entry));

    repl.history_add(history.splice(histidx, history_index - histidx).join('\n'));
    //repl.debug('handle_cmd', {histidx}, history.slice(histidx));
    /* run the garbage collector after each command */
    if(Util.getPlatform() == 'quickjs') std.gc();
  }

  function colorize_js(str) {
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

    function push_state(c) {
      state += c;
    }
    function last_state(c) {
      return state.substring(state.length - 1);
    }
    function pop_state(c) {
      var c = last_state();
      state = state.substring(0, state.length - 1);
      return c;
    }

    function parse_block_comment() {
      style = 'comment';
      push_state('/');
      for(i++; i < n - 1; i++) {
        if(str[i] == '*' && str[i + 1] == '/') {
          i += 2;
          pop_state('/');
          break;
        }
      }
    }

    function parse_line_comment() {
      style = 'comment';
      for(i++; i < n; i++) {
        if(str[i] == '\n') {
          break;
        }
      }
    }

    function parse_string(delim) {
      style = 'string';
      push_state(delim);
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
          pop_state();
          break;
        }
      }
    }

    function parse_regex() {
      style = 'regex';
      push_state('/');
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
        if(last_state() == '[') {
          if(c == ']') {
            pop_state();
          }
          //ECMA 5: ignore '/' inside char classes
          continue;
        }
        if(c == '[') {
          push_state('[');
          if(str[i] == '[' || str[i] == ']') i++;
          continue;
        }
        if(c == '/') {
          pop_state();
          while(i < n && repl.is_word(str[i])) i++;
          break;
        }
      }
    }

    function parse_number() {
      style = 'number';
      while(
        i < n &&
        (repl.is_word(str[i]) || (str[i] == '.' && (i == n - 1 || str[i + 1] != '.')))
      ) {
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

    function parse_identifier() {
      can_regex = 1;

      while(i < n && repl.is_word(str[i])) i++;

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

    function set_style(_from, to) {
      while(r.length < _from) r.push('default');
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
            //block comment
            parse_block_comment();
            break;
          }
          if(i < n && str[i] == '/') {
            //line comment
            parse_line_comment();
            break;
          }
          if(can_regex) {
            parse_regex();
            can_regex = 0;
            break;
          }
          can_regex = 1;
          continue;
        case "'":
        case '"':
        case '`':
          parse_string(c);
          can_regex = 0;
          break;
        case '(':
        case '[':
        case '{':
          can_regex = 1;
          level++;
          push_state(c);
          continue;
        case ')':
        case ']':
        case '}':
          can_regex = 0;
          if(level > 0 && repl.is_balanced(last_state(), c)) {
            level--;
            pop_state();
            continue;
          }
          style = 'error';
          break;
        default:
          if(repl.is_digit(c)) {
            parse_number();
            can_regex = 0;
            break;
          }
          if(repl.is_word(c) || c == '$') {
            parse_identifier();
            break;
          }
          can_regex = 1;
          continue;
      }
      if(style) set_style(start, i);
    }
    set_style(n, n);
    return [state, level, r];
  }
  Object.assign(repl, { debug() {}, print_status });
  Object.defineProperties(
    repl,
    Object.fromEntries(
      Object.entries({
        run,
        runSync,
        term_init,
        sigint_handler,
        term_read_handler,
        handle_byte,
        is_alpha,
        is_digit,
        is_word,
        ucs_length,
        is_trailing_surrogate,
        is_balanced,
        print_color_text,
        print_csi,
        move_cursor,
        update,
        insert,
        quoted_insert,
        abort,
        alert,
        beginning_of_line,
        end_of_line,
        forward_char,
        backward_char,
        skip_word_forward,
        skip_word_backward,
        forward_word,
        backward_word,
        accept_line,
        history_get,
        history_pos,
        history_set,
        history_add,
        history_previous,
        history_next,
        history_search,
        history_search_backward,
        history_search_forward,
        delete_char_dir,
        delete_char,
        control_d,
        backward_delete_char,
        transpose_chars,
        transpose_words,
        upcase_word,
        downcase_word,
        kill_region,
        kill_line,
        backward_kill_line,
        kill_word,
        backward_kill_word,
        yank,
        control_c,
        reset,
        get_context_word,
        get_context_object,
        get_completions,
        completion,
        dupstr,
        readline_print_prompt,
        readline_start,
        readline_clear,
        handle_char,
        handle_key,
        number_to_string,
        bigfloat_to_string,
        bigint_to_string,
        print,
        extract_directive,
        handle_directive,
        // help,
        eval_and_print,
        cmd_start,
        cmd_readline_start,
        readline_handle_cmd,
        handle_cmd,
        colorize_js,
        get_directory_entries,
        get_filename_completions,
        wrapPrintFunction
      }).map(([name, value]) => [name, { value, enumerable: false, writable: false }])
    )
  );

  return this;

  function waitRead(fd) {
    return new Promise((resolve, reject) => {
      fs.setReadHandler(fd, () => {
        fs.setReadHandler(fd, null);
        resolve();
      });
    });
  }

  async function run() {
    repl.debug('run');

    if(!console.options) console.options = {};

    /* console.options.depth = 2;
    console.options.compact = 2;
    console.options.maxArrayLength = Infinity;*/

    await repl.term_init();
    repl.cmd_start(title);

    do {
      await fs.waitRead(input.fileno());

      await repl.term_read_handler();
    } while(running);
  }
  function runSync() {
    repl.debug('run');

    if(!console.options) console.options = {};

    /* console.options.depth = 2;
    console.options.compact = 2;
    console.options.maxArrayLength = Infinity;*/

    repl.term_init();
    repl.cmd_start(title);

    os.setReadHandler(input.fileno(), () => repl.term_read_handler());
  }

  function wrapPrintFunction(fn, thisObj) {
    return (...args) => {
      let ret;
      puts('\r\x1b[K');
      ret = fn.call(thisObj, ...args);
      //repl.term_init();
      //repl.cmd_readline_start(title);
      // repl.puts('\r\x1b[J');
      repl.readline_print_prompt();
      repl.update();
    };
  }
}
