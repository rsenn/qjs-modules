/* terminal/internal.js: the tty backend and the escape constants, shared by terminal.js and terminal/*.js.
 * depends on: qjsm: misc, os, std. node, bun, deno: process and node:fs.
 * rule: not part of the API, import the functions from terminal.js. */

/* terminal.js: escape sequences, raw mode, size, keys, widths and a buffered Screen.
 *
 * depends on: qjsm: misc (ttyWrite, ttyRead, ttySetRaw, ttyGetWinSize), os (sleep), textcode.
 *             node, bun, deno: process and node:fs.
 *
 * rule: every write is one ttyWrite(f, string) call. On qjsm it flushes f, and a Win32
 * console without VT processing gets the sequences as console calls.
 * f is a std FILE on qjsm, and process.stdout, process.stderr or an fd number elsewhere.
 *
 * exports:
 *   - Cursor movement: cursorHome, cursorUp/Down/Left/Right, cursorPosition, cursorOrigin,
 *     cursorSave/Restore, cursorQuery, cursorHide/Show
 *   - Erase: eraseInDisplay, eraseInLine
 *   - Scroll: scrollUp/Down, setScrollRegion, resetScrollRegion
 *   - Screen switching: setScreen, alternateScreen
 *   - Line wrap: linewrapEnable/Disable
 *   - Mouse: mouseEnable/Disable, mousetrackingEnable/Disable
 *   - SGR/color: sgr, reverseVideoOn/Off, rgbForeground/Background, hslToRgb
 *   - Key decoding: decodeKeys, readKey, charKey
 *   - String measures: stripAnsi, stringWidth, charWidth
 *   - Capabilities: queryCapabilities
 *   - Screen class: Screen (write, moveTo, clear, clearLine, fg/bg/sgr, resetAttrs, fillRect, box)
 */

/* the four tty primitives, from misc on qjsm and from process and node:fs elsewhere */
export const backend = await (async () => {
  /* scriptArgs is QuickJS only: importing misc elsewhere makes bun fetch a package of that name */
  if(typeof scriptArgs != 'undefined') {
    const { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite } = await import('misc');
    const { sleep } = await import('os');
    const { getenv } = await import('std');
    /* the text functions are missing from a textcode older than this file: the JS ones below stay in use */
    const { TextDecoder, stringWidth, stripAnsi, truncate, truncateLines, wrap, fitTable, prevCluster, nextCluster, layoutInput } = await import('textcode');

    return { EAGAIN, ttyGetWinSize, ttyRead, ttySetRaw, ttyWrite, sleep, getenv, TextDecoder, stringWidth, stripAnsi, truncate, truncateLines, wrap, fitTable, prevCluster, nextCluster, layoutInput };
  }

  const fs = await import('node:fs');
  const wait = new Int32Array(new SharedArrayBuffer(4));
  const stream = fd => (fd == 2 ? process.stderr : process.stdout);

  return {
    EAGAIN: 11,
    getenv: name => process.env[name],
    TextDecoder: globalThis.TextDecoder,
    sleep: ms => void Atomics.wait(wait, 0, 0, ms),
    ttyWrite(f, str) {
      if(typeof f == 'number') fs.writeSync(f, str);
      else f.write(str);
    },
    ttyRead(fd, buf, offset, length) {
      try {
        return fs.readSync(fd, new Uint8Array(buf, offset, length), 0, length, null);
      } catch(e) {
        if(e.code == 'EAGAIN') return -11;
        if(e.code == 'EOF') return 0;
        throw e;
      }
    },
    ttySetRaw(fd, restore) {
      if(!process.stdin.isTTY) return false;
      process.stdin.setRawMode(!restore);
      return !!process.stdout.isTTY;
    },
    ttyGetWinSize(fd) {
      const { columns, rows } = stream(fd);

      return columns && rows ? [columns, rows] : undefined;
    },
  };
})();

export const { sleep, TextDecoder, getenv } = backend;

export const ESC = '\x1b';

export const CSI = ESC + '[';

export const RESET = CSI + '0m';
