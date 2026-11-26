// @portable: also run on node, bun and deno by ctest (tests/deno-import-map.json for deno)
/* path.win32: results equal node:path's win32 for the table below, generated with
 * node v23.11 from the inputs of drive letters, UNC and device paths, mixed
 * separators and trailing separators. */
import path from 'path';
import { assert, eq, tests } from '../../lib/tinytest.js';

const { win32 } = path;

/* Bun's and Deno's own win32 have other Function.length values and device-path results */
const other = typeof Bun != 'undefined' || typeof Deno != 'undefined';

/* [function, arguments, result] */
const golden = [
  [ "basename", [ "C:\\a\\b" ], "b" ],
  [ "dirname", [ "C:\\a\\b" ], "C:\\a" ],
  [ "extname", [ "C:\\a\\b" ], "" ],
  [ "normalize", [ "C:\\a\\b" ], "C:\\a\\b" ],
  [ "isAbsolute", [ "C:\\a\\b" ], true ],
  [ "parse", [ "C:\\a\\b" ], { "root": "C:\\", "dir": "C:\\a", "base": "b", "ext": "", "name": "b" } ],
  [ "toNamespacedPath", [ "C:\\a\\b" ], "\\\\?\\C:\\a\\b" ],
  [ "basename", [ "c:/x" ], "x" ],
  [ "dirname", [ "c:/x" ], "c:/" ],
  [ "extname", [ "c:/x" ], "" ],
  [ "normalize", [ "c:/x" ], "c:\\x" ],
  [ "isAbsolute", [ "c:/x" ], true ],
  [ "parse", [ "c:/x" ], { "root": "c:/", "dir": "c:/", "base": "x", "ext": "", "name": "x" } ],
  [ "toNamespacedPath", [ "c:/x" ], "\\\\?\\c:\\x" ],
  [ "basename", [ "C:" ], "" ],
  [ "dirname", [ "C:" ], "C:" ],
  [ "extname", [ "C:" ], "" ],
  [ "normalize", [ "C:" ], "C:." ],
  [ "isAbsolute", [ "C:" ], false ],
  [ "parse", [ "C:" ], { "root": "C:", "dir": "C:", "base": "", "ext": "", "name": "" } ],
  [ "toNamespacedPath", [ "C:" ], "\\\\?\\C:\\home\\roman\\Sources\\quickjs\\qjs-modules" ],
  [ "basename", [ "C:x" ], "x" ],
  [ "dirname", [ "C:x" ], "C:" ],
  [ "extname", [ "C:x" ], "" ],
  [ "normalize", [ "C:x" ], "C:x" ],
  [ "isAbsolute", [ "C:x" ], false ],
  [ "parse", [ "C:x" ], { "root": "C:", "dir": "C:", "base": "x", "ext": "", "name": "x" } ],
  [ "toNamespacedPath", [ "C:x" ], "\\\\?\\C:\\home\\roman\\Sources\\quickjs\\qjs-modules\\x" ],
  [ "basename", [ "C:\\" ], "" ],
  [ "dirname", [ "C:\\" ], "C:\\" ],
  [ "extname", [ "C:\\" ], "" ],
  [ "normalize", [ "C:\\" ], "C:\\" ],
  [ "isAbsolute", [ "C:\\" ], true ],
  [ "parse", [ "C:\\" ], { "root": "C:\\", "dir": "C:\\", "base": "", "ext": "", "name": "" } ],
  [ "toNamespacedPath", [ "C:\\" ], "\\\\?\\C:\\" ],
  [ "basename", [ "\\\\srv\\share\\x" ], "x" ],
  [ "dirname", [ "\\\\srv\\share\\x" ], "\\\\srv\\share\\" ],
  [ "extname", [ "\\\\srv\\share\\x" ], "" ],
  [ "normalize", [ "\\\\srv\\share\\x" ], "\\\\srv\\share\\x" ],
  [ "isAbsolute", [ "\\\\srv\\share\\x" ], true ],
  [ "parse", [ "\\\\srv\\share\\x" ], { "root": "\\\\srv\\share\\", "dir": "\\\\srv\\share\\", "base": "x", "ext": "", "name": "x" } ],
  [ "toNamespacedPath", [ "\\\\srv\\share\\x" ], "\\\\?\\UNC\\srv\\share\\x" ],
  [ "basename", [ "//srv/share/" ], "share" ],
  [ "dirname", [ "//srv/share/" ], "//srv/share/" ],
  [ "extname", [ "//srv/share/" ], "" ],
  [ "normalize", [ "//srv/share/" ], "\\\\srv\\share\\" ],
  [ "isAbsolute", [ "//srv/share/" ], true ],
  [ "parse", [ "//srv/share/" ], { "root": "//srv/share/", "dir": "//srv/share/", "base": "", "ext": "", "name": "" } ],
  [ "toNamespacedPath", [ "//srv/share/" ], "\\\\?\\UNC\\srv\\share\\" ],
  [ "basename", [ "\\\\srv" ], "srv" ],
  [ "dirname", [ "\\\\srv" ], "\\" ],
  [ "extname", [ "\\\\srv" ], "" ],
  [ "normalize", [ "\\\\srv" ], "\\srv" ],
  [ "isAbsolute", [ "\\\\srv" ], true ],
  [ "parse", [ "\\\\srv" ], { "root": "\\", "dir": "\\", "base": "srv", "ext": "", "name": "srv" } ],
  [ "toNamespacedPath", [ "\\\\srv" ], "\\srv" ],
  [ "basename", [ "\\\\?\\C:\\x" ], "x" ],
  [ "dirname", [ "\\\\?\\C:\\x" ], "\\\\?\\C:\\" ],
  [ "extname", [ "\\\\?\\C:\\x" ], "" ],
  [ "normalize", [ "\\\\?\\C:\\x" ], "\\\\?\\C:\\x" ],
  [ "isAbsolute", [ "\\\\?\\C:\\x" ], true ],
  [ "parse", [ "\\\\?\\C:\\x" ], { "root": "\\\\?\\C:\\", "dir": "\\\\?\\C:\\", "base": "x", "ext": "", "name": "x" } ],
  [ "toNamespacedPath", [ "\\\\?\\C:\\x" ], "\\\\?\\C:\\x" ],
  [ "basename", [ "\\\\.\\COM1" ], "COM1" ],
  [ "dirname", [ "\\\\.\\COM1" ], "\\\\.\\COM1" ],
  [ "extname", [ "\\\\.\\COM1" ], "" ],
  [ "normalize", [ "\\\\.\\COM1" ], "\\\\.\\COM1" ],
  [ "isAbsolute", [ "\\\\.\\COM1" ], true ],
  [ "parse", [ "\\\\.\\COM1" ], { "root": "\\\\.\\COM1", "dir": "\\\\.\\COM1", "base": "", "ext": "", "name": "" } ],
  [ "toNamespacedPath", [ "\\\\.\\COM1" ], "\\\\.\\COM1" ],
  [ "basename", [ "a\\b\\..\\c" ], "c" ],
  [ "dirname", [ "a\\b\\..\\c" ], "a\\b\\.." ],
  [ "extname", [ "a\\b\\..\\c" ], "" ],
  [ "normalize", [ "a\\b\\..\\c" ], "a\\c" ],
  [ "isAbsolute", [ "a\\b\\..\\c" ], false ],
  [ "parse", [ "a\\b\\..\\c" ], { "root": "", "dir": "a\\b\\..", "base": "c", "ext": "", "name": "c" } ],
  [ "toNamespacedPath", [ "a\\b\\..\\c" ], "\\home\\roman\\Sources\\quickjs\\qjs-modules\\a\\c" ],
  [ "basename", [ "a/b//c/" ], "c" ],
  [ "dirname", [ "a/b//c/" ], "a/b/" ],
  [ "extname", [ "a/b//c/" ], "" ],
  [ "normalize", [ "a/b//c/" ], "a\\b\\c\\" ],
  [ "isAbsolute", [ "a/b//c/" ], false ],
  [ "parse", [ "a/b//c/" ], { "root": "", "dir": "a/b/", "base": "c", "ext": "", "name": "c" } ],
  [ "toNamespacedPath", [ "a/b//c/" ], "\\home\\roman\\Sources\\quickjs\\qjs-modules\\a\\b\\c" ],
  [ "basename", [ "C:\\a\\..\\..\\b" ], "b" ],
  [ "dirname", [ "C:\\a\\..\\..\\b" ], "C:\\a\\..\\.." ],
  [ "extname", [ "C:\\a\\..\\..\\b" ], "" ],
  [ "normalize", [ "C:\\a\\..\\..\\b" ], "C:\\b" ],
  [ "isAbsolute", [ "C:\\a\\..\\..\\b" ], true ],
  [ "parse", [ "C:\\a\\..\\..\\b" ], { "root": "C:\\", "dir": "C:\\a\\..\\..", "base": "b", "ext": "", "name": "b" } ],
  [ "toNamespacedPath", [ "C:\\a\\..\\..\\b" ], "\\\\?\\C:\\b" ],
  [ "basename", [ "" ], "" ],
  [ "dirname", [ "" ], "." ],
  [ "extname", [ "" ], "" ],
  [ "normalize", [ "" ], "." ],
  [ "isAbsolute", [ "" ], false ],
  [ "parse", [ "" ], { "root": "", "dir": "", "base": "", "ext": "", "name": "" } ],
  [ "toNamespacedPath", [ "" ], "" ],
  [ "basename", [ "." ], "." ],
  [ "dirname", [ "." ], "." ],
  [ "extname", [ "." ], "" ],
  [ "normalize", [ "." ], "." ],
  [ "isAbsolute", [ "." ], false ],
  [ "parse", [ "." ], { "root": "", "dir": "", "base": ".", "ext": "", "name": "." } ],
  [ "toNamespacedPath", [ "." ], "\\home\\roman\\Sources\\quickjs\\qjs-modules" ],
  [ "basename", [ "..\\x" ], "x" ],
  [ "dirname", [ "..\\x" ], ".." ],
  [ "extname", [ "..\\x" ], "" ],
  [ "normalize", [ "..\\x" ], "..\\x" ],
  [ "isAbsolute", [ "..\\x" ], false ],
  [ "parse", [ "..\\x" ], { "root": "", "dir": "..", "base": "x", "ext": "", "name": "x" } ],
  [ "toNamespacedPath", [ "..\\x" ], "\\home\\roman\\Sources\\quickjs\\x" ],
  [ "basename", [ "\\a" ], "a" ],
  [ "dirname", [ "\\a" ], "\\" ],
  [ "extname", [ "\\a" ], "" ],
  [ "normalize", [ "\\a" ], "\\a" ],
  [ "isAbsolute", [ "\\a" ], true ],
  [ "parse", [ "\\a" ], { "root": "\\", "dir": "\\", "base": "a", "ext": "", "name": "a" } ],
  [ "toNamespacedPath", [ "\\a" ], "\\a" ],
  [ "basename", [ "file.tar.gz" ], "file.tar.gz" ],
  [ "dirname", [ "file.tar.gz" ], "." ],
  [ "extname", [ "file.tar.gz" ], ".gz" ],
  [ "normalize", [ "file.tar.gz" ], "file.tar.gz" ],
  [ "isAbsolute", [ "file.tar.gz" ], false ],
  [ "parse", [ "file.tar.gz" ], { "root": "", "dir": "", "base": "file.tar.gz", "ext": ".gz", "name": "file.tar" } ],
  [ "toNamespacedPath", [ "file.tar.gz" ], "\\home\\roman\\Sources\\quickjs\\qjs-modules\\file.tar.gz" ],
  [ "basename", [ "C:\\dir\\.hidden" ], ".hidden" ],
  [ "dirname", [ "C:\\dir\\.hidden" ], "C:\\dir" ],
  [ "extname", [ "C:\\dir\\.hidden" ], "" ],
  [ "normalize", [ "C:\\dir\\.hidden" ], "C:\\dir\\.hidden" ],
  [ "isAbsolute", [ "C:\\dir\\.hidden" ], true ],
  [ "parse", [ "C:\\dir\\.hidden" ], { "root": "C:\\", "dir": "C:\\dir", "base": ".hidden", "ext": "", "name": ".hidden" } ],
  [ "toNamespacedPath", [ "C:\\dir\\.hidden" ], "\\\\?\\C:\\dir\\.hidden" ],
  [ "basename", [ "C:\\a\\b\\" ], "b" ],
  [ "dirname", [ "C:\\a\\b\\" ], "C:\\a" ],
  [ "extname", [ "C:\\a\\b\\" ], "" ],
  [ "normalize", [ "C:\\a\\b\\" ], "C:\\a\\b\\" ],
  [ "isAbsolute", [ "C:\\a\\b\\" ], true ],
  [ "parse", [ "C:\\a\\b\\" ], { "root": "C:\\", "dir": "C:\\a", "base": "b", "ext": "", "name": "b" } ],
  [ "toNamespacedPath", [ "C:\\a\\b\\" ], "\\\\?\\C:\\a\\b" ],
  [ "basename", [ "C:\\x\\y.txt", ".txt" ], "y" ],
  [ "basename", [ "C:\\x\\y.txt", "y.txt" ], "y.txt" ],
  [ "basename", [ "C:\\x\\", "x" ], "x" ],
  [ "join", [ "C:\\a", "b" ], "C:\\a\\b" ],
  [ "join", [ "C:\\a\\", "..", "c" ], "C:\\c" ],
  [ "join", [ "\\\\srv\\share", "x" ], "\\\\srv\\share\\x" ],
  [ "join", [ "a", "", "b" ], "a\\b" ],
  [ "join", [], "." ],
  [ "join", [ "", "" ], "." ],
  [ "join", [ "C:", "x" ], "C:\\x" ],
  [ "join", [ "//srv", "share", "x" ], "\\\\srv\\share\\x" ],
  [ "relative", [ "C:\\a\\b", "C:\\a\\c" ], "..\\c" ],
  [ "relative", [ "C:\\a", "D:\\b" ], "D:\\b" ],
  [ "relative", [ "C:\\a\\b", "C:\\a\\b\\c\\d" ], "c\\d" ],
  [ "relative", [ "C:\\A", "c:\\a\\b" ], "b" ],
  [ "relative", [ "C:\\a", "C:\\a" ], "" ],
  [ "resolve", [ "C:\\a", "b" ], "C:\\a\\b" ],
  [ "resolve", [ "C:\\a", "..\\..\\b" ], "C:\\b" ],
  [ "resolve", [ "C:\\a", "D:\\c", "e" ], "D:\\c\\e" ],
  [ "resolve", [ "\\\\srv\\share", "x" ], "\\\\srv\\share\\x" ],
  [ "resolve", [ "C:\\a", "\\b" ], "C:\\b" ],
  [ "format", [ { "dir": "C:\\x", "base": "y.txt" } ], "C:\\x\\y.txt" ],
  [ "format", [ { "root": "C:\\", "name": "a", "ext": "b" } ], "C:\\a.b" ],
  [ "format", [ { "dir": "C:\\", "base": "a" } ], "C:\\\\a" ],
  [ "format", [ { "name": "n", "ext": ".e" } ], "n.e" ],
  [ "format", [ { "root": "C:\\", "dir": "C:\\x", "name": "y", "ext": ".z" } ], "C:\\x\\y.z" ],
];

tests({
  'win32 has the separator, the delimiter and links to itself'() {
    eq(win32.sep, '\\');
    eq(win32.delimiter, ';');
    assert(win32.win32 === win32);
    assert(typeof win32.posix.join == 'function');
  },
  'Function.length of the win32 functions as Node'() {
    if(other) return;

    for(const [name, len] of Object.entries({ basename: 2, dirname: 1, extname: 1, format: 1, isAbsolute: 1, join: 0, normalize: 1, parse: 1, relative: 2, resolve: 0, toNamespacedPath: 1 })) eq(win32[name].length, len, name);
  },
  'golden table of node win32 results'() {
    if(other) return;

    for(const [name, args, expected] of golden) eq(JSON.stringify(win32[name](...args)), JSON.stringify(expected), name + JSON.stringify(args));
  },
  'a destructured function works without its object'() {
    const { join, basename } = win32;

    eq(join('C:\\a', 'b'), 'C:\\a\\b');
    eq(basename('C:\\a\\b.txt', '.txt'), 'b');
  },
  'a non-string argument throws ERR_INVALID_ARG_TYPE'() {
    let err;

    try {
      win32.join('a', 5);
    } catch(e) {
      err = e;
    }

    eq(err && err.code, 'ERR_INVALID_ARG_TYPE');
  },
});
