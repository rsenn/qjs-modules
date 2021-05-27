function isNumber(n) {
  return !isNaN(parseFloat(n)) && isFinite(n);
}

function Capitalize(str) {
  return str.charAt(0).toUpperCase() + str.substring(1);
}

function MakeGetter(p) {
  return function() {
    return this[p];
  };
}

const booleanProps = ['isConstructor', 'isEval', 'isNative', 'isToplevel'];
const numericProps = ['columnNumber', 'lineNumber'];
const stringProps = ['fileName', 'functionName', 'source'];
const arrayProps = ['args'];
const objectProps = ['evalOrigin'];

const props = booleanProps.concat(numericProps, stringProps, arrayProps, objectProps);

export function Stack(arg) {
if(typeof arg == 'string') 
  arg = arg.split(/\n/g).filter(line => line !== '');
arg = [...arg].map(frame => (typeof frame == 'string' ? StackFrame.fromString(frame) : new StackFrame(frame)) ?? frame);

//arg = arg.map((frame,i) => [i,frame]).reverse();
console.log("arg",arg);

return Object.setPrototypeOf(arg, Stack.prototype);
}

Stack.prototype[Symbol.toStringTag] = 'Stack';


Stack.prototype[Symbol.inspect ?? Symbol.for('quickjs.inspect.custom')] = function(depth, options) {
   return Object.setPrototypeOf({...this}, Stack.prototype);
  }

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
          let [lineNumber, columnNumber=null] = m.map(n => n.slice(1));
  [lineNumber,columnNumber] = [lineNumber,columnNumber].map(n =>  !isNaN(+n) ? +n : n);
          obj = { functionName, fileName };

          if(lineNumber  > 0) obj.lineNumber = lineNumber;
          if(columnNumber >0) obj.columnNumber = columnNumber;
        }
      }
    }
    for(var i = 0; i < props.length; i++) {
      if(obj[props[i]] !== undefined) {
        this['set' + Capitalize(props[i])](obj[props[i]]);
      }
    }
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
    var fileName = this.getFileName() || '';
    var lineNumber = this.getLineNumber() || '';
    var columnNumber = this.getColumnNumber() || '';
    var functionName = this.getFunctionName() || '';
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
    var i = str.indexOf('(');
    var j = str.lastIndexOf(')');

    var functionName = str.substring(0, i).replace(/^\s*at\s+/, '').trimEnd();
    var args = str.substring(i + 1, j).split(',');
    var locationString = str.substring(i + 1, j);
console.log('locationString',locationString);

      var parts;

      if((parts =  /(.+?)(?::(\d+))?(?::(\d+))?$/.exec(locationString, ''))) {
      var fileName = parts[1];
      var lineNumber = parts[2];
      var columnNumber = parts[3];
       }

       if(!functionName && !fileName)
        return null;

    return new StackFrame({
      functionName: functionName,
      args: args || undefined,
      fileName: fileName,
      lineNumber: lineNumber || undefined,
      columnNumber: columnNumber || undefined
    });
  }

  get [Symbol.toStringTag]() { return "StackFrame"; };

  [Symbol.inspect ?? Symbol.for('quickjs.inspect.custom')](depth, options) {
    const { functionName, fileName, lineNumber, columnNumber } = this;

    let obj = Object.setPrototypeOf({functionName,fileName}, StackFrame.prototype);
    if(lineNumber  > 0) obj.lineNumber=lineNumber;
    if(columnNumber  > 0) obj.columnNumber=columnNumber;

    return obj;
  }
}

for(var i = 0; i < booleanProps.length; i++) {
  StackFrame.prototype['get' + Capitalize(booleanProps[i])] = MakeGetter(booleanProps[i]);
  StackFrame.prototype['set' + Capitalize(booleanProps[i])] = (function (p) {
    return function(v) {
      this[p] = Boolean(v);
    };
  })(booleanProps[i]);
}

for(var j = 0; j < numericProps.length; j++) {
  StackFrame.prototype['get' + Capitalize(numericProps[j])] = MakeGetter(numericProps[j]);
  StackFrame.prototype['set' + Capitalize(numericProps[j])] = (function (p) {
    return function(v) {
      if(!isNumber(v)) {
        throw new TypeError(p + ' must be a Number');
      }
      this[p] = Number(v);
    };
  })(numericProps[j]);
}

for(var k = 0; k < stringProps.length; k++) {
  StackFrame.prototype['get' + Capitalize(stringProps[k])] = MakeGetter(stringProps[k]);
  StackFrame.prototype['set' + Capitalize(stringProps[k])] = (function (p) {
    return function(v) {
      this[p] = String(v);
    };
  })(stringProps[k]);
}

export default StackFrame;
