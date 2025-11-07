import { weakDefine, nonenumerable } from 'util';

export const SetExtensions = nonenumerable({
  difference(other) {
    if(this.size > other.size) {
      const r = new Set(this);
      for(const e of other) r.delete(e);
      return r;
    }
    const r = new Set();
    for(const e of this) if(!other.has(e)) r.add(e);
    return r;
  },
  symmetricDifference(other) {
    return SetExtensions.union.call(SetExtensions.difference.call(this, other), SetExtensions.difference.call(other, this));
  },
  isDisjointFrom(other) {
    if(this.size > other.size) return SetExtensions.isDisjointFrom.call(other, this);
    for(const e of this) if(other.has(e)) return false;
    return true;
  },
  union(other) {
    const r = new Set(this);
    for(const e of other) r.add(e);
    return r;
  },
  intersection(other) {
    if(this.size > other.size) return SetExtensions.intersection.call(other, this);
    const r = new Set();
    for(const e of this) if(other.has(e)) r.add(e);
    return r;
  },
  isSubsetOf(other) {
    if(this.size > other.size) return false;
    for(const e of this) if(!other.has(e)) return false;
    return true;
  },
  isSupersetOf(other) {
    return SetExtensions.isSubsetOf.call(other, this);
  },
});

export const SetStatic = nonenumerable({});

export const SetPrototype = Set.prototype;
export const SetConstructor = SetPrototype.constructor;

export function extendSet(proto = SetPrototype, ctor = SetConstructor) {
  weakDefine(ctor, SetStatic);
  weakDefine(proto, SetExtensions);
}

export default extendSet;
