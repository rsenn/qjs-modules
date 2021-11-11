import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';

export function Console(...args) {
  let [out, opts = {}] = args;
  if(typeof out == 'object' && out != null) {
    if(typeof out.write == 'function') {
    } else if('inspectOptions' in out) {
      opts = out;
      out = std.out;
    }
  }
  //console.log('out:', out);
  let env = std.getenviron();
  let stdioFds = [std.in, std.out, std.err].map(f => f.fileno());
  let termFd = stdioFds[1];
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
    depth: 2,
    colors: isTerminal,
    stringBreakNewline: false,
    maxArrayLength: 30,
    maxStringLength: 1024,
    compact: 1,
    customInspect: true,
    hideKeys: [Symbol.toStringTag]
  };
  let options = {
    ...defaultOptions,
    ...(opts.inspectOptions ?? {})
  };

  options.breakLength ??= defaultBreakLength;

  let c = globalThis.console;
  let log = c.log;
  let newcons = Object.create(Console.prototype);
  if(globalThis.inspect !== inspect) globalThis.inspect = inspect;
  if(!globalThis.inspect) globalThis.inspect = arg => arg;

  const outputFunction =
    out =>
    (...args) =>
      out.puts(args.join(' ') + '\n');

  const inspectFunction = (...args) => {
    let [obj, opts] = args;
    if(args.length == 0) obj = newcons;
    return inspect(obj, ConsoleOptions.merge(newcons.options, opts));
  };

  const logFunction = output =>
    function(...args) {
      let tempOpts = new ConsoleOptions(newcons.options);
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
            acc.push(typeof arg == 'string' ? arg : inspectFunction(arg, tempOpts));
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
  globalThis.console = newcons;

  return addMissingMethods(newcons);

  function addMissingMethods(cons) {
    let fns = {};

    for(let [method, output] of [
      ['log', out || std.out],
      ['info', out || std.out],
      ['error', out || std.err],
      ['warn', out || std.err],
      ['debug', out || std.out]
    ]) {
      if(cons[method] === undefined) fns[method] = logFunction(outputFunction(output));
    }
    return Object.assign(cons, fns);
  }
}

Console.prototype.config = function config(obj = {}) {
  return new ConsoleOptions(obj);
};

function ConsoleOptions(obj = {}) {
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

globalThis.console = new Console();

export default function ConsoleSetup(inspectOptions = {}, callback) {
  globalThis.console = new Console({ inspectOptions });
  if(typeof callback == 'functions') callback(globalThis.console);
  return globalThis.console;
}
