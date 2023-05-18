import { isatty, ttyGetWinSize, signal } from 'os';
import { fdopen, out } from 'std';
import { SIGWINCH, define } from 'util';

export function ReadStream(fd) {
  let f = fdopen(fd, 'r');

  return Object.setPrototypeOf(f, (new.target ?? ReadStream).prototype);
}

define(ReadStream.prototype, {
  [Symbol.toStringTag]: 'ReadStream'
});

Object.setPrototypeOf(ReadStream.prototype, Object.getPrototypeOf(out));

const writeStreams = new Set();

const addStream = stream => {
  const { size } = writeStreams;
  writeStreams.add(stream);

  if(size == 0 && writeStreams.size > 0)
    signal(SIGWINCH, () => {
      for(let stream of writeStreams) updateStream(stream);
    });
};

const updateStream = stream => {
  let [columns, rows] = stream.getWindowSize();

  Object.defineProperties(stream, {
    columns: { value: columns, configurable: true },
    rows: { value: rows, configurable: true }
  });

  if('onresize' in stream) stream.onresize.call(stream);
};

export function WriteStream(fd) {
  let f = fdopen(fd, 'w');

  if(isatty(fd)) {
    f = Object.setPrototypeOf(f, (new.target ?? WriteStream).prototype);

    updateStream(f);
    addStream(f);
  }

  return f;
}

Object.defineProperties(WriteStream.prototype, {
  getWindowSize: {
    value() {
      return ttyGetWinSize(this.fileno());
    },
    configurable: true
  },
  clearLine: {
    value(dir) {
      this.puts(`\x1b[${[1, 2, 0][dir + 1]}K`);
      this.flush();
      return true;
    },
    configurable: true
  },
  clearScreenDown: {
    value() {
      this.puts(`\x1b[0J`);
      this.flush();
      return true;
    },
    configurable: true
  },
  cursorTo: {
    value(x, y) {
      this.puts(y === undefined ? `\x1b[${x}G` : x == 1 && y == 1 ? `\x1b[H` : `\x1b[${y};${x}H`);
      this.flush();
      return true;
    },
    configurable: true
  },
  moveCursor: {
    value(dx, dy) {
      let ax = Math.abs(dx),
        ay = Math.abs(dy);
      if(dy !== undefined) if (ay) this.puts(`\x1b[${ay <= 1 ? '' : ay};${dy < 0 ? 'A' : 'B'}`);
      if(ay !== undefined) if (ax) this.puts(`\x1b[${ax <= 1 ? '' : ax};${dx < 0 ? 'D' : 'C'}`);
      this.flush();
      return true;
    },
    configurable: true
  },
  [Symbol.toStringTag]: { value: 'WriteStream', configurable: true }
});

Object.setPrototypeOf(WriteStream.prototype, Object.getPrototypeOf(out));

export { isatty } from 'os';
