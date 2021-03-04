# quickjs-modules

Some modules for QuickJS

## deep
  - find(object, (obj,key) => {})
  - get(object, pointer)
  - set(object, pointer, value)
  - unset(object, pointer)

## inspect
  - inspect(value[, options])

## mmap
  - mmap(addr, size, prot, flags, fd, offset)
  - munmap(addr)

## path
  - basename
  - collapse
  - dirname
  - exists
  - extname
  - fnmatch
  - getcwd
  - gethome
  - getsep
  - isAbsolute
  - isRelative
  - isDirectory
  - isSymlink
  - isSeparator
  - length
  - components
  - readlink
  - realpath
  - right
  - skip
  - skipSeparator
  - absolute
  - append
  - canonical
  - concat
  - find
  - normalize
  - relative
  - join
  - parse
  - format
  - resolve
  - delimiter
  - sep

## pointer
  - new Pointer([array | string | pointer])
 
## tree-walker
  - new TreeWalker(root[, flags])
  - new TreeIterator(root[, flags])

## xml
  - read(string | arraybuffer)
  - write(object)