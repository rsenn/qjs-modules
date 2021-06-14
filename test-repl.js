import * as std from 'std';
import * as os from 'os';
import * as deep from 'deep';
import * as fs from 'fs';
import * as path from 'path';
import { Console } from 'console';
import REPL from './lib/repl.js';
import inspect from 'inspect';
import { extendArray } from 'util';

const base = path.basename(scriptArgs[0], /\.[a-z]*$/);
const cmdhist = `.history-${base}`;

extendArray(Array.prototype);

function LoadHistory(filename) {
  let contents = std.loadFile(filename);
  let data;

  const parse = () => {
    try {
      data = JSON.parse(contents);
    } catch(e) {}
    if(data) return data;
    try {
      data = contents.split(/\n/g);
    } catch(e) {}
    if(data) return data;
  };

  return (parse() ?? [])
    .filter(entry => (entry + '').trim() != '')
    .map(entry => entry.replace(/\\n/g, '\n'));
}

function ReadJSON(filename) {
  let data = std.loadFile(filename);

  if(data) console.log(`ReadJSON('${filename}') ${data.length} bytes read`);
  return data ? JSON.parse(data) : null;
}

function ReadFile(name, binary) {
  let data = fs.readFileSync(name, binary ? null : 'utf-8');

  console.log(`Read ${name}: ${data.length} bytes`);

  return Object.create(null, {
    data: { value: data, enumerable: false },
    getNode: {
      value(node) {
        const { start, end } = node;
        return this.data.slice(start, end);
      },
      configurable: true
    },
    toString: {
      value() {
        return this.data;
      },
      configurable: true
    }
  });
}

function WriteFile(name, data, verbose = true) {
  if(typeof data == 'string' && !data.endsWith('\n')) data += '\n';
  let ret = fs.writeFileSync(name, data);

  if(verbose) console.log(`Wrote ${name}: ${ret} bytes`);
}
function main(...args) {
  globalThis.console = new Console({
    inspectOptions: {
      customInspect: true,
      compact: false,
      depth: Infinity,
      maxArrayLength: Infinity,
      hideKeys: ['loc', 'range']
    }
  });
  let name = path.basename(scriptArgs[0], '.js');
  let repl = new REPL(name);
  console.log('repl', repl);
  repl.help = () => {};
  let { log } = console;
  repl.show = arg => std.puts(typeof arg == 'string' ? arg : inspect(arg, { colors: true }));

  repl.cleanup = () => {
    Terminal.mousetrackingDisable();
    let hist = repl.history_get().filter((item, i, a) => a.lastIndexOf(item) == i);
    fs.writeFileSync(
      cmdhist,
      hist
        .filter(entry => (entry + '').trim() != '')
        .map(entry => entry.replace(/\n/g, '\\n') + '\n')
        .join('')
    );
    console.log(`EXIT (wrote ${hist.length} history entries)`);
    std.exit(0);
  };
  let debugLog = fs.fopen('debug.log', 'a');
  repl.debugLog = debugLog;
  //  console.log = repl.wrapPrintFunction(log, console);

  /*os.ttySetRaw(0, true);
  os.setReadHandler(0, () => repl.term_read_handler());
*/
  console.log('repl.run()', repl.runSync(name));

  /*  let client = new rpc.Socket();
  client.log = repl.wrapPrintFunction(log, console);

  import('os').then(os =>
    import('net').then(({ client }) =>
      client.connect((url, callbacks) => client({ ...url, ...callbacks }), os)
    )
  );
  console.log('client', client);*/

  Object.assign(globalThis, { repl, WriteFile, ReadFile, ReadJSON });
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  std.err.puts(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
