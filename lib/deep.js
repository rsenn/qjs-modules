function ReturnValuePath(value, path, flags) {
  if(flags & PATH_AS_STRING) path = path.join('.');

  switch (flags & RETURN_MASK) {
    case RETURN_VALUE_PATH:
      return [value, path];
    case RETURN_PATH_VALUE:
      return [path, value];
    case RETURN_PATH:
      return path;
    case RETURN_VALUE:
      return value;
  }
}

function ReturnValuePathFunction(flags) {
  switch (flags & RETURN_MASK) {
    case RETURN_VALUE_PATH:
      return (value, path) => [value, path];
    case RETURN_PATH_VALUE:
      return (value, path) => [path, value];
    case RETURN_PATH:
      return (value, path) => path;
    case RETURN_VALUE:
      return (value, path) => value;
  }
}

export class deep {
  static RETURN_VALUE_PATH = 0;
  static RETURN_PATH = 1 << 24;
  static RETURN_VALUE = 2 << 24;
  static RETURN_PATH_VALUE = 3 << 24;
  static RETURN_MASK = 7 << 24;
  static PATH_AS_STRING = 1 << 28;
  static NO_THROW = 1 << 29;
  static MAXDEPTH_MASK = 0xffffff;

  static isPlainObject = obj => {
    if((obj != null ? obj.constructor : void 0) == null) return false;
    return obj.constructor.name === 'Object';
  };

  static clone = obj => {
    let out, v, key;
    out = Array.isArray(obj) ? [] : {};
    for(key in obj) {
      v = obj[key];
      out[key] = typeof v === 'object' && v !== null ? clone(v) : v;
    }
    return out;
  };

  static equals(a, b) {
    let i, k, size_a, j, ref;
    if(a === b) {
      return true;
    } else if(Array.isArray(a)) {
      if(!(Array.isArray(b) && a.length === b.length)) {
        return false;
      }
      for(i = j = 0, ref = a.length; 0 <= ref ? j < ref : j > ref; i = 0 <= ref ? ++j : --j) {
        if(!equals(a[i], b[i])) {
          return false;
        }
      }
      return true;
    } else if(isPlainObject(a)) {
      size_a = Util.size(a);
      if(!(isPlainObject(b) && size_a === Util.size(b))) {
        return false;
      }
      for(k in a) {
        if(!equals(a[k], b[k])) {
          return false;
        }
      }
      return true;
    }
    return false;
  }

  static extend(...args) {
    let destination, k, source, sources, j, len;
    (destination = args[0]), (sources = 2 <= args.length ? Array.prototype.slice.call(args, 1) : []);
    for(j = 0, len = sources.length; j < len; j++) {
      source = sources[j];
      for(k in source) {
        if(isPlainObject(destination[k]) && isPlainObject(source[k])) {
          extend(destination[k], source[k]);
        } else {
          destination[k] = clone(source[k]);
        }
      }
    }
    return destination;
  }

  static select(root, filter, flags = 0) {
    let fn = ReturnValuePathFunction(flags);

    function SelectFunction(root, filter, path = []) {
      let k,
        selected = [];
      try {
        if(filter(root, path)) selected.push(fn(root, path));
      } catch(e) {}
      if(root !== null && { object: true }[typeof root])
        for(k in root) selected = selected.concat(SelectFunction(root[k], filter, path.concat([isNaN(+k) ? k : +k])));
      return selected;
    }
    //console.log('deep.select', [filter + '', flags]);
    return SelectFunction(root, filter);
  }

  static find(node, filter, flags = 0, root) {
    let k,
      ret,
      result = null;
    let path = /*(typeof path == 'string' ? path.split(/[\.\/]/) : path) ||*/ [];
    if(!root) {
      root = node;
      result = ReturnValuePath(null, null, flags);
    }
    ret = filter(node, path, root);

    if(ret === -1) return -1;
    else if(ret) result = ReturnValuePath(node, path, flags);
    else if(typeof node == 'object' && node != null) {
      for(k in node) {
        result = find(node[k], filter, [...path, k], root);
        if(result) break;
      }
    }
    return result;
  }

  static forEach = function(...args) {
    const [value, fn, path = []] = args;
    let root = args[3] ?? value;

    fn(value, path, root);

    if(Util.isObject(value)) for(let k in value) forEach(value[k], fn, path.concat([isNaN(+k) ? k : +k]), root);
  };

  static iterate = function* (...args) {
    let [value, filter = v => true, flags = RETURN_VALUE_PATH, path = []] = args;
    //const path = Array.isArray(flags) ? flags : [];

    //if(typeof flags != 'number') flags = typeof args[3] == 'number' ? args[3] ?? RETURN_VALUE_PATH;

    let r,
      root = args[4] ?? value;

    if((r = filter(value, path, root))) yield [value, path, root];
    if(r !== -1)
      if(Util.isObject(value)) {
        for(let k in value) yield* iterate(value[k], filter, flags, path.concat([isNaN(+k) ? k : +k]), root);
      }
  };

  static flatten = (
    iter,
    dst = {},
    filter = (v, p) => typeof v != 'object' && v != null,
    map = (p, v) => [p.join('.'), v]
  ) => {
    let insert;
    if(!iter.next) iter = iterate(iter, filter);

    if(typeof dst.set == 'function') insert = (name, value) => dst.set(name, value);
    else if(typeof dst.push == 'function') insert = (name, value) => dst.push([name, value]);
    else insert = (name, value) => (dst[name] = value);

    for(let [value, path] of iter) insert(...map(path, value));

    return dst;
  };

  static get(root, path) {
    //console.log("deep.get", /*console.config({ depth:1}),*/{ root,path });
    let j, len;
    path = typeof path == 'string' ? path.split(/[\.\/]/) : [...path];
    for(j = 0, len = path.length; j < len; j++) {
      let k = path[j];
      root = root[k];
    }
    return root;
  }

  static set(root, path, value) {
    //console.log("deep.set", { root,path,value });
    path = typeof path == 'string' ? path.split(/[\.\/]/) : [...path];

    if(path.length == 0) return Object.assign(root, value);

    for(let j = 0, len = path.length; j + 1 < len; j++) {
      let pathElement = isNaN(+path[j]) ? path[j] : +path[j];
      //console.log("path element:",pathElement);
      if(!(pathElement in root)) root[pathElement] = /^[0-9]+$/.test(path[j + 1]) ? [] : {};
      root = root[pathElement];
    }
    let lastPath = path.pop();
    root[lastPath] = value;
    return root;
    return (root[lastPath] = value);
  }

  static delegate(root, path) {
    if(path) {
      const last = path.pop();
      const obj = get(root, path);
      return function(value) {
        return value !== undefined ? (obj[last] = value) : obj[last];
      };
    }
    return function(path, value) {
      return value !== undefined ? obj.set(root, path, value) : obj.get(root, path);
    };
  }

  static transform(obj, filter, t) {
    let k,
      transformed,
      v,
      j,
      len,
      path = arguments[3] == [];
    if(filter(obj, path)) {
      return t(obj, path);
    } else if(Array.isArray(obj)) {
      transformed = [];
      for(j = 0, len = obj.length; j < len; j++) {
        v = obj[j];
        transformed.push(transform(v, filter, t, [...path, j]));
      }
      return transformed;
    } else if(isPlainObject(obj)) {
      transformed = {};
      q;
      for(k in obj) {
        v = obj[k];
        transformed[k] = transform(v, filter, [...path, k]);
      }
      return transformed;
    }
    return obj;
  }

  static unset(object, path) {
    if(object && typeof object === 'object') {
      let parts = typeof path == 'string' ? path.split('.') : path;

      if(parts.length > 1) {
        unset(object[parts.shift()], parts);
      } else {
        if(Array.isArray(object) && Util.isNumeric(path)) object.splice(+path, 1);
        else delete object[path];
      }
    }
    return object;
  }

  static unflatten(map, obj = {}) {
    for(let [path, value] of map) {
      set(obj, path, value);
    }
    return obj;
  }
}

export default deep;
