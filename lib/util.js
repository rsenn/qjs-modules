/*import * as std from 'std';
import * as os from 'os';
*/

import inspect from 'inspect';

export default function util() {
  return util;
}

util.prototype.constructor = util;

util.inspect = inspect;

util.types = {
  isAnyArrayBuffer(value) {
    return isObject(value) && (value instanceof ArrayBuffer || value instanceof SharedArrayBuffer);
  },
  isBigInt64Array(value) {
    return isObject(value) && value instanceof BigInt64Array;
  },
  isBigUint64Array(value) {
    return isObject(value) && value instanceof BigUint64Array;
  },
  isDate(value) {
    return isObject(value) && value instanceof Date;
  },
  isFloat32Array(value) {
    return isObject(value) && value instanceof Float32Array;
  },
  isFloat64Array(value) {
    return isObject(value) && value instanceof Float64Array;
  },
  isInt8Array(value) {
    return isObject(value) && value instanceof Int8Array;
  },
  isInt16Array(value) {
    return isObject(value) && value instanceof Int16Array;
  },
  isInt32Array(value) {
    return isObject(value) && value instanceof Int32Array;
  },
  isMap(value) {
    return isObject(value) && value instanceof Map;
  },
  isPromise(value) {
    return isObject(value) && value instanceof Promise;
  },
  isProxy(value) {
    return isObject(value) && value instanceof Proxy;
  },
  isRegExp(value) {
    return isObject(value) && value instanceof RegExp;
  },
  isSet(value) {
    return isObject(value) && value instanceof Set;
  },
  isSharedArrayBuffer(value) {
    return isObject(value) && value instanceof SharedArrayBuffer;
  },
  isUint8Array(value) {
    return isObject(value) && value instanceof Uint8Array;
  },
  isUint8ClampedArray(value) {
    return isObject(value) && value instanceof Uint8ClampedArray;
  },
  isUint16Array(value) {
    return isObject(value) && value instanceof Uint16Array;
  },
  isUint32Array(value) {
    return isObject(value) && value instanceof Uint32Array;
  },
  isWeakMap(value) {
    return isObject(value) && value instanceof WeakMap;
  },
  isWeakSet(value) {
    return isObject(value) && value instanceof WeakSet;
  }
  /* isArrayBufferView(value) {
    return isObject(value) && value instanceof ArrayBufferView;
  },
  isArgumentsObject(value) {
    return isObject(value) && value instanceof ArgumentsObject;
  },
  isArrayBuffer(value) {
    return isObject(value) && value instanceof ArrayBuffer;
  },
  isAsyncFunction(value) {
    return isObject(value) && value instanceof AsyncFunction;
  },
  isBooleanObject(value) {
    return isObject(value) && value instanceof BooleanObject;
  },
  isBoxedPrimitive(value) {
    return isObject(value) && value instanceof BoxedPrimitive;
  },
  isDataView(value) {
    return isObject(value) && value instanceof DataView;
  },
  isExternal(value) {
    return isObject(value) && value instanceof External;
  },
  isGeneratorFunction(value) {
    return isObject(value) && value instanceof GeneratorFunction;
  },
  isGeneratorObject(value) {
    return isObject(value) && value instanceof GeneratorObject;
  },
  isMapIterator(value) {
    return isObject(value) && value instanceof MapIterator;
  },
  isModuleNamespaceObject(value) {
    return isObject(value) && value instanceof ModuleNamespaceObject;
  },
  isNativeError(value) {
    return isObject(value) && value instanceof NativeError;
  },
  isNumberObject(value) {
    return isObject(value) && value instanceof NumberObject;
  },
  isSetIterator(value) {
    return isObject(value) && value instanceof SetIterator;
  },
  isStringObject(value) {
    return isObject(value) && value instanceof StringObject;
  },
  isSymbolObject(value) {
    return isObject(value) && value instanceof SymbolObject;
  },
  isTypedArray(value) {
    return isObject(value) && value instanceof TypedArray;
  }*/
};

function isObject(value) {
  return typeof value == 'object' && value !== null;
}
const inspectMethod = Symbol.for('nodejs.util.inspect.custom');

Object.assign(util, {
  [inspectMethod]() {
    let obj = { ...util };
    delete obj[inspectMethod];
    return inspect(obj, { customInspect: false });
  }
});

export { inspect } from 'inspect';
