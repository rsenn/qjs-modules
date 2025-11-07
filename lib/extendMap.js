import { weakDefine, nonenumerable } from 'util';

export const MapExtensions = nonenumerable({
  getOrInsert(k, v) {
    if(this.has(k)) return this.get(k);
    this.set(k, v);
    return v;
  },
  getOrInsertComputed(k, fn) {
    if(this.has(k)) return this.get(k);
    const v = fn(k);
    this.set(k, v);
    return v;
  },
});

export const MapPrototype = Map.prototype;

export function extendMap(proto = MapPrototype) {
  weakDefine(proto, MapExtensions);
}

export default extendMap;
