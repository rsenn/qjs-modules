#!/bin/sh
# Lists what a module, an object or a class has, with describe.js, in
# the JavaScript runtime of your choice: QuickJS (qjsm or qjs), Node.js, Bun or
# Deno. The same dump of the same API in two runtimes can then be compared.
#
# Usage: describe.sh [-r|--runtime <name>] [--json] [--js] [--dts] [--flat] [--sort] [--no-arguments] [--class] <module> [export...]
#        describe.sh [-r|--runtime <name>] [--json] [--js] [--dts] [--flat] [--sort] [--no-arguments] [--class] --global [name...]
#
#   <module>       a file (a generated binding, ffi.so, lib/bindings/*.js) or a specifier
#                  the runtime imports: node:fs, bun:ffi, ffi
#   export         a name or dotted path (read.u8); every export by default
#   --global       describe properties of globalThis (Bun, process.versions)
#   --class        describe a function as a class unless its prototype is plain
#                  (native constructors)
#   --json         print the raw describeClass()/describeObject() results
#   --js           print a skeleton of the module as JavaScript, bodies empty
#   --dts          print TypeScript declarations for the module
#   --flat         print one sorted line per export and member, for diffing
#   --sort         order exports and members by name (--flat always does)
#   --no-arguments show every function as name() (--no-args, --no-arg)
#   -r, --runtime  qjsm (default), qjs, node, bun, deno or browser; or
#                  $DESCRIBE_RUNTIME. "browser" is a headless Chrome or Firefox
#                  (see describe-browser.js: $DESCRIBE_BROWSER names it), driven
#                  by node, bun or deno; use --global (navigator, CSS) or a URL
#   -l, --list     list the modules of the runtime (module.builtinModules);
#                  with --diff, how the two runtimes' lists differ
#   -d, --diff     <a>,<b>: dump in two runtimes and show how the dumps differ
#                  (every line, "-" only in a, "+" only in b, red and green on
#                  a terminal; --flat unless --js, --dts or --json is given;
#                  exit status 0 whether they differ or not)
#
# With nothing but a module name, no runtime is run for a dump: every runtime
# that is installed is asked whether it can load the module, and the command
# that dumps it is printed for each one that can, with this script named the way
# it was started:
#
#   $ ./utilities/describe.sh node:fs
#   ./utilities/describe.sh -r node node:fs
#   ./utilities/describe.sh -r bun node:fs
#   ./utilities/describe.sh -r deno node:fs
#
# Importing a binding loads the libraries it binds, so QUICKJS_MODULE_PATH has to
# find the 'ffi' module (a build directory, say) as well as the usual
# directories; it is passed on as it is.
#
# Examples:
#   describe.sh build/ffi.so                  # which runtimes have it
#   describe.sh -r qjsm build/ffi.so          # what it exports
#   describe.sh -r node node:fs readFile promises.readFile
#   describe.sh -r bun bun:ffi
#   describe.sh -r bun --global Bun.FFI
#   describe.sh -r node --class --global Buffer
#   describe.sh -l -r bun                            # modules bun has built in
#   describe.sh -l -d node,bun                       # which node has and bun lacks
#   describe.sh -r browser --global navigator        # the browser's own API
#   describe.sh -d bun,browser --flat --global WebSocket
#   describe.sh -d node,bun node:fs                  # node:fs in node against bun
#   describe.sh -d qjsm,bun --dts bun:ffi            # as declarations

dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1

# The comment header above, minus its leading "# ".
usage() {
  sed -n '2,/^$/{/^$/d;s/^# \{0,1\}//;p;}' "$0"
}

[ $# -gt 0 ] || { usage >&2; exit 1; }

runtimes='qjsm qjs node bun deno'
diff_pair=
list=0
script="$dir/../lib/describe.js"
runtime=${DESCRIBE_RUNTIME:-}
explicit=0
[ -z "$runtime" ] || explicit=1

while [ $# -gt 0 ]; do
  case $1 in
    -r|--runtime)
      [ $# -ge 2 ] || { echo "describe.sh: $1 needs a runtime name" >&2; exit 1; }
      runtime=$2
      explicit=1
      shift 2
      ;;
    --runtime=*)
      runtime=${1#--runtime=}
      explicit=1
      shift
      ;;
    -l|--list)
      list=1
      shift
      ;;
    -d|--diff)
      [ $# -ge 2 ] || { echo "describe.sh: $1 needs <runtime>,<runtime>" >&2; exit 1; }
      diff_pair=$2
      shift 2
      ;;
    --diff=*)
      diff_pair=${1#--diff=}
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *) break ;;
  esac
done

# $1 as one shell word, quoted if it needs it.
quote() {
  case $1 in
    '' | *[!A-Za-z0-9_./:@%+=,-]*) printf "'%s'" "$(printf %s "$1" | sed "s/'/'\\\\''/g")" ;;
    *) printf %s "$1" ;;
  esac
}

# The runtime that drives the browser: the first of node, bun, deno that is installed.
browser_host() {
  for h in node bun deno; do
    command -v "$h" >/dev/null 2>&1 && { echo "$h"; return 0; }
  done

  return 1
}

# Checks that $1 is a runtime this script knows and has installed.
check_runtime() {
  case $1 in
    qjsm|qjs|node|bun|deno) ;;
    browser)
      browser_host >/dev/null || { echo "describe.sh: browser needs node, bun or deno to drive it" >&2; exit 1; }
      return 0
      ;;
    *)
      echo "describe.sh: unknown runtime '$1' (qjsm, qjs, node, bun, deno, browser)" >&2
      exit 1
      ;;
  esac

  command -v "$1" >/dev/null 2>&1 || {
    echo "describe.sh: $1 not found" >&2
    exit 1
  }
}

# The built-in modules of runtime $1, one per line, sorted. One portable
# line: node and bun take it with -e, deno with eval, qjsm has no top-level await there.
list_in() {
  [ "$1" != qjs ] || { echo "describe.sh: qjs has no module list, use qjsm" >&2; return 1; }
  [ "$1" != browser ] || { echo "describe.sh: a browser has no module list" >&2; return 1; }
  code="import('module').then(m => console.log(m.builtinModules.join('\\n'))).catch(e => console.error('no module list: ' + e.message))"

  case $1 in
    deno) deno eval "$code" ;;
    *) "$1" -e "$code" ;;
  esac | sort
  return 0
}

# describe.js in the browser, started and driven by describe-browser.js.
browser_run() {
  case $(browser_host) in
    bun) bun run "$dir/describe-browser.js" "$@" ;;
    deno) deno run -A "$dir/describe-browser.js" "$@" ;;
    *) node "$dir/describe-browser.js" "$@" ;;
  esac
}

# describe.js in runtime $1 with the rest of the arguments.
run_in() {
  rt=$1
  shift

  case $rt in
    browser) browser_run "$@" ;;
    bun) bun run "$script" "$@" ;;
    deno) deno run -A "$script" "$@" ;;
    *) "$rt" "$script" "$@" ;;
  esac
}

# -d a,b: the same dump in two runtimes, diffed.
if [ -n "$diff_pair" ]; then
  a=${diff_pair%%,*}
  b=${diff_pair#*,}

  [ -n "$a" ] && [ -n "$b" ] && [ "$a" != "$diff_pair" ] || { echo "describe.sh: --diff needs <runtime>,<runtime>" >&2; exit 1; }
  check_runtime "$a"
  check_runtime "$b"

  format=--flat

  for arg; do
    case $arg in --js|--dts|--json) format= ;; esac
  done

  tmp=$(mktemp -d) || exit 1
  trap 'rm -rf "$tmp"' EXIT
  if [ "$list" = 1 ]; then
    list_in "$a" >"$tmp/$a" || { echo "describe.sh: $a failed" >&2; exit 1; }
    list_in "$b" >"$tmp/$b" || { echo "describe.sh: $b failed" >&2; exit 1; }
  else
    run_in "$a" $format "$@" >"$tmp/$a" || { echo "describe.sh: $a failed" >&2; exit 1; }
    run_in "$b" $format "$@" >"$tmp/$b" || { echo "describe.sh: $b failed" >&2; exit 1; }
  fi
  # the whole of both dumps, not hunks: " same", "-only in a", "+only in b";
  # on a terminal the removed lines are light red, the added ones light green.
  esc=$(printf '\033')
  red= green= reset=

  if [ -t 1 ]; then
    red="$esc[1;31m"
    green="$esc[1;32m"
    reset="$esc[0m"
  fi

  diff --unchanged-line-format=' %l
' --old-line-format="$red-%l$reset
" --new-line-format="$green+%l$reset
" "$tmp/$a" "$tmp/$b"
  rc=$?

  # diff's 1 means "they differ", which is a result, not a failure
  [ "$rc" -le 1 ] || exit "$rc"
  exit 0
fi

# -l: the modules of one runtime.
if [ "$list" = 1 ]; then
  runtime=${runtime:-qjsm}
  check_runtime "$runtime"
  list_in "$runtime"
  exit $?
fi

# A module name alone: find the runtimes that have it.
if [ "$explicit" = 0 ] && [ $# -eq 1 ]; then
  case $1 in
    -*) ;;
    *)
      found=0

      for r in $runtimes; do
        if ! command -v "$r" >/dev/null 2>&1; then
          echo "# $r: not installed" >&2
          continue
        fi

        probe_ok=0

        case $r in
          bun) bun run "$script" --probe "$1" >/dev/null 2>&1 && probe_ok=1 ;;
          deno) deno run -A "$script" --probe "$1" >/dev/null 2>&1 && probe_ok=1 ;;
          *) "$r" "$script" --probe "$1" >/dev/null 2>&1 && probe_ok=1 ;;
        esac

        if [ "$probe_ok" = 1 ]; then
          echo "$(quote "$0") -r $r $(quote "$1")"
          found=1
        else
          echo "# $r: cannot load $1" >&2
        fi
      done

      [ "$found" = 1 ] || { echo "describe.sh: no installed runtime can load $1" >&2; exit 1; }
      exit 0
      ;;
  esac
fi

runtime=${runtime:-qjsm}
check_runtime "$runtime"

case $runtime in
  browser) browser_run "$@"; exit $? ;;
  bun) exec bun run "$script" "$@" ;;
  deno) exec deno run -A "$script" "$@" ;;
  *) exec "$runtime" "$script" "$@" ;;
esac
