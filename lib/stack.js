const inspectSymbol = Symbol.inspect ?? Symbol.for('quickjs.inspect.custom');

const booleanProps = ['isConstructor', 'isEval', 'isNative', 'isToplevel'];
const numericProps = ['columnNumber', 'lineNumber'];
const stringProps = ['fileName', 'functionName', 'source'];
const arrayProps = ['args'];
const objectProps = ['evalOrigin'];

const props = [...booleanProps, ...numericProps, ...stringProps, ...arrayProps, ...objectProps];

export function Stack(arg) {
  arg ??= new Error().stack;

  if(typeof arg == 'string') arg = arg.split(/\n/g).filter(line => line !== '');

  arg = [...arg].map(frame =>
      (typeof frame == 'string' ? StackFrame.fromString(frame) : new StackFrame(frame)) ?? frame
  );

  return Object.setPrototypeOf({ ...arg, length: arg.length }, Stack.prototype);
}

for(let method of [
  'slice',
  'indexOf',
  'lastIndexOf',
  'find',
  'findIndex',
  'findLastIndex',
  'entries',
  'values'
])
  Stack.prototype[method] = Array.prototype[method];

Stack.prototype.toString = function() {
  return [...this].map(frame => frame.raw ?? frame.toString()).join('\n');
};
Stack.prototype[Symbol.toStringTag] = 'Stack';
Stack.prototype[Symbol.iterator] = function* () {
  for(let i = 0; i < this.length; i++) yield this[i];
};
Stack.prototype[inspectSymbol] = function(depth, options) {
  return (`\x1b[1;31m${this[Symbol.toStringTag]}\x1b[0m ` +
    globalThis.inspect([...this], depth, { ...options, breakLength: 1000, compact: 1 })
  );
};

Stack.fromString = function fromString(str) {
  return new Stack((str + '').split(/\n/g));
};

export class StackFrame {
  constructor(obj) {
    if(!obj) return;

    if(typeof obj == 'string') {
      const str = obj;
      const re = /^\s*at\s+([^ ]+)\s\(([^:\)]+)(:[0-9]*|)(:[0-9]*|).*/;

      if(re.test(str)) {
        let m;
        if((m = m = obj.match(re))) {
          m = [...m].slice(1);
          const [functionName, fileName] = m.splice(0, 2);
          let [lineNumber, columnNumber = null] = m.map(n => n.slice(1));
          [lineNumber, columnNumber] = [lineNumber, columnNumber].map(n => (!isNaN(+n) ? +n : n));
          obj = { functionName, fileName };

          if(lineNumber > 0) obj.lineNumber = lineNumber;
          if(columnNumber > 0) obj.columnNumber = columnNumber;
        }
      }
    }
    for(let i = 0; i < props.length; i++)
      if(obj[props[i]] !== undefined) this['set' + Capitalize(props[i])](obj[props[i]]);
  }

  getArgs() {
    return this.args;
  }

  setArgs(v) {
    if(Object.prototype.toString.call(v) !== '[object Array]')
      throw new TypeError('Args must be an Array');
    this.args = v;
  }

  getEvalOrigin() {
    return this.evalOrigin;
  }

  setEvalOrigin(v) {
    if(v instanceof StackFrame) {
      this.evalOrigin = v;
    } else if(v instanceof Object) {
      this.evalOrigin = new StackFrame(v);
    } else {
      throw new TypeError('Eval Origin must be an Object or StackFrame');
    }
  }

  toString() {
    const fileName = this.getFileName() ?? '';
    const lineNumber = this.getLineNumber() ?? '';
    const columnNumber = this.getColumnNumber() ?? '';
    const functionName = this.getFunctionName() ?? '';
    if(this.getIsEval()) {
      if(fileName) {
        return '[eval] (' + fileName + ':' + lineNumber + ':' + columnNumber + ')';
      }
      return '[eval]:' + lineNumber + ':' + columnNumber;
    }
    if(functionName) {
      return functionName + ' (' + fileName + ':' + lineNumber + ':' + columnNumber + ')';
    }
    return fileName + ':' + lineNumber + ':' + columnNumber;
  }

  static fromString(str) {
    const i = str.indexOf('(');
    const j = str.lastIndexOf(')');
    const functionName = str
      .substring(0, i)
      .replace(/^\s*at\s+/, '')
      .trimEnd();
    const args = str.endsWith(')') ? null : str.substring(i + 1, j).split(',');
    const loc = str.substring(i + 1, j);
    let parts;
    if((parts = /(.+?)(?::(\d+))?(?::(\d+))?$/.exec(loc, ''))) {
      var fileName = parts[1];
      var lineNumber = parts[2];
      var columnNumber = parts[3];
    }
    if(!functionName && !fileName) return null;

    let obj = { functionName, fileName };
    if(lineNumber > 0) obj.lineNumber = lineNumber;
    if(columnNumber > 0) obj.columnNumber = columnNumber;
    if(args) obj.args = args;

    let frame = new StackFrame(obj);
    Object.defineProperty(frame, 'raw', {
      value: str,
      enumerable: false,
      configurable: true,
      writable: true
    });
    return frame;
  }

  get [Symbol.toStringTag]() { return "StackFrame"; }

  [inspectSymbol](depth, options) {
    const { functionName, fileName, lineNumber, columnNumber } = this;
    let obj = Object.setPrototypeOf({ functionName, fileName }, StackFrame.prototype);
    if(lineNumber > 0) obj.lineNumber = lineNumber;
    if(columnNumber > 0) obj.columnNumber = columnNumber;
    return obj;
  }
}

function isNumber(n) {
  return !isNaN(parseFloat(n)) && isFinite(n);
}

function Capitalize(str) {
  return str.charAt(0).toUpperCase() + str.substring(1);
}

function AddMethods(proto,
  props,
  MakeSetter,
  MakeGetter = p =>
    function() {
      return this[p];
    }
) {
  for(let i = 0; i < props.length; i++) {
    proto['get' + Capitalize(props[i])] = MakeGetter(props[i]);
    proto['set' + Capitalize(props[i])] = MakeSetter(props[i]);
  }
}

// prettier-ignore
AddMethods(StackFrame.prototype,booleanProps, p => function(v) { this[p] = Boolean(v); });
// prettier-ignore
AddMethods(StackFrame.prototype,numericProps, p => function(v) {
      if(!isNumber(v)) throw new TypeError(p + ' must be a Number');
      this[p] = Number(v);
    });
// prettier-ignore
AddMethods(StackFrame.prototype,stringProps, p => function(v) { this[p] = String(v); });

export default StackFrame;
