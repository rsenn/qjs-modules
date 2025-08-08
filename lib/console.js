import * as os from 'os';
import inspect from 'inspect';
import * as std from 'std';
import { define, properties, getPerformanceCounter } from 'util';

function isStream(obj) {
  return typeof obj == 'object' && typeof obj.write == 'function';
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

  let env = std.getenviron();
  let stdioFds = [std.in, std.out, std.err].map((f, i) => {
    try {
      return f.fileno();
    } catch(e) {
      return i;
    }
  });

  let termFd = stdioFds.find(fd => os.isatty(fd)) ?? stdioFds[1];
  if(typeof termFd != 'number') termFd = 1;

  const consoleWidth = (fd = termFd) => {
    let size;
    try {
      size = os.ttyGetWinSize(fd);
    } catch(err) {}
    return Array.isArray(size) ? size[0] : undefined;
  };

  const isTerminal = os.isatty(termFd);

  const defaultBreakLength = (isTerminal && consoleWidth()) || env.COLUMNS || Infinity;
  const defaultOptions = {
    depth: Infinity,
    colors: isTerminal && !os.platform.startsWith('win'),
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

  let options = Object.setPrototypeOf(
    {
      ...defaultOptions,
      ...(opts.inspectOptions ?? {}),
    },
    null,
  );

  options.breakLength ??= defaultBreakLength;

  let newcons = Object.create(Console.prototype);
  /* if(globalThis.inspect !== inspect) globalThis.inspect = inspect;
  if(!globalThis.inspect) globalThis.inspect = arg => arg;*/

  const printFunction = out => (typeof out == 'function' ? out : typeof out.flush == 'function' ? text => (out.puts(text), out.flush()) : text => out.puts(text));

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
      let tempOpts = new ConsoleOptions(options);
      let acc = tempOpts.prefix ? [tempOpts.prefix] : [];
      let i = 0;

      for(let arg of args) {
        try {
          if(typeof arg == 'object') {
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

  newcons.options = options;
  //globalThis.console = newcons;

  return addMissingMethods(newcons);

  function addMissingMethods(cons) {
    let fns = {};

    for(let [method, output] of [
      ['log', out || std.out],
      ['info', out || std.out],
      ['error', out || std.err],
      ['warn', out || std.err],
      ['debug', out || std.out],
    ]) {
      if(cons[method] === undefined) fns[method] = logFunction(outputFunction(output));
    }
    return Object.assign(cons, fns);
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
  if(typeof callback == 'function') callback(globalThis.console);
  return globalThis.console;
}

export { default as inspect } from 'inspect';
