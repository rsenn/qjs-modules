import { isatty, platform, ttyGetWinSize } from 'os';
import { define, isObject, isFunction, getPerformanceCounter } from 'util';
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

  const stdioFds = [stdin, stdout, stderr].map((f, i) => {
    try {
      return f.fileno();
    } catch(e) {
      return i;
    }
  });

  let termFd = stdioFds.find(fd => isatty(fd)) ?? stdioFds[1];
  if(typeof termFd != 'number') termFd = 1;

  const consoleWidth = (fd = termFd) => {
    let size;
    try {
      size = ttyGetWinSize(fd);
    } catch(err) {}
    return Array.isArray(size) ? size[0] : undefined;
  };

  const isTerminal = isatty(termFd);

  const defaultBreakLength = (isTerminal && consoleWidth()) || getenv('COLUMNS') || Infinity;
  const defaultOptions = {
    depth: Infinity,
    colors: isTerminal && !platform.startsWith('win'),
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
  };

  const options = Object.setPrototypeOf(
    {
      ...defaultOptions,
      ...(opts.inspectOptions ?? {}),
    },
    null,
  );

  options.breakLength ??= defaultBreakLength;

  const newcons = Object.create(Console.prototype);

  newcons.options = options;

  const printFunction = out => (isFunction(out) ? out : isFunction(out.flush) ? text => (out.puts(text), out.flush()) : text => out.puts(text));

  const outputFunction = out => {
    const print = printFunction(out);
    return (...args) => print(args.join(' ') + '\n');
  };

  const inspectFunction = (...args) => {
    let [obj, opts] = args;
    if(args.length == 0) obj = newcons;
    return inspect(obj, ConsoleOptions.merge(options, opts));
  };

  const logFunction = output =>
    function log(...args) {
      const tempOpts = new ConsoleOptions(options);
      const acc = tempOpts.prefix ? [tempOpts.prefix] : [];
      let i = 0;

      for(let arg of args) {
        try {
          if(isObject(arg)) {
            if(arg == null) {
              acc.push('null');
              continue;
            } else if(arg.merge === ConsoleOptions.prototype.merge) {
              tempOpts.merge(arg);
              continue;
            }
          }

          if(i++ >= 0) {
            if(typeof arg != 'string') arg = inspectFunction(arg, tempOpts);

            if(typeof arg != 'symbol') acc.push(arg);
            continue;
          }

          acc.push(arg);
        } catch(error) {
          output('error:', error);
        }
      }

      return output(...acc);
    };

  return addMissingMethods(newcons);

  function addMissingMethods(cons) {
    for(const [method, output] of [
      ['log', out || stdout],
      ['info', out || stdout],
      ['error', out || stderr],
      ['warn', out || stderr],
      ['debug', out || stdout],
    ]) {
      if(method == 'debug') {
        const file = getenv('DEBUG')
          .split(/[^\w.@/]/g)
          .find(s => /^@/.test(s));
        if(file) output = open(file.slice(1), 'a');
      }

      if(cons[method] === undefined) cons[method] = logFunction(outputFunction(output));
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
