#!/bin/sh
# Dumps the public API surface of every module of a qjs-modules installation
# (builtin, native C, JS) with describe.js, and compares two dumps.
#
# Usage: api-surface.sh [options]                  collect
#        api-surface.sh [--exit-code] diff OLD NEW compare two output directories
#
#   -i, --install DIR   a DESTDIR install root; every lib/quickjs and
#                       lib/*/quickjs below it is a module directory
#   -m, --modules PATH  module directories instead (QUICKJS_MODULE_PATH syntax)
#   -q, --qjsm FILE     the qjsm to run (default: build/<cc -dumpmachine>/qjsm,
#                       else qjsm from PATH); its builtins (qjsm -l) are included
#   -o, --output DIR    where the dump goes (default: api-surface)
#   -f, --format NAME   flat (default), dts or json; diff needs flat
#   -x, --exclude RE    skip modules whose name matches the egrep pattern
#   -t, --timeout SEC   per module (default 30); memory is capped at 4 GB
#   --strict            exit 1 when a module could not be loaded
#
# A module that needs the web globals (TransformStream) is run again behind
# `import 'globals'`.
#
# Which module comes from where: a name in `qjsm -l` is imported by name (what a
# script gets), any other .js or .so under the module directories by its path.
#
# Output (in --output):
#   modules/<name>.txt  one module's dump, one member per line
#   api.txt             every dump, each line prefixed with "<name>: "; run- and
#                       build-specific values (process.pid, versions) are dropped
#   INDEX.txt           name, origin (builtin|js|native), line count
#   failed.txt          modules that did not load, with the first error line
#
# `diff` lists members removed, changed and added between two api.txt files and
# the version bump they call for: removed or changed means major, added means
# minor, nothing means patch. --exit-code exits 1 on any difference.
#
# Examples:
#   make -C build/x86_64-linux-gnu install DESTDIR=$PWD/build/x86_64-linux-gnu/inst
#   utilities/api-surface.sh -i build/x86_64-linux-gnu/inst -o /tmp/api-new
#   utilities/api-surface.sh diff /tmp/api-v1.2 /tmp/api-new

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
root=$(CDPATH= cd -- "$dir/.." && pwd)
describe="$root/lib/describe.js"

die() { echo "api-surface.sh: $*" >&2; exit 1; }

usage() {
  sed -n '2,/^$/{/^$/d;s/^# \{0,1\}//;p;}' "$0"
}

# diff OLD NEW: awk over the two api.txt files. A key is the module plus the
# member name (the line up to its first "(" or ":"); same key with another
# line is a change, e.g. "Blob.prototype.size: get" vs "Blob.prototype.size".
do_diff() {
  old=$1/api.txt
  new=$2/api.txt
  [ -f "$old" ] || die "no $old"
  [ -f "$new" ] || die "no $new"
  awk -v exitcode="$exit_code" '
    function key(l,   m, rest, i) {
      m = substr(l, 1, index(l, ": ") - 1)
      rest = substr(l, length(m) + 3)
      i = match(rest, /[(:]/)
      return m "\t" (i ? substr(rest, 1, i - 1) : rest)
    }
    function mod(l) { return substr(l, 1, index(l, ": ") - 1) }
    BEGIN { nr = nc = na = ng = nf = 0 }
    FNR == 1 { file++ }
    file == 1 { o[key($0)] = $0; om[mod($0)] = 1; next }
    { n[key($0)] = $0; nm[mod($0)] = 1 }
    END {
      for (k in o) {
        if (!(k in n)) { removed[++nr] = o[k] }
        else if (o[k] != n[k]) { changed[++nc] = o[k] "\n    -> " n[k] }
      }
      for (k in n) if (!(k in o)) added[++na] = n[k]
      for (m in om) if (!(m in nm)) gone[++ng] = m
      for (m in nm) if (!(m in om)) fresh[++nf] = m
      sortarr(removed, nr); sortarr(changed, nc); sortarr(added, na)
      sortarr(gone, ng); sortarr(fresh, nf)
      if (ng) { print "modules removed:"; for (i = 1; i <= ng; i++) print "  " gone[i] }
      if (nf) { print "modules added:"; for (i = 1; i <= nf; i++) print "  " fresh[i] }
      if (nr) { print "members removed (" nr "):"; for (i = 1; i <= nr; i++) print "  - " removed[i] }
      if (nc) { print "members changed (" nc "):"; for (i = 1; i <= nc; i++) print "  ~ " changed[i] }
      if (na) { print "members added (" na "):"; for (i = 1; i <= na; i++) print "  + " added[i] }
      bump = (nr + nc + ng) ? "major" : (na + nf) ? "minor" : "patch"
      print "summary: " ng " modules removed, " nf " added; " nr " members removed, " nc " changed, " na " added"
      print "suggested bump: " bump
      exit (exitcode && (nr + nc + na + ng + nf)) ? 1 : 0
    }
    function sortarr(a, n,   i, j, t) {
      for (i = 2; i <= n; i++) { t = a[i]; for (j = i - 1; j >= 1 && a[j] > t; j--) a[j + 1] = a[j]; a[j + 1] = t }
    }
  ' "$old" "$new"
}

install_root=
modpath=
qjsm=
out=api-surface
format=flat
exclude=
tmo=30
strict=0
exit_code=0
cmd=collect

while [ $# -gt 0 ]; do
  case $1 in
    -i|--install) [ $# -ge 2 ] || die "$1 needs a directory"; install_root=$2; shift 2 ;;
    -m|--modules) [ $# -ge 2 ] || die "$1 needs a path"; modpath=$2; shift 2 ;;
    -q|--qjsm) [ $# -ge 2 ] || die "$1 needs a file"; qjsm=$2; shift 2 ;;
    -o|--output) [ $# -ge 2 ] || die "$1 needs a directory"; out=$2; shift 2 ;;
    -f|--format) [ $# -ge 2 ] || die "$1 needs flat, dts or json"; format=$2; shift 2 ;;
    -x|--exclude) [ $# -ge 2 ] || die "$1 needs a pattern"; exclude=$2; shift 2 ;;
    -t|--timeout) [ $# -ge 2 ] || die "$1 needs seconds"; tmo=$2; shift 2 ;;
    --strict) strict=1; shift ;;
    --exit-code) exit_code=1; shift ;;
    -h|--help) usage; exit 0 ;;
    diff) cmd=diff; shift; break ;;
    *) die "unknown argument '$1' (--help)" ;;
  esac
done

if [ $cmd = diff ]; then
  [ $# -eq 2 ] || die "diff needs OLD NEW"
  do_diff "$1" "$2"
  exit $?
fi

[ $# -eq 0 ] || die "unexpected argument '$1'"
case $format in flat|dts|json) ;; *) die "unknown format '$format'" ;; esac

if [ -z "$qjsm" ]; then
  qjsm="$root/build/$(cc -dumpmachine 2>/dev/null)/qjsm"
  [ -x "$qjsm" ] || qjsm=$(command -v qjsm) || die "no qjsm (use --qjsm)"
fi
[ -x "$qjsm" ] || die "cannot run $qjsm"
[ -f "$describe" ] || die "no $describe"

# module directories: from the install root, or the given path (; or :)
dirs=
if [ -n "$install_root" ]; then
  [ -d "$install_root" ] || die "no directory $install_root"
  dirs=$(find "$install_root" -type d -name quickjs -path '*/lib/*' | sort)
  [ -n "$dirs" ] || die "no lib/quickjs below $install_root"
  modpath=$(printf '%s\n' "$dirs" | paste -sd ';' -)
elif [ -n "$modpath" ]; then
  dirs=$(printf '%s\n' "$modpath" | tr ';:' '\n\n')
else
  modpath=${QUICKJS_MODULE_PATH:-}
  dirs=$(printf '%s\n' "$modpath" | tr ';:' '\n\n')
fi

# values that differ per run or per build, dropped from api.txt: the member stays
volatile='/^(process|module): [^=]*\<(pid|ppid|argv0?|execPath|title|version|versions|release|buildInfo|env|cwd)\>[^=]*= /s/ = .*$//'

rm -rf "$out"
mkdir -p "$out/modules" || exit 1
out=$(CDPATH= cd -- "$out" && pwd)
work=$(mktemp -d) || exit 1
trap 'rm -rf "$work"' EXIT INT TERM

# module list: "name<TAB>origin<TAB>specifier"; builtins win over files
( ulimit -v 4000000; timeout 30 "$qjsm" -l 2>/dev/null ) |
  sed -n '/^Builtin modules:/,$p' | sed '1d;s/^ *//;s/\.js$//;/^$/d' |
  awk -F'\t' '{ print $0 "\tbuiltin\t" $0 }' >"$work/list"

printf '%s\n' "$dirs" | while IFS= read -r d; do
  [ -d "$d" ] || continue
  (cd "$d" && find . -type f \( -name '*.js' -o -name '*.so' -o -name '*.dll' -o -name '*.dylib' \) | sort) |
    while IFS= read -r f; do
      f=${f#./}
      name=${f%.*}
      case $f in *.js) origin=js ;; *) origin=native ;; esac
      printf '%s\t%s\t%s\n' "$name" "$origin" "$d/$f"
    done
done >>"$work/list"

# first occurrence of a name wins; optional exclude
awk -F'\t' '!seen[$1]++' "$work/list" | sort -t'	' -k1,1 >"$work/modules"
[ -z "$exclude" ] || { grep -Ev -e "^($exclude)	" "$work/modules" >"$work/m2"; mv "$work/m2" "$work/modules"; }

: >"$out/INDEX.txt"
: >"$out/failed.txt"
: >"$out/api.txt"
total=0
bad=0
ext=txt
[ $format = flat ] || ext=$format

printf "import 'globals';\nimport { run } from '%s';\nrun();\n" "$describe" >"$work/globals.mjs"

run() {
  (ulimit -v 4000000; QUICKJS_MODULE_PATH="$modpath" timeout "$tmo" "$qjsm" "$1" "--$format" "$spec" >"$file" 2>"$work/err")
}

while IFS='	' read -r name origin spec; do
  total=$((total + 1))
  file="$out/modules/$name.$ext"
  mkdir -p "$(dirname "$file")"
  # a module that wants the web globals says "import globals first": run it again behind them
  run "$describe" || { grep -q 'import globals' "$file" "$work/err" 2>/dev/null && run "$work/globals.mjs"; }
  if [ $? -eq 0 ] && [ -s "$file" ]; then
    lines=$(wc -l <"$file")
    printf '%s\t%s\t%s\n' "$name" "$origin" "$lines" >>"$out/INDEX.txt"
    [ $format != flat ] || sed "s|^|$name: |" "$file" | sed -E "$volatile" >>"$out/api.txt"
  else
    bad=$((bad + 1))
    # describe.js reports "cannot load" on stdout or stderr
    cat "$file" "$work/err" 2>/dev/null | grep -m 1 -e 'cannot load' -e . | sed 's/^describe.js: cannot load [^:]*: //' >"$work/why"
    rm -f "$file"
    printf '%s\t%s\n' "$name" "$(cat "$work/why")" >>"$out/failed.txt"
  fi
done <"$work/modules"

echo "$total modules, $((total - bad)) dumped, $bad failed -> $out" >&2
[ $bad -eq 0 ] || echo "failed: see $out/failed.txt" >&2
[ $strict -eq 0 ] || [ $bad -eq 0 ] || exit 1
exit 0
