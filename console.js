import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect.so';

export function Console(opts = {}) {
  let env = std.getenviron();
  const consoleWidth = (fd = 1) => {
    const size = os.ttyGetWinSize(fd);
    return Array.isArray(size) ? size[0] : undefined;
  };
  const stdoutFileno = std.out.fileno();
  const defaultBreakLength =
    (os.isatty(stdoutFileno) && consoleWidth(stdoutFileno)) || env.COLUMNS || Infinity;
  const options = {
    depth: 2,
    colors: os.isatty(stdoutFileno),
    breakLength: defaultBreakLength,
    maxArrayLength: Infinity,
    compact: false,
    customInspect: true,
    ...opts
  };

  let c = globalThis.console;

  let log = c.log;
  c.reallog = log;
  /*
  class Console {
    config(obj = {}) {
      return new ConsoleOptions(obj);
    }
  }*/

  function ConsoleOptions(obj = {}) {
    Object.assign(this, obj);
  }
  ConsoleOptions.prototype.merge = function(...args) {
    return Object.assign(this, ...args);
  };
  ConsoleOptions.merge = function(opts, ...args) {
    return new ConsoleOptions(opts).merge(...args);
  };

  let newcons = Object.create(Console.prototype);

  if(globalThis.inspect !== inspect) globalThis.inspect = inspect;

  return Object.assign(newcons, {
    options,
    reallog: log,
    inspect(...args) {
      let [obj, opts] = args;
      if(args.length == 0) obj = this;
      return inspect(obj, ConsoleOptions.merge(this.options, opts));
    },
    log(...args) {
      let tempOpts = new ConsoleOptions(this.options);
      return log.call(this,
        ...args.reduce((acc, arg) => {
          if(typeof arg && arg != null && arg instanceof ConsoleOptions) tempOpts.merge(arg);
          else if(typeof arg == 'object') acc.push(inspect(arg, tempOpts));
          else acc.push(arg);
          return acc;
        }, [])
      );
    }
  });

  function addMissingMethods(cons) {
    let fns = {};

    for(let method of ['error', 'warn', 'debug']) {
      if(cons[method] === undefined) fns[method] = cons.log;
    }
    return Object.assign(cons, fns);
  }

  globalThis.console = addMissingMethods(ret);
}

export default Console;
