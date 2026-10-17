#!/bin/sh
# Dumps the surface of node:path in node, bun and deno (and 'path' in the
# in-tree qjsm) in describe.js's flat format, then compares the name sets.
#
# Usage: dump-node-path.sh [-o DIR] [--qjsm FILE]
#
#   -o DIR       where the dumps go (default /tmp/node-path-surface)
#   --qjsm FILE  qjsm for the 'path' dump (default build/<cc -dumpmachine>/qjsm)
#
# Output in DIR, for each runtime R in node bun deno qjsm:
#   R.args.txt     --flat, with parameter names where the runtime has them
#   R.noargs.txt   --flat --no-arguments
#   R.<group>      sorted names of one group: default posix win32 named.function
#                  named.value named.object
#   compare.txt    what differs between each runtime and node
#
# Deno is run without a memory limit (it aborts on startup under `ulimit -v`);
# the other runtimes are run under timeout only, as describe.sh does.

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
out=/tmp/node-path-surface
qjsm=

while [ $# -gt 0 ]; do
  case $1 in
    -o) out=$2; shift 2 ;;
    --qjsm) qjsm=$2; shift 2 ;;
    -h|--help) sed -n '2,/^$/{/^$/d;s/^# \{0,1\}//;p;}' "$0"; exit 0 ;;
    *) echo "dump-node-path.sh: unknown argument '$1'" >&2; exit 1 ;;
  esac
done

[ -n "$qjsm" ] || qjsm="$dir/build/$(cc -dumpmachine 2>/dev/null)/qjsm"
mkdir -p "$out" || exit 1

# describe.sh finds qjsm on PATH: put the chosen one first
bin=$(mktemp -d) || exit 1
trap 'rm -rf "$bin"' EXIT INT TERM
[ -x "$qjsm" ] && ln -s "$(CDPATH= cd -- "$(dirname -- "$qjsm")" && pwd)/$(basename -- "$qjsm")" "$bin/qjsm"
PATH="$bin:$PATH"
[ -d "$(dirname -- "$qjsm")" ] && QUICKJS_MODULE_PATH=$(dirname -- "$qjsm")
export QUICKJS_MODULE_PATH

for r in node bun deno qjsm; do
  command -v "$r" >/dev/null 2>&1 || { echo "dump-node-path.sh: no $r, skipped" >&2; continue; }
  spec=node:path
  [ $r != qjsm ] || spec=path
  "$dir/utilities/describe.sh" -r $r --flat "$spec" >"$out/$r.args.txt" 2>"$out/$r.err"
  "$dir/utilities/describe.sh" -r $r --flat --no-arguments "$spec" >"$out/$r.noargs.txt" 2>>"$out/$r.err"

  f=$out/$r.noargs.txt
  for kind in function value object; do
    grep "^$kind " "$f" | sed -E 's/^[a-z]+ //; s/[(:].*//' | sort -u >"$out/$r.named.$kind"
  done
  for g in default posix win32; do
    grep "^$g\." "$f" | sed -E "s/^$g\.//; s/[(:].*//" | sort -u >"$out/$r.$g"
  done
  echo "$r: $(wc -l <"$f") lines"
done

{
  for r in bun deno qjsm; do
    [ -s "$out/$r.noargs.txt" ] || continue
    echo "== $r against node"
    for k in named.function named.value named.object default posix win32; do
      only_node=$(comm -23 "$out/node.$k" "$out/$r.$k" | tr '\n' ' ')
      only_r=$(comm -13 "$out/node.$k" "$out/$r.$k" | tr '\n' ' ')
      [ -z "$only_node$only_r" ] || printf '%s\n  only in node: %s\n  only in %s: %s\n' "$k" "$only_node" "$r" "$only_r"
    done
    echo
  done
} >"$out/compare.txt"

cat "$out/compare.txt"
