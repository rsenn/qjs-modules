import { inspect } from 'inspect';
import { Location } from 'location';
export { Location } from 'location';

const inspectSymbol = Symbol.for('quickjs.inspect.custom');

const booleanProps = ['isConstructor', 'isEval', 'isNative', 'isToplevel'];

const numericProps = ['columnNumber', 'lineNumber'];
const stringProps = ['fileName', 'functionName', 'source'];
const arrayProps = ['args'];
const objectProps = ['evalOrigin'];

const props = [...booleanProps, ...numericProps, ...stringProps, ...arrayProps, ...objectProps];

Error.stackTraceLimit = 10;

export function Stack(st, pred = fr => true) {
  let idx = 0;
  if(!st) {
    st = new Error().stack;
    idx = 1;
  }
  if(typeof st == 'string') st = st.split(/\n/g).filter(line => line !== '');
  st = [...st].map(frame => (typeof frame == 'string' ? StackFrame.fromString(frame) : new StackFrame(frame)) ?? frame);

  st = st.filter((fr, i) => !(fr.fileName == 'native' && !(fr.lineNumber > 0)) && pred(fr, i, st));

  if(idx) st = st.slice(idx);
  return Object.assign(this, { ...st, length: st.length });
}

Stack.prototype[Symbol.toStringTag] = 'Stack';

Stack.prototype.toString = function() {
  return [...this].map(frame => frame.toString?.() ?? frame.raw).join('\n');
};

Stack.prototype[Symbol.toPrimitive] = function() {
  return [...this].map(frame => frame.raw ?? frame.toString()).join('\n');
};

Stack.prototype[Symbol.iterator] = function* () {
  for(let i = 0; i < this.length; i++) yield this[i];
};
Stack.prototype[inspectSymbol] = function(depth, options) {
  return (
    `\x1b[1;31m${this[Symbol.toStringTag]}\x1b[0m ` +
    inspect([...this], depth, {
      ...options,
      breakLength: 1000,
      compact: 1,
    })
  );
};

Object.assign(Stack.prototype, {
  update(arg) {
    const stack = this,
      other = [...arg];
    const common = Stack.common(stack, other);
    const frames = common > 0 ? this.slice(this.length - common) : [];
    const newFrames = other.slice(0, other.length - common);
    //console.log('Stack.prototype.update', {len: stack.length,common,new: newFrames.length});

    stack.splice(0, stack.length - common, ...newFrames);
    return stack;
  },
});

Object.defineProperties(Stack, {
  [Symbol.species]: {
    get() {
      return Stack;
    },
  },
  fromString: {
    value(str) {
      return new Stack((str + '').split(/\n/g));
    },
    enumerable: false,
  },
  common: {
    value(a, b) {
      const len = Math.min(a.length, b.length);
      let i;
      for(i = 0; i < len; i++) if(!StackFrame.equal(a[a.length - (i + 1)], b[b.length - (i + 1)])) break;
      return i;
    },
    enumerable: false,
  },
  update: {
    value(stack, newStack = new Stack()) {
      if(!stack) return newStack;
      return Stack.prototype.update.call(stack, newStack);
    },
    enumerable: false,
  },
});

for(let method of ['slice', 'splice', 'indexOf', 'lastIndexOf', 'find', 'findIndex', 'findLastIndex', 'entries', 'values', 'filter', 'reverse', 'shift', 'unshift', 'push', 'pop'])
  Stack.prototype[method] = Array.prototype[method];

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
    for(let i = 0; i < props.length; i++) if(obj[props[i]] !== undefined) this['set' + Capitalize(props[i])](obj[props[i]]);
  }

  getArgs() {
    return this.args;
  }

  setArgs(v) {
    if(Object.prototype.toString.call(v) !== '[object Array]') throw new TypeError('Args must be an Array');
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
      if(fileName) return '[eval] (' + fileName + ':' + lineNumber + ':' + columnNumber + ')';
      return '[eval]:' + lineNumber + ':' + columnNumber;
    }
    if(functionName) return functionName + ' (' + fileName + ':' + lineNumber + ':' + columnNumber + ')';
    let s = fileName;
    if(lineNumber) {
      s += ':' + lineNumber;
      if(columnNumber) s += ':' + columnNumber;
    }
    return s;
  }

  equals(other) {
    for(let name of props) {
      if(!(name in other) && !(name in this)) continue;
      if(other[name] != this[name]) return false;
    }
    return true;
  }

  /* prettier-ignore */ get loc() {
    return new Location(this);
  }

  static equal(a, b) {
    return StackFrame.prototype.equals.call(a, b);
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
      writable: true,
    });
    return frame;
  }

  /* prettier-ignore */ get [Symbol.toStringTag]() {
    return 'StackFrame';
  }

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

function AddMethods(
  proto,
  props,
  MakeSetter,
  MakeGetter = p =>
    function() {
      return this[p];
    },
) {
  for(let i = 0; i < props.length; i++) {
    Object.defineProperties(proto, {
      ['get' + Capitalize(props[i])]: {
        value: MakeGetter(props[i]),
        enumerable: false,
      },

      ['set' + Capitalize(props[i])]: {
        value: MakeSetter(props[i]),
        enumerable: false,
      },
    });
    /*   proto['get' + Capitalize(props[i])] = MakeGetter(props[i]);
    proto['set' + Capitalize(props[i])] = MakeSetter(props[i]);*/
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
