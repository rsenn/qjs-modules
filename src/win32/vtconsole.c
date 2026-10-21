/* vtconsole.c: VT output and input on a Win32 console without VT mode.
 * depends on: vtparse.c (output events), the console API.
 * rule: VT coordinates are relative to the visible window (srWindow),
 * never to the scrollback buffer. */

#ifdef _WIN32
#include <string.h>
#include "vtconsole.h"

/**
 * \addtogroup vtconsole
 * @{
 */

#define WIDE_CHUNK 1024

/* ANSI color order (black red green yellow blue magenta cyan white)
 * to console attribute bits, foreground position */
static const WORD ansi_colors[8] = {
    0,
    FOREGROUND_RED,
    FOREGROUND_GREEN,
    FOREGROUND_RED | FOREGROUND_GREEN,
    FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_BLUE,
    FOREGROUND_GREEN | FOREGROUND_BLUE,
    FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
};

static WORD
color_bits(int c) {
  return ansi_colors[c & 7] | (c & 8 ? FOREGROUND_INTENSITY : 0);
}

static WORD
vc_attr(VtConsole* vc) {
  WORD fg = vc->fg < 0 ? vc->default_attr & 0x0f : color_bits(vc->fg);
  WORD bg = vc->bg < 0 ? (vc->default_attr >> 4) & 0x0f : color_bits(vc->bg);

  if(vc->bold)
    fg |= FOREGROUND_INTENSITY;

  if(vc->reverse) {
    WORD t = fg;

    fg = bg;
    bg = t;
  }

  return fg | (bg << 4) | (vc->underline ? COMMON_LVB_UNDERSCORE : 0);
}

static BOOL
vc_info(VtConsole* vc, CONSOLE_SCREEN_BUFFER_INFO* sbi) {
  if(GetConsoleScreenBufferInfo(vc->handle, sbi))
    return TRUE;

  vc->failed = 1;
  return FALSE;
}

static void
vc_goto(VtConsole* vc, const CONSOLE_SCREEN_BUFFER_INFO* sbi, int x, int y) {
  COORD c;

  c.X = x < 0 ? 0 : x >= sbi->dwSize.X ? sbi->dwSize.X - 1 : x;
  c.Y = y < sbi->srWindow.Top ? sbi->srWindow.Top : y > sbi->srWindow.Bottom ? sbi->srWindow.Bottom : y;

  if(!SetConsoleCursorPosition(vc->handle, c))
    vc->failed = 1;
}

static void
vc_fill(VtConsole* vc, int x, int y, DWORD n) {
  COORD c = {x, y};
  DWORD w;

  if(!FillConsoleOutputCharacterW(vc->handle, L' ', n, c, &w) || !FillConsoleOutputAttribute(vc->handle, vc_attr(vc), n, c, &w))
    vc->failed = 1;
}

static void
vc_wide(VtConsole* vc, const char* s, size_t len) {
  WCHAR wbuf[WIDE_CHUNK];
  DWORD w;
  int n = MultiByteToWideChar(CP_UTF8, 0, s, len, wbuf, WIDE_CHUNK);

  if(n > 0 && !WriteConsoleW(vc->handle, wbuf, n, &w, 0))
    vc->failed = 1;
}

/* length of the UTF-8 sequence led by c, 1 for a stray byte */
static int
utf8_seqlen(unsigned char c) {
  return c >= 0xf0 ? 4 : c >= 0xe0 ? 3 : c >= 0xc0 ? 2 : 1;
}

/* the prefix of s[0..len) that ends on a sequence boundary */
static size_t
utf8_complete(const char* s, size_t len) {
  size_t i = len;

  while(i > 0 && len - i < 3 && (s[i - 1] & 0xc0) == 0x80)
    i--;

  if(i == 0)
    return len;

  return len - (i - 1) < (size_t)utf8_seqlen(s[i - 1]) ? i - 1 : len;
}

static void
vc_text(void* opaque, const char* s, size_t len) {
  VtConsole* vc = opaque;

  if(vc->ntail) {
    int need = utf8_seqlen(vc->tail[0]);

    while(vc->ntail < need && len && (*s & 0xc0) == 0x80) {
      vc->tail[vc->ntail++] = *s++;
      len--;
    }

    if(vc->ntail < need && !len)
      return;

    vc_wide(vc, vc->tail, vc->ntail);
    vc->ntail = 0;
  }

  while(len > WIDE_CHUNK) {
    size_t n = utf8_complete(s, WIDE_CHUNK);

    vc_wide(vc, s, n);
    s += n;
    len -= n;
  }

  if(len) {
    size_t n = utf8_complete(s, len);

    vc_wide(vc, s, n);
    memcpy(vc->tail, s + n, len - n);
    vc->ntail = len - n;
  }
}

static void
vc_control(void* opaque, int c) {
  VtConsole* vc = opaque;
  WCHAR wc = c;
  DWORD w;

  vc->ntail = 0;

  if(c == '\r' || c == '\n' || c == '\b' || c == '\t' || c == '\a')
    if(!WriteConsoleW(vc->handle, &wc, 1, &w, 0))
      vc->failed = 1;
}

static void
vc_save(VtConsole* vc) {
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(vc_info(vc, &sbi)) {
    vc->saved.X = sbi.dwCursorPosition.X;
    vc->saved.Y = sbi.dwCursorPosition.Y - sbi.srWindow.Top;
  }
}

static void
vc_restore(VtConsole* vc) {
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  if(vc_info(vc, &sbi))
    vc_goto(vc, &sbi, vc->saved.X, sbi.srWindow.Top + vc->saved.Y);
}

static void
vc_set_attr(VtConsole* vc) {
  if(!SetConsoleTextAttribute(vc->handle, vc_attr(vc)))
    vc->failed = 1;
}

static void
vc_esc(void* opaque, int intermediate, int final) {
  VtConsole* vc = opaque;

  vc->ntail = 0;

  if(intermediate)
    return;

  switch(final) {
    case '7': vc_save(vc); break;
    case '8': vc_restore(vc); break;
    case 'c': {
      CONSOLE_SCREEN_BUFFER_INFO sbi;

      vc->fg = vc->bg = -1;
      vc->bold = vc->underline = vc->reverse = 0;
      vc_set_attr(vc);

      if(vc_info(vc, &sbi)) {
        vc_fill(vc, 0, sbi.srWindow.Top, (sbi.srWindow.Bottom - sbi.srWindow.Top + 1) * sbi.dwSize.X);
        vc_goto(vc, &sbi, 0, sbi.srWindow.Top);
      }

      break;
    }
  }
}

/* ANSI color 0-15 nearest to an RGB value */
static int
rgb_color(int r, int g, int b) {
  int c = (r >= 128 ? 1 : 0) | (g >= 128 ? 2 : 0) | (b >= 128 ? 4 : 0);

  return c | (r >= 192 || g >= 192 || b >= 192 ? 8 : 0);
}

/* ANSI color 0-15 nearest to xterm color n of 256 */
static int
xterm_color(int n) {
  static const unsigned char cube[6] = {0, 95, 135, 175, 215, 255};

  if(n < 16)
    return n;

  if(n >= 232) {
    int level = 8 + (n - 232) * 10;

    return level < 64 ? 0 : level < 160 ? 8 : level < 224 ? 7 : 15;
  }

  n -= 16;
  return rgb_color(cube[n / 36], cube[(n / 6) % 6], cube[n % 6]);
}

/* SGR 38 and 48: returns the color, advances *i past its parameters */
static int
sgr_extended(const VtParser* vp, int* i) {
  int mode = vt_param(vp, *i + 1, 0);

  if(mode == 5) {
    *i += 2;
    return xterm_color(vt_param(vp, *i, 0) & 0xff);
  }

  if(mode == 2) {
    *i += 4;
    return rgb_color(vt_param(vp, *i - 2, 0), vt_param(vp, *i - 1, 0), vt_param(vp, *i, 0));
  }

  *i += 1;
  return -1;
}

static void
vc_sgr(VtConsole* vc, const VtParser* vp) {
  int i, n = vp->nparams ? vp->nparams : 1;

  for(i = 0; i < n; i++) {
    int p = vt_param(vp, i, 0);

    if(p == 0) {
      vc->fg = vc->bg = -1;
      vc->bold = vc->underline = vc->reverse = 0;
    } else if(p == 1) {
      vc->bold = 1;
    } else if(p == 4) {
      vc->underline = 1;
    } else if(p == 7) {
      vc->reverse = 1;
    } else if(p == 22) {
      vc->bold = 0;
    } else if(p == 24) {
      vc->underline = 0;
    } else if(p == 27) {
      vc->reverse = 0;
    } else if(p >= 30 && p <= 37) {
      vc->fg = p - 30;
    } else if(p == 38) {
      vc->fg = sgr_extended(vp, &i);
    } else if(p == 39) {
      vc->fg = -1;
    } else if(p >= 40 && p <= 47) {
      vc->bg = p - 40;
    } else if(p == 48) {
      vc->bg = sgr_extended(vp, &i);
    } else if(p == 49) {
      vc->bg = -1;
    } else if(p >= 90 && p <= 97) {
      vc->fg = p - 90 + 8;
    } else if(p >= 100 && p <= 107) {
      vc->bg = p - 100 + 8;
    }
  }

  vc_set_attr(vc);
}

static void
vc_cursor_visible(VtConsole* vc, BOOL visible) {
  CONSOLE_CURSOR_INFO ci;

  if(!GetConsoleCursorInfo(vc->handle, &ci))
    return;

  ci.bVisible = visible;

  if(!SetConsoleCursorInfo(vc->handle, &ci))
    vc->failed = 1;
}

/* erase in display (line = 0) or in line (line = 1), VT modes 0 1 2 */
static void
vc_erase(VtConsole* vc, const CONSOLE_SCREEN_BUFFER_INFO* sbi, int mode, BOOL line) {
  int w = sbi->dwSize.X, x = sbi->dwCursorPosition.X, y = sbi->dwCursorPosition.Y;
  int top = line ? y : sbi->srWindow.Top, bottom = line ? y : sbi->srWindow.Bottom;

  switch(mode) {
    case 0: vc_fill(vc, x, y, (bottom - y) * w + (w - x)); break;
    case 1: vc_fill(vc, 0, top, (y - top) * w + x + 1); break;
    default: vc_fill(vc, 0, top, (bottom - top + 1) * w); break;
  }
}

static void
vc_csi(void* opaque, const VtParser* vp, int final) {
  VtConsole* vc = opaque;
  CONSOLE_SCREEN_BUFFER_INFO sbi;
  int n, x, y, top;

  vc->ntail = 0;

  if(vp->prefix == '?') {
    if((final == 'h' || final == 'l') && vt_param(vp, 0, 0) == 25)
      vc_cursor_visible(vc, final == 'h');

    return;
  }

  if(vp->prefix || vp->intermediate)
    return;

  if(final == 'm') {
    vc_sgr(vc, vp);
    return;
  }

  if(!vc_info(vc, &sbi))
    return;

  n = vt_param(vp, 0, 1);
  x = sbi.dwCursorPosition.X;
  y = sbi.dwCursorPosition.Y;
  top = sbi.srWindow.Top;

  switch(final) {
    case 'A': vc_goto(vc, &sbi, x, y - n); break;
    case 'B': vc_goto(vc, &sbi, x, y + n); break;
    case 'C': vc_goto(vc, &sbi, x + n, y); break;
    case 'D': vc_goto(vc, &sbi, x - n, y); break;
    case 'E': vc_goto(vc, &sbi, 0, y + n); break;
    case 'F': vc_goto(vc, &sbi, 0, y - n); break;
    case 'G': vc_goto(vc, &sbi, n - 1, y); break;
    case 'd': vc_goto(vc, &sbi, x, top + n - 1); break;
    case 'H':
    case 'f': vc_goto(vc, &sbi, vt_param(vp, 1, 1) - 1, top + n - 1); break;
    case 'J': vc_erase(vc, &sbi, vt_param(vp, 0, 0), FALSE); break;
    case 'K': vc_erase(vc, &sbi, vt_param(vp, 0, 0), TRUE); break;
    case 'X': vc_fill(vc, x, y, n); break;
    case 's': vc_save(vc); break;
    case 'u': vc_restore(vc); break;
  }
}

static const VtHandler vc_handler = {vc_text, vc_control, vc_esc, vc_csi};

void
vtconsole_init(VtConsole* vc, HANDLE h) {
  CONSOLE_SCREEN_BUFFER_INFO sbi;

  memset(vc, 0, sizeof(*vc));
  vt_init(&vc->parser, &vc_handler, vc);
  vc->handle = h;
  vc->default_attr = GetConsoleScreenBufferInfo(h, &sbi) ? sbi.wAttributes & 0xff : 0x07;
  vc->fg = vc->bg = -1;
}

BOOL
vtconsole_write(VtConsole* vc, const char* buf, size_t len) {
  vc->failed = 0;
  vt_feed(&vc->parser, buf, len);
  return !vc->failed;
}

static size_t
put_utf8(char* out, unsigned cp) {
  if(cp < 0x80) {
    out[0] = cp;
    return 1;
  }

  if(cp < 0x800) {
    out[0] = 0xc0 | (cp >> 6);
    out[1] = 0x80 | (cp & 0x3f);
    return 2;
  }

  if(cp < 0x10000) {
    out[0] = 0xe0 | (cp >> 12);
    out[1] = 0x80 | ((cp >> 6) & 0x3f);
    out[2] = 0x80 | (cp & 0x3f);
    return 3;
  }

  out[0] = 0xf0 | (cp >> 18);
  out[1] = 0x80 | ((cp >> 12) & 0x3f);
  out[2] = 0x80 | ((cp >> 6) & 0x3f);
  out[3] = 0x80 | (cp & 0x3f);
  return 4;
}

/* a key without a character: its xterm sequence, at most 8 bytes.
 *
 *   VK_UP, no modifier  gives  ESC [ A
 *   VK_F5, ctrl         gives  ESC [ 15 ; 5 ~
 */
static size_t
put_vkey(char* out, WORD vk, int mod) {
  static const char letters[] = "ABCDHF";
  static const WORD letter_keys[] = {VK_UP, VK_DOWN, VK_RIGHT, VK_LEFT, VK_HOME, VK_END};
  static const unsigned char tilde_codes[] = {2, 3, 5, 6, 15, 17, 18, 19, 20, 21, 23, 24};
  static const WORD tilde_keys[] = {VK_INSERT, VK_DELETE, VK_PRIOR, VK_NEXT, VK_F5, VK_F6, VK_F7, VK_F8, VK_F9, VK_F10, VK_F11, VK_F12};
  size_t i, pos = 0;
  int code = 0, final = 0;

  for(i = 0; i < sizeof(letter_keys) / sizeof(letter_keys[0]); i++)
    if(letter_keys[i] == vk)
      final = letters[i];

  for(i = 0; i < sizeof(tilde_keys) / sizeof(tilde_keys[0]); i++)
    if(tilde_keys[i] == vk) {
      code = tilde_codes[i];
      final = '~';
    }

  if(vk >= VK_F1 && vk <= VK_F4) {
    final = 'P' + (vk - VK_F1);

    if(mod == 1) {
      out[0] = 0x1b;
      out[1] = 'O';
      out[2] = final;
      return 3;
    }
  }

  if(!final)
    return 0;

  out[pos++] = 0x1b;
  out[pos++] = '[';

  if(code >= 10)
    out[pos++] = '0' + code / 10;

  if(code)
    out[pos++] = '0' + code % 10;
  else if(mod > 1)
    out[pos++] = '1';

  if(mod > 1) {
    out[pos++] = ';';
    out[pos++] = '0' + mod;
  }

  out[pos++] = final;
  return pos;
}

/* the bytes for one key-down event, at most 8 */
static size_t
put_key(char* out, const KEY_EVENT_RECORD* ke, WCHAR* surrogate) {
  DWORD st = ke->dwControlKeyState;
  BOOL ctrl = !!(st & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)), alt = !!(st & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED));
  BOOL shift = !!(st & SHIFT_PRESSED);
  WCHAR wc = ke->uChar.UnicodeChar;
  size_t pos = 0;

  if(wc >= 0xd800 && wc < 0xdc00) {
    *surrogate = wc;
    return 0;
  }

  if(wc >= 0xdc00 && wc < 0xe000) {
    unsigned cp = *surrogate ? 0x10000 + ((*surrogate - 0xd800) << 10) + (wc - 0xdc00) : 0xfffd;

    *surrogate = 0;
    return put_utf8(out, cp);
  }

  if(wc == 0)
    return put_vkey(out, ke->wVirtualKeyCode, 1 + shift + alt * 2 + ctrl * 4);

  if(ke->wVirtualKeyCode == VK_BACK && !ctrl)
    wc = 0x7f;
  else if(ke->wVirtualKeyCode == VK_TAB && shift) {
    memcpy(out, "\x1b[Z", 3);
    return 3;
  }

  /* alt without ctrl prefixes ESC, ctrl and alt together is AltGr */
  if(alt && !ctrl)
    out[pos++] = 0x1b;

  return pos + put_utf8(out + pos, wc);
}

int
vtconsole_read(HANDLE h, char* buf, size_t len, WCHAR* surrogate) {
  INPUT_RECORD rec[16];
  DWORD i, count, avail;
  size_t pos = 0;

  if(!GetNumberOfConsoleInputEvents(h, &avail))
    return -1;

  while(avail > 0 && len - pos >= 16) {
    DWORD max = (len - pos) / 16;

    if(max > sizeof(rec) / sizeof(rec[0]))
      max = sizeof(rec) / sizeof(rec[0]);

    if(!ReadConsoleInputW(h, rec, avail < max ? avail : max, &count))
      return -1;

    for(i = 0; i < count; i++) {
      const KEY_EVENT_RECORD* ke = &rec[i].Event.KeyEvent;
      WORD r;

      if(rec[i].EventType != KEY_EVENT || !ke->bKeyDown)
        continue;

      for(r = 0; r < (ke->wRepeatCount ? ke->wRepeatCount : 1) && len - pos >= 8; r++)
        pos += put_key(buf + pos, ke, surrogate);
    }

    avail -= count;
  }

  return pos;
}

/** @} */

#endif /* defined(_WIN32) */
