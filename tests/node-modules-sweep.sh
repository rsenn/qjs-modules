#!/bin/bash
# node-modules-sweep.sh: imports every package of some node_modules
# directories under qjsm and node, and reports where qjsm differs.
#
#   tests/node-modules-sweep.sh [-j N] [-o OUTDIR] [-f REGEX] [NODE_MODULES_DIR...]
#
#   -j N       parallel runs (default 4)
#   -o OUTDIR  where results.tsv and the per-package logs go
#              (default ${TMPDIR:-/tmp}/node-modules-sweep)
#   -f REGEX   only packages whose name matches (grep -E)
#
#   QJSM  qjsm binary (default build/$(cc -dumpmachine)/qjsm, else qjsm)
#   NODE  node binary, the reference (default node)
#
# each package is imported by its bare name from a directory whose
# node_modules is a symlink to the swept one, so qjsm resolves it through
# lib/loaders/packages.js (exports, main) like node does. qjsm installs
# lib/loaders/commonjs.js first, so CommonJS packages can load at all.
#
# result per package, one line in results.tsv:
#
#   OK       both load, same keys of the default export
#   DIFF     both load, the default export's keys differ
#   NSDIFF   same default keys, different named exports (cjs-module-lexer
#            on node versus Object.keys at run time on qjsm)
#   FAIL     node loads it, qjsm throws
#   CRASH    qjsm died from a signal (exit > 128)
#   TIMEOUT  qjsm did not finish in 20 s
#   SKIP     node cannot import it either (types-only, bin-only, ...)
#
# qjsm runs under ulimit -v 2 GB and timeout 20 s, node under timeout only.

set -u

JOBS=4
OUT=${TMPDIR:-/tmp}/node-modules-sweep
FILTER=
ROOT=$(cd "$(dirname "$0")/.." && pwd)

while getopts j:o:f:h opt; do
  case $opt in
    j) JOBS=$OPTARG ;;
    o) OUT=$OPTARG ;;
    f) FILTER=$OPTARG ;;
    *) sed -n '2,12s/^# \{0,1\}//p' "$0"; exit 2 ;;
  esac
done
shift $((OPTIND - 1))

[ $# -gt 0 ] || set -- \
  "$HOME/.nvm/versions/node/v26.11.0/lib/node_modules/npm/node_modules" \
  "$HOME/Sources/magic-string/node_modules"

if [ -z "${QJSM:-}" ]; then
  QJSM=$ROOT/build/$(cc -dumpmachine)/qjsm
  [ -x "$QJSM" ] || QJSM=$(command -v qjsm)
fi
NODE=${NODE:-node}
export QJSM NODE OUT

mkdir -p "$OUT/logs"
: > "$OUT/results.tsv"

# the probe prints the keys of the namespace and of the default export
cat > "$OUT/probe.mjs" <<'EOF'
const name = process.argv[2];
/* node 23+ adds 'module.exports' to a CommonJS namespace */
const list = keys => keys.filter(k => k != 'module.exports').sort().slice(0, 300).join(',');

try {
  if(typeof scriptArgs != 'undefined')
    (await import('loaders/commonjs')).default();

  const ns = await import(name);
  const def = ns.default;

  console.log('NS ' + list(Object.keys(ns)));
  console.log('DEF ' + (def !== null && (typeof def == 'object' || typeof def == 'function') ? list(Object.keys(def)) : typeof def));
} catch(e) {
  console.log('ERR ' + (e?.name ?? typeof e) + ': ' + String(e?.message ?? e).split('\n')[0]);
}

process.exit(0);
EOF

# one package: $1 = swept node_modules dir, $2 = package name
sweep_one() {
  local dir=$1 name=$2 work node_out qjsm_out status
  local log=$OUT/logs/$(printf %s "$name" | tr / _).log

  work=$(mktemp -d "$OUT/work.XXXXXX")
  ln -s "$dir" "$work/node_modules"
  cp "$OUT/probe.mjs" "$work/probe.mjs"

  node_out=$(cd "$work" && timeout 20 "$NODE" probe.mjs "$name" 2>&1 </dev/null)
  qjsm_out=$(cd "$work" && ulimit -v 2000000 && timeout 20 "$QJSM" probe.mjs "$name" 2>&1 </dev/null)
  status=$?
  rm -rf "$work"

  printf '== node\n%s\n== qjsm (exit %d)\n%s\n' "$node_out" "$status" "$qjsm_out" > "$log"

  local nerr nns ndef qerr qns qdef result detail=
  nerr=$(grep -m1 '^ERR ' <<< "$node_out")
  nns=$(grep -m1 '^NS ' <<< "$node_out")
  ndef=$(grep -m1 '^DEF ' <<< "$node_out")
  qerr=$(grep -m1 '^ERR ' <<< "$qjsm_out")
  qns=$(grep -m1 '^NS ' <<< "$qjsm_out")
  qdef=$(grep -m1 '^DEF ' <<< "$qjsm_out")

  if [ -n "$nerr" ] || [ -z "$ndef" ]; then
    result=SKIP detail=${nerr:-node printed nothing}
  elif [ $status -eq 124 ]; then
    result=TIMEOUT
  elif [ $status -gt 128 ]; then
    result=CRASH detail="signal $((status - 128))"
  elif [ -n "$qerr" ]; then
    result=FAIL detail=${qerr#ERR }
  elif [ -z "$qdef" ]; then
    result=FAIL detail="no output: $(grep -m1 -v '^$' <<< "$qjsm_out")"
  elif [ "$ndef" != "$qdef" ]; then
    result=DIFF detail="node ${ndef#DEF } | qjsm ${qdef#DEF }"
  elif [ "$nns" != "$qns" ]; then
    result=NSDIFF detail="node ${nns#NS } | qjsm ${qns#NS }"
  else
    result=OK
  fi

  printf '%s\t%s\t%s\t%s\n' "$result" "$name" "$dir" "${detail:0:300}"
}
export -f sweep_one

# every package dir, @scope/name included, as "dir<TAB>name"
for dir in "$@"; do
  dir=$(cd "$dir" && pwd -P) || continue
  for p in "$dir"/*/ "$dir"/@*/*/; do
    [ -d "$p" ] || continue
    name=${p#"$dir"/}
    name=${name%/}
    [[ $name == .* || $name == */.* ]] && continue
    [[ $name == @* && $name != @*/* ]] && continue
    printf '%s\t%s\n' "$dir" "$name"
  done
done |
  awk -F'\t' -v re="$FILTER" '$2 ~ re && !seen[$2]++ { printf "%s%c%s%c", $1, 0, $2, 0 }' |
  xargs -0 -n2 -P "$JOBS" bash -c 'sweep_one "$1" "$2"' _ \
  >> "$OUT/results.tsv"

sort -o "$OUT/results.tsv" "$OUT/results.tsv"

echo "qjsm: $QJSM"
echo "node: $("$NODE" --version)"
echo
cut -f1 "$OUT/results.tsv" | sort | uniq -c | sort -rn
echo

# failures grouped by message, paths and quoted names folded, so one
# loader bug shows up as one line with a count
echo "FAIL and CRASH causes:"
awk -F'\t' '$1 == "FAIL" || $1 == "CRASH" { print $4 }' "$OUT/results.tsv" |
  sed -E "s#Module [^ ]+ not found#Module <name> not found#; s#in module '[^']*'#in module <path>#" |
  sort | uniq -c | sort -rn | head -30
echo
echo "results: $OUT/results.tsv, logs: $OUT/logs/"

grep -qE '^(FAIL|CRASH|TIMEOUT|DIFF)' "$OUT/results.tsv" && exit 1
exit 0
