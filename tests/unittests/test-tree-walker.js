import { TreeWalker, TreeIterator } from 'tree_walker';
import { assert, eq, tests } from '../../lib/tinytest.js';

/* NOTE: the module's import specifier is 'tree_walker' (underscore), not
 * 'tree-walker' as doc/native/tree-walker.md's title/filename would
 * suggest - that's simply this build's module file name, not a bug. */

function fixture() {
  return { a: 1, b: { c: 2, d: 3 }, e: [10, 20] };
}

tests({
  'constructor positions at the first node, root stays the original root'() {
    const root = fixture();
    const w = new TreeWalker(root);
    assert(w.root === root);
    eq(w.depth, 0);
    eq(w.currentKey, 'a');
    eq(w.currentNode, 1);
  },
  'nextSibling() walks top-level entries in order'() {
    const root = fixture();
    const w = new TreeWalker(root);
    eq(w.currentKey, 'a');
    w.nextSibling();
    eq(w.currentKey, 'b');
    assert(w.currentNode === root.b);
    w.nextSibling();
    eq(w.currentKey, 'e');
    assert(w.currentNode === root.e);
  },
  'previousSibling() walks back'() {
    const root = fixture();
    const w = new TreeWalker(root);
    w.nextSibling();
    w.nextSibling();
    eq(w.currentKey, 'e');
    w.previousSibling();
    eq(w.currentKey, 'b');
    w.previousSibling();
    eq(w.currentKey, 'a');
  },
  'firstChild()/lastChild() descend, parentNode() returns to a real parent'() {
    const root = fixture();
    const w = new TreeWalker(root);
    w.nextSibling(); // -> b
    eq(w.currentKey, 'b');
    w.firstChild(); // -> b.c
    eq(w.currentKey, 'c');
    eq(w.depth, 1);
    const parent = w.parentNode();
    assert(parent === root.b);
    eq(w.currentKey, 'b');

    w.lastChild(); // -> b.d (last child of b)
    eq(w.currentKey, 'd');
    eq(w.currentNode, 3);
  },
  'currentPath tracks the path from the root'() {
    const root = fixture();
    const w = new TreeWalker(root);
    w.nextSibling(); // -> b
    w.firstChild(); // -> b.c
    eq(JSON.stringify(w.currentPath), JSON.stringify(['b', 'c']));
  },
  'length/index describe the current node among its siblings'() {
    const root = fixture();
    const w = new TreeWalker(root);
    eq(w.length, 3); // 3 top-level entries: a, b, e
    eq(w.index, 0);
    w.nextSibling();
    eq(w.index, 1);
  },
  'nextNode() visits the whole tree in document order, then returns undefined'() {
    const root = fixture();
    const w = new TreeWalker(root);
    const keys = [w.currentKey];
    let guard = 0;
    let ret;
    while((ret = w.nextNode()) !== undefined && guard++ < 20) keys.push(w.currentKey);
    eq(JSON.stringify(keys), JSON.stringify(['a', 'b', 'c', 'd', 'e', 0, 1]));
    eq(ret, undefined);
  },
  'TreeIterator drives a for-of over the tree, starting after the initial node'() {
    /* Like TreeWalker#nextNode(), the iterator's next() always advances
     * before returning - it never re-yields the node the walker/iterator
     * was constructed at ('a': 1 here), so a for-of starting fresh begins
     * one node in. */
    const root = fixture();
    const it = new TreeIterator(root);
    const values = [...it];
    eq(JSON.stringify(values), JSON.stringify([{ c: 2, d: 3 }, 2, 3, [10, 20], 10, 20]));
  },
  'TreeIterator.next() follows the iterator protocol'() {
    const root = fixture();
    const it = new TreeIterator(root);
    const first = it.next();
    eq(first.done, false);
    eq(JSON.stringify(first.value), JSON.stringify({ c: 2, d: 3 }));
  },
});
