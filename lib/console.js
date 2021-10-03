import * as os from 'os';
import * as std from 'std';
import inspect from 'inspect';
//import {types,getPrototypeChain,getConstructorChain} from 'util';

/*const FileProto = Object.getPrototypeOf(std.in);
const { constructor: File } = FileProto;*/

export function Console(...args) {
  const [out, err] = args;
  let opts = {};
  if(typeof out == 'object' && out != null) {
    if(typeof out.write == 'function') {
    } else if('inspectOptions' in out) {
      opts = out;
    }
  }
  let env = std.getenviron();
  let stdioFds = [std.in, std.out, std.err].map(f => f.fileno());
  let termFd = stdioFds[1]; // .find(fd => os.isatty(fd));
  if(typeof termFd != 'number') termFd = 1;
  console.log('termFd', termFd);

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
    maxArrayLength: Infinity,
    compact: false,
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
      ['log', std.out],
      ['info', std.out],
      ['error', std.err],
      ['warn', std.err],
      ['debug', std.out]
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
  if(callback) callback(globalThis.console);
  return globalThis.console;
}
