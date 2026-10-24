import { isatty, platform } from 'os';
import { ttyGetWinSize } from 'terminal';
import { define, isObject, isFunction, getPerformanceCounter, isPrototypeOf } from 'util';
import inspect from 'inspect';
import { open, err as stderr, in as stdin, out as stdout, getenv } from 'std';

function isStream(obj) {
  return isObject(obj) && isFunction(obj.write);
}

export function Console(...args) {
  let out, err, ignErr, opts;

  if(args.length > 0 && isStream(args[0])) {
    out = args.shift();

    if(args.length > 0 && isStream(args[0])) {
      err = args.shift();

      if(args.length > 0 && typeof args[0] == 'boolean') ignErr = args.shift();
    }
  }

  if(args.length > 0 && typeof args[0] == 'object') opts = args.shift();

  opts ??= { inspectOptions: { compact: false } };

  /* Node's new Console({ stdout, stderr, inspectOptions }) */
  if(!out && isStream(opts.stdout)) {
    out = opts.stdout;

    if(isStream(opts.stderr)) err = opts.stderr;
  }

  out = out || stdout;
  err = err || out || stderr;

  const isWin = platform.startsWith('win');

  const createStreamOptions = (stream, defaultFd) => {
    let fd = defaultFd;
    try {
      fd = stream.fileno();
    } catch(e) {}

    let size;
    try {
      size = ttyGetWinSize(fd);
    } catch(err) {}

    const terminal = isatty(fd);
    const breakLength = (terminal && Array.isArray(size) ? size[0] : undefined) || getenv('COLUMNS') || Infinity;

    return Object.setPrototypeOf(
      {
        depth: Infinity,
        stringBreakNewline: false,
        maxArrayLength: 30,
        maxStringLength: 1024,
        compact: 1,
        customInspect: true,
        showHidden: false,
        showProxy: false,
        getters: false,
        reparseable: false,
        numberBase: 10,
        classKey: Symbol.toStringTag,
        hideKeys: [Symbol.toStringTag],
        colors: terminal && !isWin,
        breakLength,
        ...(opts.inspectOptions ?? {}),
      },
      null,
    );
  };

  const outOptions = createStreamOptions(out, 1);
  const errOptions = createStreamOptions(err, 2);

  const newcons = Object.create(Console.prototype);
  newcons.options = outOptions;

  const printFunction = stream => (isFunction(stream) ? stream : isFunction(stream.flush) ? text => (stream.puts(text), stream.flush()) : text => stream.puts(text));

  const outputFunction = stream => {
    const print = printFunction(stream);
    return (...args) => print(args.join(' ') + '\n');
  };

  const inspectFunction = (obj, optsToUse, customOptions) => {
    if(obj === undefined) obj = newcons;
    return inspect(obj, ConsoleOptions.merge(optsToUse, customOptions));
  };

  const logFunction = (stream, streamOptions) =>
    function log(...args) {
      const tempOpts = new ConsoleOptions(streamOptions);
      const acc = tempOpts.prefix ? [tempOpts.prefix] : [];
      let i = 0;

      for(let arg of args) {
        if(isPrototypeOf(ConsoleOptions.prototype, arg)) {
          tempOpts.merge(arg);
          continue;
        }

        if(i++ >= 0) {
          if(typeof arg != 'string') arg = inspectFunction(arg, streamOptions, tempOpts);
          if(typeof arg != 'symbol') acc.push(arg);
          continue;
        }

        acc.push(arg);
      }

      return outputFunction(stream)(...acc);
    };

  return addMissingMethods(newcons);

  function addMissingMethods(cons) {
    for(let [method, stream, streamOpts] of [
      ['log', out, outOptions],
      ['info', out, outOptions],
      ['error', err, errOptions],
      ['warn', err, errOptions],
      ['debug', out, outOptions],
    ]) {
      if(method == 'debug') {
        let file = (getenv('DEBUG') ?? '').split(/[^\w.@/]/g).find(s => /^@/.test(s));
        if(file) {
          file = file.slice(1);
          stdout.puts(`file: ${file}\n`);
          stdout.flush();
          stream = open(file, 'a');
        }
      }

      if(cons[method] === undefined) cons[method] = logFunction(stream, streamOpts);
    }

    return cons;
  }
}

const timers = {};

define(Console.prototype, {
  config(obj = {}) {
    return new ConsoleOptions(obj);
  },
  time(name) {
    if(name in timers) throw new Error(`Timer '${name}' already exists`);
    timers[name] = getPerformanceCounter();
  },
  timeLog(name) {
    const t = getPerformanceCounter();

    if(!(name in timers)) throw new Error(`Timer '${name}' does not exist`);

    this.log(name + ': ' + (t - timers[name]).toFixed(6) + 'ms');
  },
  timeEnd(name) {
    this.timeLog(name);

    delete timers[name];
  },
});

export function ConsoleOptions(obj = {}) {
  let { multiline = true, ...rest } = obj;

  if(multiline == false) {
    rest.breakLength = Infinity;
    rest.stringBreakNewline = false;
  }

  Object.assign(this, rest);
}

ConsoleOptions.prototype.merge = function(...args) {
  return Object.assign(this, ...args);
};

ConsoleOptions.merge = function(opts, ...args) {
  return new ConsoleOptions(opts).merge(...args);
};

export default function ConsoleSetup(inspectOptions = {}, callback) {
  globalThis.console = new Console({ inspectOptions });
  if(isFunction(callback)) callback(globalThis.console);
  return globalThis.console;
}

export { default as inspect } from 'inspect';
