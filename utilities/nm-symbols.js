#!/usr/bin/env qjsm

import { fdopen, puts, exit, loadFile, open } from 'std';
import * as os from 'os';
import { exec, pipe, close, waitpid, readdir, isatty } from 'os';
import { startInteractive, getOpt, isMainModule } from 'util';
import { findFunctionDefinitions } from 'c-functions';
import { Console } from 'console';

globalThis.console = new Console({ inspectOptions: { maxArrayLength: Infinity } });

Map.prototype.getOrInsertComputed = function(key, callback) {
  if(!Map.prototype.isPrototypeOf(this)) throw new TypeError('Method Map.prototype.getOrInsertComputed called on incompatible receiver');
  if(typeof callback !== 'function') throw new TypeError('callback is not a function');

  if(!this.has(key)) {
    const value = callback(key);
    this.set(key, value);
    return value;
  }

  return this.get(key);
};

class ObjectDependencyGraph {
  /**
   * Per-file symbol data, unified with FunctionDependencyGraph's shape:
   * { [objFile]: { symbols: [{ symbol, type }] } }
   */
  #fileData;
  #symbolOwners = new Map(); // symbol -> Set of object files defining it
  #fileRefs = new Map(); // objFile -> Set of referenced ('U') symbols

  constructor(fileData) {
    this.#fileData = fileData;

    for(const [objFile, { symbols }] of Object.entries(fileData)) {
      const refs = new Set();

      for(const { symbol, type } of symbols)
        if(type === 'U') refs.add(symbol);
        else if(/^[A-TV-Z]$/.test(type)) this.#symbolOwners.getOrInsertComputed(symbol, () => new Set()).add(objFile);

      this.#fileRefs.set(objFile, refs);
    }
  }

  get objects() {
    return Object.keys(this.#fileData);
  }

  get fileData() { return this.#fileData; }
  get symbolOwners() { return this.#symbolOwners; }
  get fileRefs() { return this.#fileRefs; }

  /**
   * Computes the dependency graph lazily for a given entry point.
   *
   * @param {string} entryPoint - The starting symbol name (e.g., 'main' or 'js_init_module')
   * @returns {Object} An object containing includedObjects, unusedObjects, and unresolvedSymbols arrays
   */
  compute(entryPoint) {
    const includedObjects = new Set();
    const worklist = [];

    // Seed the graph with the entry point symbol's owners
    const entryObjs = this.#symbolOwners.get(entryPoint);

    if(entryObjs) {
      for(const obj of entryObjs) {
        includedObjects.add(obj);
        worklist.push(obj);
      }
    }

    // Iteratively resolve dependencies (archive/object linking simulation)
    const processedObjs = new Set();

    while(worklist.length > 0) {
      const obj = worklist.pop();
      if(processedObjs.has(obj)) continue;
      processedObjs.add(obj);

      const refs = this.#fileRefs.get(obj);

      if(refs)
        for(const ref of refs) {
          const defObjs = this.#symbolOwners.get(ref);

          if(defObjs)
            for(const defObj of defObjs)
              if(!includedObjects.has(defObj)) {
                includedObjects.add(defObj);
                worklist.push(defObj);
              }
        }
    }

    const unusedObjects = this.objects.filter(obj => !includedObjects.has(obj));
    const unresolvedSymbols = new Set();

    for(const obj of includedObjects) {
      const refs = this.#fileRefs.get(obj);

      if(refs) for(const ref of refs) if (!this.#symbolOwners.has(ref)) unresolvedSymbols.add(ref);
    }

    if(!this.#symbolOwners.has(entryPoint)) unresolvedSymbols.add(entryPoint);

    return {
      includedObjects: Array.from(includedObjects),
      unusedObjects,
      unresolvedSymbols: Array.from(unresolvedSymbols),
    };
  }
}

ObjectDependencyGraph.prototype[Symbol.toStringTag] = 'ObjectDependencyGraph';

/**
 * Maps function callers to referenced symbols based on objdump section ranges and relocations.
 */
class FunctionDependencyGraph {
  #fileData;
  #symbolOwners = new Map(); // symbol -> Set of object files defining it
  #fileRefs = new Map(); // fileName -> Set of referenced symbols (from relocs)
  #allSymbols = new Set(); // all unique symbols found in symbol tables or relocs
  #referencedInRelocs = new Set(); // symbols appearing in at least one relocation record

  #callGraph = new Map(); // calleeSymbol -> Set of { fileName, caller }
  #reverseCallGraph = new Map(); // `${fileName}:${callerName}` -> Set of calleeSymbols

  constructor(parsedObjdumpData) {
    this.#fileData = parsedObjdumpData;
    this.#init();
  }

  get callGraph() { return this.#callGraph; }
  get reverseCallGraph() { return this.#reverseCallGraph; }
  get referencedInRelocs() { return this.#referencedInRelocs; }
  get allSymbols() { return this.#allSymbols; }
  get fileRefs() { return this.#fileRefs; }
  get symbolOwners() { return this.#symbolOwners; }
  get fileData() { return this.#fileData; }

  #init() {
    for(const [fileName, data] of Object.entries(this.#fileData)) {
      const { symbols, relocs, calls } = data;
      if(!symbols) continue;

      // Group defined symbols by section for efficient range lookups
      const symbolsBySection = new Map();
      for(const sym of symbols) {
        this.#allSymbols.add(sym.symbol);
        if(sym.type === 'U' || !sym.section) continue;

        // Only a global ('g') binding makes a file a valid cross-file dependency target.
        // A local ('l', i.e. `static`) symbol is private to its own translation unit - many
        // files can each have their own same-named local copy (e.g. `static inline` helpers
        // from quickjs.h like JS_FreeValue), and an unrelated file's copy must never be
        // treated as satisfying a reference resolved elsewhere. symbolsBySection stays
        // unrestricted since it's only used to find the caller within *this* file's own
        // relocations/calls, where local symbols are perfectly valid callers.
        if(sym.type === 'g') this.#symbolOwners.getOrInsertComputed(sym.symbol, () => new Set()).add(fileName);
        symbolsBySection.getOrInsertComputed(sym.section, () => []).push(sym);
      }

      const fileReferencedSymbols = new Set();

      const addCall = (callerName, calleeName) => {
        this.#callGraph.getOrInsertComputed(calleeName, () => new Set()).add({ fileName, caller: callerName });

        const callerKey = `${fileName}:${callerName}`;
        this.#reverseCallGraph.getOrInsertComputed(callerKey, () => new Set()).add(calleeName);
      };

      if(relocs)
        for(const reloc of relocs) {
          // A reference to a local (static) symbol from data - e.g. a function pointer
          // sitting in a JSCFunctionListEntry table built by JS_CFUNC_MAGIC_DEF/
          // JS_CGETSET_MAGIC_DEF - is emitted section-relative ('.text+0x1234') rather
          // than by name, since the assembler can't name a local symbol from another
          // section/object. Resolve it back to whichever local symbol actually occupies
          // that offset in this same file, so it isn't lost as a reference to '.text'.
          let symbol = reloc.symbol;

          if(symbol.startsWith('.') && symbolsBySection.has(symbol)) {
            const resolved = symbolsBySection.get(symbol).find(sym => reloc.addend >= sym.start && reloc.addend < sym.start + sym.size);
            if(resolved) symbol = resolved.symbol;
          }

          this.#allSymbols.add(symbol);
          this.#referencedInRelocs.add(symbol);
          fileReferencedSymbols.add(symbol);

          const sectionSyms = symbolsBySection.get(reloc.section);
          if(!sectionSyms) continue;

          // Check if the relocation offset falls within the symbol's address range
          for(const sym of sectionSyms)
            if(reloc.offset >= sym.start && reloc.offset < sym.start + sym.size) {
              addCall(sym.symbol, symbol);
              break;
            }
        }

      // Direct calls to local/static symbols are resolved by the assembler and never show
      // up as relocations (see parseObjdumpSymbols); the disassembly already gives us the
      // caller and callee by name, so no section/offset lookup is needed here.
      if(calls)
        for(const { caller, callee } of calls) {
          this.#allSymbols.add(callee);
          this.#referencedInRelocs.add(callee);
          fileReferencedSymbols.add(callee);
          addCall(caller, callee);
        }

      this.#fileRefs.set(fileName, fileReferencedSymbols);
    }
  }

  /**
   * Computes the function dependency graph and object inclusions starting from an entry point symbol.
   *
   * @param {string} entryPoint - The starting symbol name
   * @returns {Object} An object containing includedObjects, unusedObjects, and unusedSymbols arrays
   */
  compute(entryPoint) {
    const allObjects = Object.keys(this.#fileData);
    const includedObjects = new Set();
    const worklist = [];

    // Seed the graph with the entry point symbol's owners
    const entryObjs = this.#symbolOwners.get(entryPoint);
    if(entryObjs)
      for(const obj of entryObjs) {
        includedObjects.add(obj);
        worklist.push(obj);
      }

    // Iteratively resolve dependencies via file references
    const processedObjs = new Set();
    while(worklist.length > 0) {
      const obj = worklist.pop();
      if(processedObjs.has(obj)) continue;
      processedObjs.add(obj);

      const refs = this.#fileRefs.get(obj);
      if(refs)
        for(const ref of refs) {
          const defObjs = this.#symbolOwners.get(ref);

          if(defObjs)
            for(const defObj of defObjs)
              if(!includedObjects.has(defObj)) {
                includedObjects.add(defObj);
                worklist.push(defObj);
              }
        }
    }

    const unusedObjects = allObjects.filter(obj => !includedObjects.has(obj));

    // unusedSymbols are symbols never referenced in a reloc
    const unusedSymbols = [];
    for(const sym of this.#allSymbols) if(!this.#referencedInRelocs.has(sym)) unusedSymbols.push(sym);

    return {
      includedObjects: Array.from(includedObjects),
      unusedObjects,
      unusedSymbols,
    };
  }

  *getSymbols(symbolName) {
    if(typeof symbolName == 'string') {
      const name = symbolName;
      symbolName = s => s == name;
    } else if(RegExp.prototype.isPrototypeOf(symbolName)) {
      const re = symbolName;
      symbolName = s => re.test(s);
    }

    if(typeof symbolName != 'function')
      throw new TypeError(`argument 1  must be string|RegExp|Function`);

    for(const file in this.#fileData) {
      const { sections, symbols, relocs } = this.#fileData[file];

      const s = symbols.find(sym => /[^Ul]/.test(sym.type) && symbolName(sym.symbol));

      if(s) yield [file, s];
    }
  }

  /**
   * Returns a list of callers referencing the given symbol.
   * @param {string} symbolName
   * @returns {Array<{fileName: string, caller: string}>}
   */
  calledBy(symbolName) {
    const callers = this.#callGraph.get(symbolName);
    return callers ? Array.from(callers) : [];
  }

  /**
   * Returns a list of symbols called/referenced by a specific function.
   * @param {string} fileName
   * @param {string} functionName
   * @returns {string[]}
   */
  calls(fileName, functionName) {
    const key = `${fileName}:${functionName}`;
    const callees = this.#reverseCallGraph.get(key);
    return callees ? Array.from(callees) : [];
  }
}

FunctionDependencyGraph.prototype[Symbol.toStringTag] = 'FunctionDependencyGraph';

/**
 * Cross-checks an ObjectDependencyGraph (nm -A) against a FunctionDependencyGraph
 * (objdump) built from the same paths. Both store per-file data as { symbols: [{
 * symbol, type }, ...] }, so the nm-derived symbol set for a file must be a subset
 * of the objdump-derived one; and since FunctionDependencyGraph resolves references
 * via relocations (a superset of nm's undefined-symbol references), its includedObjects
 * must be a superset of ObjectDependencyGraph's.
 *
 * Symbols are compared as (defined-or-undefined, name) pairs rather than by raw type
 * letter: nm's type alphabet (T/t/D/d/B/b/...) and objdump's raw symbol-table flags
 * (binding chars l/g/!) aren't the same code, but both parsers agree on 'U' for
 * undefined references, which is the only distinction either graph's compute() uses.
 *
 * @returns {Object} { missingFiles, mismatchedFiles, onlyInObjectGraph, onlyInFunctionGraph }
 */
function compareGraphs(objGraph, funcGraph, objDeps, funcDeps) {
  const missingFiles = [];
  const mismatchedFiles = [];
  const key = s => `${s.type === 'U' ? 'U' : 'D'}:${s.symbol}`;

  for(const [file, { symbols }] of Object.entries(objGraph.fileData)) {
    const funcEntry = funcGraph.fileData[file];

    if(!funcEntry) {
      missingFiles.push(file);
      continue;
    }

    const objdumpSet = new Set(funcEntry.symbols.map(key));
    const onlyInNm = symbols.map(key).filter(s => !objdumpSet.has(s));

    if(onlyInNm.length) mismatchedFiles.push({ file, onlyInNm });
  }

  const funcIncluded = new Set(funcDeps.includedObjects);
  const objIncluded = new Set(objDeps.includedObjects);

  return {
    missingFiles,
    mismatchedFiles,
    onlyInObjectGraph: objDeps.includedObjects.filter(f => !funcIncluded.has(f)),
    onlyInFunctionGraph: funcDeps.includedObjects.filter(f => !objIncluded.has(f)),
  };
}

/**
 * Walks FunctionDependencyGraph's reverseCallGraph (caller -> callees) from entryPoint,
 * following each callee to its defining file(s) to keep descending, and returns every
 * symbol name reached. A callee of a reached symbol is transitively reached too, so this
 * is the actual "used" set - not just "has some caller in an included file".
 *
 * @returns {Set<string>}
 */
function computeReachableSymbols(funcGraph, entryPoint) {
  const reached = new Set();
  const worklist = [entryPoint];

  while(worklist.length > 0) {
    const symbol = worklist.pop();
    if(reached.has(symbol)) continue;
    reached.add(symbol);

    const owners = funcGraph.symbolOwners.get(symbol);
    if(!owners) continue;

    for(const file of owners) {
      const callees = funcGraph.reverseCallGraph.get(`${file}:${symbol}`);
      if(callees) for(const callee of callees) if(!reached.has(callee)) worklist.push(callee);
    }
  }

  return reached;
}

/**
 * For each included object, reports how many of its defined (non-'U') symbols are
 * actually reached by the call graph rooted at entryPoint - i.e. how much of the
 * object is "pulled in" versus dead weight linked alongside the one symbol that's used.
 *
 * @returns {Array<{file, total, used, usedNames, unusedNames}>} sorted by usage ratio ascending
 */
function objectSymbolCoverage(funcGraph, includedObjects, entryPoint) {
  const reachable = computeReachableSymbols(funcGraph, entryPoint);
  const report = [];

  for(const file of includedObjects) {
    const { symbols } = funcGraph.fileData[file];
    const defined = symbols.filter(s => s.type !== 'U');
    const usedNames = [];
    const unusedNames = [];

    for(const { symbol } of defined) (reachable.has(symbol) ? usedNames : unusedNames).push(symbol);

    report.push({ file, total: defined.length, used: usedNames.length, usedNames, unusedNames });
  }

  return report.sort((a, b) => a.used / (a.total || 1) - b.used / (b.total || 1));
}

function renderSummary(title, deps) {
  const lines = [`== ${title} ==`, `included objects: ${deps.includedObjects.length}`, `unused objects:   ${deps.unusedObjects.length}`];

  if(deps.unresolvedSymbols) lines.push(`unresolved symbols: ${deps.unresolvedSymbols.length}`);
  if(deps.unusedSymbols) lines.push(`unused symbols:    ${deps.unusedSymbols.length}`);

  if(deps.unusedObjects.length) lines.push('-- unused objects --', ...deps.unusedObjects.map(o => `  ${o}`));

  return lines.join('\n');
}

function renderComparisonReport(cmp) {
  const lines = ['== ObjectDependencyGraph vs FunctionDependencyGraph consistency =='];

  lines.push(`files present in nm data but missing from objdump data: ${cmp.missingFiles.length}`);
  lines.push(...cmp.missingFiles.map(f => `  ${f}`));

  lines.push(`files where nm reports symbols objdump doesn't: ${cmp.mismatchedFiles.length}`);
  lines.push(...cmp.mismatchedFiles.map(({ file, onlyInNm }) => `  ${file}: ${onlyInNm.join(', ')}`));

  lines.push(`objects included only by the nm-based graph: ${cmp.onlyInObjectGraph.length}`);
  lines.push(...cmp.onlyInObjectGraph.map(f => `  ${f}`));

  lines.push(`objects included only by the objdump-based graph (finer call info): ${cmp.onlyInFunctionGraph.length}`);
  lines.push(...cmp.onlyInFunctionGraph.map(f => `  ${f}`));

  return lines.join('\n');
}

function renderCoverageReport(coverage, { onlyPartial = true } = {}) {
  const lines = ['== Per-object symbol coverage =='];

  for(const { file, total, used, unusedNames } of coverage) {
    if(used === 0) continue;
    if(onlyPartial && used === total) continue;

    lines.push(`${file}: ${used}/${total} symbols used`);
    if(unusedNames.length) lines.push(`  unused: ${unusedNames.join(', ')}`);
  }

  return lines.join('\n');
}

/**
 * Renders a single-page, top-level summary: object/symbol counts for both graphs,
 * consistency between them, aggregate call-graph size, aggregate function coverage, and
 * the worst-covered objects - everything needed to judge the build at a glance without
 * wading through the full per-object or call-graph dumps.
 */
function renderStatsSummary({ entryPoint, objGraph, objDeps, funcGraph, funcDeps, cmp, coverage }) {
  const lines = ['== Summary =='];

  let callEdges = 0;
  for(const callers of funcGraph.callGraph.values()) callEdges += callers.size;

  let localCalls = 0;
  let relocs = 0;

  for(const data of Object.values(funcGraph.fileData)) {
    localCalls += data.calls?.length ?? 0;
    relocs += data.relocs?.length ?? 0;
  }

  const totalDefined = coverage.reduce((sum, c) => sum + c.total, 0);
  const totalUsed = coverage.reduce((sum, c) => sum + c.used, 0);
  const pct = totalDefined ? ((totalUsed / totalDefined) * 100).toFixed(1) : '0.0';

  lines.push(`entry point:                ${entryPoint}`);
  lines.push(`object files scanned:       ${objGraph.objects.length}`);
  lines.push('');
  lines.push(`ObjectDependencyGraph (nm):        ${objDeps.includedObjects.length}/${objGraph.objects.length} objects included, ${objDeps.unresolvedSymbols.length} unresolved symbols`);
  lines.push(`FunctionDependencyGraph (objdump): ${funcDeps.includedObjects.length}/${objGraph.objects.length} objects included, ${funcDeps.unusedSymbols.length} unused symbols`);
  lines.push('');
  lines.push(
    `consistency: ${cmp.mismatchedFiles.length} symbol mismatches, ${cmp.missingFiles.length} missing files, ` +
      `${cmp.onlyInObjectGraph.length} nm-only objects, ${cmp.onlyInFunctionGraph.length} objdump-only objects`,
  );
  lines.push('');
  lines.push(`call graph: ${funcGraph.callGraph.size} distinct callees, ${callEdges} edges (${relocs} relocation-derived, ${localCalls} local/pointer-derived)`);
  lines.push('');
  lines.push(`function coverage of included objects: ${totalUsed}/${totalDefined} symbols reachable (${pct}%)`);

  const worst = coverage.filter(c => c.total > 0).slice(0, 5);
  if(worst.length) {
    lines.push('worst-covered objects:');
    for(const { file, used, total } of worst) lines.push(`  ${used}/${total}  ${file}`);
  }

  return lines.join('\n');
}

/**
 * Renders the function-level call graph reachable from entryPoint as indented text,
 * following FunctionDependencyGraph's reverseCallGraph (caller -> callees). Can be very
 * large for a well-connected entry point - opt in explicitly rather than printing by default.
 */
function renderCallGraphText(funcGraph, entryPoint, { maxDepth = 8 } = {}) {
  const lines = [];
  const visited = new Set();

  function visit(symbol, depth) {
    const owners = funcGraph.symbolOwners.get(symbol);
    const indent = '  '.repeat(depth);

    if(!owners || owners.size === 0) {
      lines.push(`${indent}${symbol} (undefined)`);
      return;
    }

    for(const file of owners) {
      const key = `${file}:${symbol}`;
      lines.push(`${indent}${symbol}  [${file}]`);

      if(visited.has(key)) {
        lines.push(`${indent}  ... (see above)`);
        continue;
      }

      if(depth >= maxDepth) {
        lines.push(`${indent}  ... (max depth)`);
        continue;
      }

      visited.add(key);

      const callees = funcGraph.reverseCallGraph.get(key);
      if(callees) for(const callee of [...callees].sort()) visit(callee, depth + 1);
    }
  }

  visit(entryPoint, 0);

  return lines.join('\n');
}

Object.assign(globalThis, {
  parseNmSymbols,
  parseObjdumpSymbols,
  ObjectDependencyGraph,
  FunctionDependencyGraph,
  compareGraphs,
  objectSymbolCoverage,
  computeReachableSymbols,
  renderSummary,
  renderComparisonReport,
  renderCoverageReport,
  renderStatsSummary,
  renderCallGraphText,
});

const OPTIONS = {
  help: [false, printHelp, 'h'],
  graph: [false, null, 'g'],
  'dead-code': [false, null, 'd'],
  source: [true, (v, prev) => (prev ?? []).concat(v), 's'],
  output: [true, null, 'o'],
  '@': 'paths',
};

function printHelp() {
  puts(
    `Usage: ${scriptArgs[0]} [OPTIONS] <files-or-dirs...>\n\n` +
      'Cross-checks nm/objdump symbol data to find which object files and symbols\n' +
      `are pulled into a build by a given entry point (default: 'js_init_module'),\n` +
      'and reports unused objects/symbols and per-object symbol coverage.\n\n' +
      '<files-or-dirs...>  .a/.o/.obj/.lib files, or directories to search for them\n\n' +
      'Options:\n' +
      '  -g, --graph  also print the full call graph reached from the entry point\n' +
      '  -h, --help   show this help\n\n' +
      `Dead-code mode (${scriptArgs[0]} -d <so-dir-or-files...>):\n\n` +
      'Finds functions defined in the given --source dirs (default: include, src)\n' +
      "that never appear as a defined symbol in any of the given .so files' (or the\n" +
      "build dir's qjsm executable, if present) local symbol table, i.e. functions\n" +
      "-ffunction-sections/--gc-sections stripped as dead at link time. Requires a\n" +
      'build compiled with -fno-inline, or successfully-inlined live functions will\n' +
      'be misreported as dead. Writes a JSON report (array of { file, name,\n' +
      'startLine, endLine }) meant to be reviewed/trimmed by hand before being fed\n' +
      "to a removal script - name-only symbol matching can't distinguish two\n" +
      'same-named static functions in different files, so a match against one\n' +
      "used elsewhere can mask another same-named function that's truly dead.\n" +
      'Scans include/, src/, and the repo-root quickjs-*.[ch] module sources. The\n' +
      'report also lists notCompiledFiles (whole quickjs-*.c files this build never\n' +
      "compiled, e.g. a disabled MODULE_* option - can't tell if they're used\n" +
      'elsewhere, so excluded rather than reported dead) and platformGuarded\n' +
      '(functions inside a #if/#ifdef mentioning another platform, e.g. _WIN32 -\n' +
      'inactive on this build regardless of whether some other platform uses them,\n' +
      'so kept separate from deadFunctions rather than folded in) and\n' +
      'referencedButUnlinked (absent from every .so, but named in a relocation\n' +
      "record in some .o file - e.g. `.finalizer = my_fn` in a struct that's never\n" +
      'actually passed anywhere live, a real bug that would otherwise look exactly\n' +
      "like dead code; this cross-check trusts the compiler's own relocation data\n" +
      "over the linker's final reachability verdict, so it also catches cases this\n" +
      "tool's own detection logic gets wrong in the future - kept separate rather\n" +
      'than folded into deadFunctions).\n\n' +
      '  -d, --dead-code     switch to dead-code report mode\n' +
      '  -s, --source <dir>  source dir to scan (repeatable, default: include, src)\n' +
      '  -o, --output <file> write JSON report to file instead of stdout\n',
  );
  exit(0);
}

if(isMainModule(import.meta.url)) main(...scriptArgs.slice(1));

function main(...args) {
  const ENTRY_POINT = 'js_init_module';

  // The full call graph text dump can be huge for a well-connected entry point, so it's
  // opt-in only via --graph, rather than printed unconditionally.
  const params = getOpt(OPTIONS, args);
  const showGraph = !!params.graph;
  const paths = params['@'];

  if(params['dead-code']) {
    const sourceDirs = params.source ?? ['include', 'src'];
    const report = findDeadFunctions(sourceDirs, paths);
    const json = JSON.stringify(report, null, 2);

    if(params.output) {
      const f = open(params.output, 'w+');
      f.puts(json + '\n');
      f.close();
    } else puts(json + '\n');

    return;
  }

  const nmData = parseNmSymbols(paths);
  const objGraph = new ObjectDependencyGraph(nmData);
  const objDeps = objGraph.compute(ENTRY_POINT);

  const objdumpData = parseObjdumpSymbols(paths);
  const funcGraph = new FunctionDependencyGraph(objdumpData);
  const funcDeps = funcGraph.compute(ENTRY_POINT);

  console.log(renderSummary('ObjectDependencyGraph (nm)', objDeps));
  console.log();
  console.log(renderSummary('FunctionDependencyGraph (objdump)', funcDeps));
  console.log();

  const cmp = compareGraphs(objGraph, funcGraph, objDeps, funcDeps);
  console.log(renderComparisonReport(cmp));
  console.log();

  const coverage = objectSymbolCoverage(funcGraph, funcDeps.includedObjects, ENTRY_POINT);
  console.log(renderCoverageReport(coverage));
  console.log();

  console.log(renderStatsSummary({ entryPoint: ENTRY_POINT, objGraph, objDeps, funcGraph, funcDeps, cmp, coverage }));

  if(showGraph) {
    console.log();
    console.log(`== Call graph from ${ENTRY_POINT} ==`);
    console.log(renderCallGraphText(funcGraph, ENTRY_POINT, { maxDepth: 6 }));
  }

  Object.assign(globalThis, { nmData, objGraph, objDeps, objdumpData, funcGraph, funcDeps, cmp, coverage });

  if(isatty(0)) {
    startInteractive();
    os.kill(os.getpid(), os.SIGUSR1);
  }
}

function* searchPaths(paths) {
  if(typeof paths == 'string') paths = [paths];

  // Process arguments: if an argument is a directory, search it using os.readdir()[cite: 2]
  for(const p of paths) {
    const [entries, err] = readdir(p);

    if(!err && entries) {
      // It is a directory; filter entries matching pattern
      for(const entry of entries) if(/\.(a|o|obj|lib)$/i.test(entry)) yield `${p}/${entry}`;
    } else {
      // Treat as a direct file path
      yield p;
    }
  }
}

function* searchSoFiles(paths) {
  if(typeof paths == 'string') paths = [paths];

  // Besides the .so modules, also pick up the qjsm executable if the build dir has
  // one: it's the only other build target linking src/include utils, and functions
  // it alone calls shouldn't be reported as dead just because no .so uses them.
  for(const p of paths) {
    const [entries, err] = readdir(p);

    if(!err && entries) {
      for(const entry of entries) if(/\.so(\.\d+)*$/i.test(entry) || /^qjsm(\.exe)?$/i.test(entry)) yield `${p}/${entry}`;
    } else {
      yield p;
    }
  }
}

/**
 * Recursively walks a directory, yielding paths of files matching `re`. Directories are
 * distinguished from files the same way searchPaths() does: try readdir() on the entry.
 */
function* walkFiles(dir, re) {
  const [entries, err] = readdir(dir);
  if(err || !entries) return;

  for(const entry of entries) {
    if(entry == '.' || entry == '..') continue;

    const p = `${dir}/${entry}`;
    const [subEntries, subErr] = readdir(p);

    if(!subErr && subEntries) yield* walkFiles(p, re);
    else if(re.test(entry)) yield p;
  }
}

/**
 * Runs a single 'nm -A' across all .so files (one exec, not one per file - os.exec()'s
 * fork+exec closes every inherited fd up to the process's fd-table size first, which
 * on a system with a large 'ulimit -n' makes each call noticeably slow, so batching
 * matters) and collects the names of all defined text symbols (types T/t/W/w) across
 * all of them - i.e. every function name that survived -ffunction-sections/
 * --gc-sections in *some* build output.
 *
 * @param {string[]} soFiles
 * @returns {Set<string>}
 */
function collectDefinedFunctionSymbols(soFiles) {
  const symbols = new Set();
  if(soFiles.length == 0) return symbols;

  for(const line of runCommand('nm', '-A', ...soFiles)) {
    if(!line) continue;

    const { index } = / [A-Za-z?-] /.exec(line) ?? { index: -1 };
    if(index == -1) continue;

    const remainder = line.slice(index + 1).trim();
    const parts = remainder.split(/\s+/);
    if(parts.length < 2) continue;

    const [type, name] = [parts[parts.length - 2], parts[parts.length - 1]];
    if(/^[TtWw]$/.test(type)) symbols.add(name);
  }

  return symbols;
}

/**
 * Cross-checks every function *defined* in `sourceDirs` (include/src, by default)
 * against the union of defined-symbol names across all `soPaths` .so files, and
 * reports the ones that never made it into any of them - i.e. dead-stripped by
 * -ffunction-sections/-fdata-sections + --gc-sections, and therefore unreferenced
 * anywhere in the built modules.
 *
 * Matching is by function name only (C has no mangling), so two identically-named
 * static functions in different files are indistinguishable: if either is used
 * anywhere, both are reported as alive. This report is meant for human review
 * (trim entries you don't want removed) before being handed to a removal script.
 *
 * @param {string[]} sourceDirs
 * @param {string[]} soPaths - .so files, or directories to search for them
 * @returns {Object}
 */
function* rootModuleFiles() {
  // The quickjs-<name>.c/.h module sources live flat in the repo root, alongside
  // include/ and src/ - not nested under either, so sourceDirs' recursive walk
  // never sees them; scan for them separately.
  const [entries, err] = readdir('.');
  if(err || !entries) return;

  for(const entry of entries) if(/^quickjs-.*\.[ch]$/i.test(entry)) yield entry;
}

/**
 * Returns the set of 1-based line numbers that fall inside a #if/#ifdef/#elif branch
 * whose condition mentions a non-Linux platform macro. Doesn't evaluate conditions -
 * just tracks nesting depth and flags any branch whose *own* condition text matches;
 * an #else paired with a platform-gated #if is treated as still guarded (tolerable
 * over-caution: worst case a removable function is merely skipped, never wrongly
 * deleted).
 */
function findPlatformGuardedLines(source) {
  // #if/#ifdef/#elif conditions mentioning these are almost always gating code for a
  // platform other than "this build" (this tool only ever runs against a single Linux
  // build) - a #if defined(_WIN32) block compiles to nothing here regardless of
  // whether that code is actually dead on Windows, so it must never be reported dead.
  const PLATFORM_MACRO_RE = /\b(_WIN32|_WIN64|__CYGWIN__|__MSYS__|__MINGW32__|__APPLE__|__ANDROID__)\b/;

  const lines = source.split('\n');
  const guarded = new Set();
  const stack = [];

  for(let ln = 0; ln < lines.length; ln++) {
    const m = /^\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)$/.exec(lines[ln]);

    if(!m) {
      if(stack.some(Boolean)) guarded.add(ln + 1);
      continue;
    }

    const [, kw, rest] = m;

    if(kw == 'if' || kw == 'ifdef' || kw == 'ifndef') stack.push(PLATFORM_MACRO_RE.test(rest));
    else if(kw == 'elif') {
      if(stack.length) stack[stack.length - 1] = PLATFORM_MACRO_RE.test(rest);
    } else if(kw == 'endif') stack.pop();
  }

  return guarded;
}

/**
 * Extracts the referenced function name from an objdump -r VALUE column, or null if
 * it isn't a plain code symbol reference (e.g. a .rodata/.bss data reference). Local
 * per-function-section symbols (-ffunction-sections gives every function its own
 * `.text.<name>`/`.text.unlikely.<name>`/etc. section) show up as that section name
 * rather than a plain symbol name, so the last '.'-separated segment is taken; a
 * trailing '+0x...'/'-0x...' addend is stripped first either way.
 */
function relocationTargetName(value) {
  value = value.replace(/[+-]0x[0-9a-fA-F]+$/, '');

  if(value.startsWith('.text.')) value = value.split('.').pop();

  return /^[A-Za-z_]\w*$/.test(value) ? value : null;
}

/**
 * Runs a single batched 'objdump -r' across every .o file found under `soPaths` (not
 * just the .so build outputs - every object file compiled for this build, including
 * ones whose containing section --gc-sections later dropped from the final link) and
 * collects every function name referenced by any relocation record, anywhere.
 *
 * This is deliberately broader/cheaper than a full reachability graph: a function
 * referenced only via a data relocation (e.g. a `.finalizer = my_fn` field in a
 * `static JSClassDef`) is exactly as "referenced" here whether or not the struct
 * holding that relocation ended up reachable from js_init_module in the final .so -
 * so this independently corroborates (or, more importantly, *refutes*) a "dead by
 * absence from the .so" verdict from collectDefinedFunctionSymbols(), using the
 * compiler's own relocation data instead of re-deriving reachability by hand.
 *
 * @param {string[]} soPaths - same dirs/files passed to findDeadFunctions()
 * @returns {Set<string>}
 */
function collectRelocationReferencedSymbols(soPaths) {
  const objFiles = soPaths.flatMap(p => [...walkFiles(p, /\.o$/i)]).filter(f => !/\/CMakeFiles\/CMakeTmp\//.test(f));

  const symbols = new Set();
  if(objFiles.length == 0) return symbols;

  // DWARF sections (.debug_info, .debug_aranges, etc.) carry a relocation to every
  // compiled function's own address for debug-metadata purposes, regardless of
  // whether anything actually calls it - counting those would make this cross-check
  // useless (every function would look "referenced"). Only relocations from a real
  // code/data section count as a genuine usage signal.
  // Allow-list, not a block-list: metadata sections that reference every function
  // regardless of usage keep turning up (.debug_*, .eh_frame's unwind info, ...) -
  // safer to only trust the section kinds that are actually code/data referencing
  // something, rather than trying to enumerate every metadata section that isn't.
  const REAL_SECTION_RE = /^\.(text|data|rodata|bss)(\.|$)/;
  let inRealSection = false;

  for(const line of runCommand('objdump', '-r', ...objFiles)) {
    const header = /^RELOCATION RECORDS FOR \[(.+)\]:$/.exec(line);
    if(header) {
      inRealSection = REAL_SECTION_RE.test(header[1]);
      continue;
    }

    if(!inRealSection) continue;

    const m = /^[0-9a-fA-F]+\s+\S+\s+(\S+)/.exec(line);
    if(!m) continue;

    const name = relocationTargetName(m[1]);
    if(name) symbols.add(name);
  }

  return symbols;
}

/**
 * Basenames of .c files the given build dir's compile_commands.json actually compiles.
 * A src/*.c or quickjs-*.c file missing here wasn't part of this build at all (e.g. a
 * module disabled via a CMake MODULE_* option) - there's no data on whether its
 * functions are used elsewhere, so it must be excluded rather than reported dead.
 */
function compiledFileNames(soPaths) {
  for(const p of soPaths) {
    const json = loadFile(`${p}/compile_commands.json`);
    if(json == null) continue;

    try {
      return new Set(JSON.parse(json).map(({ file }) => file.replace(/^.*\//, '')));
    } catch(e) {}
  }

  return null;
}

export function findDeadFunctions(sourceDirs, soPaths) {
  const soFiles = [...searchSoFiles(soPaths)];
  const definedSymbols = collectDefinedFunctionSymbols(soFiles);
  const referencedSymbols = collectRelocationReferencedSymbols(soPaths);
  const compiledNames = compiledFileNames(soPaths);

  const files = [...sourceDirs.flatMap(dir => [...walkFiles(dir, /\.[ch]$/i)]), ...rootModuleFiles()];

  const deadFunctions = [];
  const notCompiled = [];
  const platformGuarded = [];
  const referencedButUnlinked = [];
  let totalFunctions = 0,
    skippedMacroAliases = 0;

  for(const file of files) {
    if(compiledNames && /\.c$/i.test(file) && !compiledNames.has(file.replace(/^.*\//, ''))) {
      notCompiled.push(file);
      continue;
    }

    const source = loadFile(file);
    if(source == null) continue;

    // A function whose name is itself #define'd in the same file (e.g. the
    // `#define JS_INIT_MODULE js_init_module_foo` / `JS_INIT_MODULE(ctx, name) {...}`
    // pattern every module uses for its entry point) links under the macro-expanded
    // name, not the literal source text - can't check symbol presence without running
    // the preprocessor, so these are skipped rather than misreported as dead.
    const macroNames = new Set([...source.matchAll(/^#define\s+(\w+)\b/gm)].map(m => m[1]));
    const guardedLines = findPlatformGuardedLines(source);

    const defs = findFunctionDefinitions(source, file);
    totalFunctions += defs.length;

    for(const { name, startLine, endLine, declStartLine } of defs) {
      if(macroNames.has(name)) {
        skippedMacroAliases++;
        continue;
      }

      if(definedSymbols.has(name)) continue;

      if(guardedLines.has(declStartLine)) platformGuarded.push({ file, name, startLine, endLine });
      else if(referencedSymbols.has(name)) referencedButUnlinked.push({ file, name, startLine, endLine });
      else deadFunctions.push({ file, name, startLine, endLine });
    }
  }

  return {
    soFiles,
    sourceDirs: [...sourceDirs, '<repo-root>/quickjs-*.[ch]'],
    totalFunctions,
    skippedMacroAliases,
    notCompiledFiles: notCompiled,
    platformGuarded,
    referencedButUnlinked,
    deadFunctionCount: deadFunctions.length,
    deadFunctions,
  };
}

function runCommand(...args) {
  // Create a pipe for non-blocking process output communication
  const p = pipe();
  if(!p) throw new Error('Failed to create OS pipe');

  const [fd, stdout] = p;

  // Run command non-blocking (`block: false`), redirecting stdout to the pipe's write end
  const pid = exec(args, {
    stdout,
    block: false,
  });

  // Close the write file descriptor in the parent so EOF is triggered upon child exit
  close(stdout);

  // Open the read file descriptor using std.fdopen[cite: 2]
  const file = fdopen(fd, 'r');

  if(!file) {
    close(fd);
    throw new Error('Failed to open pipe file descriptor with fdopen');
  }

  return {
    [Symbol.iterator]: () => ({
      next() {
        const done = file.eof();

        if(done) {
          waitpid(pid, 0);
          file.close();
          return { done };
        }

        const value = file.getline();
        return { value, done };
      },
    }),
  };
}

/**
 * Runs 'nm -A' on a list of archive or object files (.a or .o) using a non-blocking
 * os.exec process, reads the output line-by-line via std.fdopen and .getline(),
 * and builds a nested object hash: { objectFileName: { symbols: [{ symbol, type }] } }
 *
 * This is the same per-file shape used by parseObjdumpSymbols()/FunctionDependencyGraph
 * (minus sections/relocs), so an ObjectDependencyGraph and a FunctionDependencyGraph built
 * from the same paths can be compared directly.
 *
 * @param {string[]} filePaths - Array of paths to .a or .o files
 * @returns {Object} Nested hash of object files to { symbols } objects
 */
function parseNmSymbols(paths, symbolType) {
  if(typeof symbolType == 'string') symbolType = new RegExp(symbolType, 'y');

  if(RegExp.prototype.isPrototypeOf(symbolType)) {
    const re = symbolType;
    symbolType = s => re.test(s);
  }

  const files = [...searchPaths(paths)];

  if(files.length === 0) return {};

  // Create a pipe for non-blocking process output communication
  const p = pipe();
  if(!p) throw new Error('Failed to create OS pipe');

  const [read_fd, write_fd] = p;

  // Run 'nm -A' non-blocking (`block: false`), redirecting stdout to the pipe's write end
  const lines = runCommand('nm', '-A', ...files);
  const result = {};

  // Read line-by-line using .getline()[cite: 2]
  for(const line of lines) {
    if(!line) continue;

    // The layout of nm -A output prefixes each line with the containing file/archive member:
    // "<[archive:]file(s)>:[address] <type> <name>"
    const { index } = / [A-Za-z?-] /.exec(line) ?? { index: -1 };

    if(index == -1) continue; // throw new SyntaxError(`Line '${line}' doesn't match <[archive:]file(s)>:[address] <type> <name>`);

    const objFileName = line.slice(0, line.lastIndexOf(':', index));

    const remainder = line.slice(index + 1).trim();
    const parts = remainder.split(/\s+/);

    if(parts.length < 2) continue;

    const name = parts[parts.length - 1];
    const type = parts[parts.length - 2];

    // Validate that the symbol type matches standard single-character nm types (e.g., U, T)
    if(!/^[A-Za-z?-]$/.test(type)) continue;

    // Build the nested object hash structure: { filename: { symbols: [{ symbol, type }] } }
    if(!symbolType || symbolType?.(type)) (result[objFileName] ??= { symbols: [] }).symbols.push({ symbol: name, type });
  }

  return result;
}

/**
 * Runs 'objdump -h -t -r' on a batch of files (object files first, then archives),
 * tracking archive/object headers, section headers, symbol tables, and relocation tables to build
 * a plain-object hash containing sections, symbol definitions, and relocation arrays. A second
 * pass (parseLocalCalls) adds a `calls` array of direct, unrelocated call edges to the same
 * per-file entries - see its docstring for why that has to be a separate objdump invocation.
 *
 * @param {string[]} paths - Array of file paths or directory paths
 * @returns {Object} Hash keyed by file/archive member -> object with sections, symbols, relocs, calls
 */
function parseObjdumpSymbols(paths) {
  const resolvedFiles = [...searchPaths(paths)];

  if(resolvedFiles.length === 0) return {};

  // Order arguments: object files first, then archives (.a or .lib)
  const objectFiles = [];
  const archiveFiles = [];

  for(const f of resolvedFiles) {
    if(/\.(a|lib)$/i.test(f)) archiveFiles.push(f);
    else objectFiles.push(f);
  }

  const lines = runCommand('objdump', '-h', '-t', '-r', ...objectFiles, ...archiveFiles);
  const fileDataMap = new Map();
  const newEntry = () => ({ sections: [], symbols: [], relocs: [], calls: [] });

  let archiveName = null;
  let objectName = null;
  let inSections = false;
  let inSymbolTable = false;
  let inRelocTable = null;

  for(const line of lines) {
    if(!line) continue;

    const archiveMatch = /^In archive (.*):$/.exec(line);
    if(archiveMatch) {
      archiveName = archiveMatch[1];
      objectName = null;
      inSections = false;
      inSymbolTable = false;
      inRelocTable = null;
      continue;
    }

    const fileFormatMatch = /^(.*):\s*file format (.*)$/.exec(line);
    if(fileFormatMatch) {
      objectName = fileFormatMatch[1];
      inSections = false;
      inSymbolTable = false;
      inRelocTable = null;
      continue;
    }

    if(line.includes('Sections:')) {
      inSections = true;
      inSymbolTable = false;
      inRelocTable = null;
      continue;
    }

    if(line.includes('SYMBOL TABLE:')) {
      inSections = false;
      inSymbolTable = true;
      inRelocTable = null;
      continue;
    }

    let tmp;
    if((tmp = /RELOCATION RECORDS FOR \[(.*)\]:/.exec(line))) {
      inSections = false;
      inSymbolTable = false;
      inRelocTable = tmp[1];
      continue;
    }

    const getKey = () => (archiveName ? `${archiveName}:${objectName}` : objectName);

    try {
      if(inSections) {
        if(line.trim().startsWith('Idx') || line.trim() === '') continue;
        const parts = line.trim().split(/\s+/);
        if(parts.length >= 7 && /^\d+$/.test(parts[0]) && parts[1].startsWith('.')) {
          const name = parts[1];
          const size = parseInt(parts[2], 16);
          const offset = parseInt(parts[5], 16);

          let align = 1;
          const alignStr = parts[6];
          const alignMatch = alignStr.match(/^2\*\*(\d+)$/);
          if(alignMatch) {
            align = 1 << parseInt(alignMatch[1], 10);
          } else {
            align = parseInt(alignStr, 10) || 1;
          }

          const key = getKey();
          if(key) {
            const entry = fileDataMap.getOrInsertComputed(key, newEntry);
            (entry.sections ??= []).push({ name, size, offset, align });
          }
        }
      } else if(inSymbolTable) {
        if(line == 'no symbols') continue;

        const matches = line.matchAll(/[0-9A-Fa-f]{8,16}/g);
        const matchArr = [...matches];
        if(matchArr.length < 2) continue;

        const [[addrPos, addrEnd], [sizePos, sizeEnd]] = matchArr.map(m => [m.index, m.index + m[0].length]);

        const obj = {
          symbol: line.slice(sizeEnd + 1).trim(),
          section: line.slice(addrEnd + 9, line.indexOf('\t', addrEnd + 9)).trim(),
        };

        if(/^\*?UND\*?$/.test(obj.section)) {
          obj.type = 'U';
          delete obj.section;
        } else {
          if(obj.symbol.startsWith('.hidden')) {
            obj.symbol = obj.symbol.slice(7).trimStart();
            obj.hidden = true;
          }
          obj.type = line[addrEnd + 1];

          if(/\w/.test(line[addrEnd + 1 + 6])) obj.f = line[addrEnd + 1 + 6];
          if(/\w/.test(line[addrEnd + 1 + 5])) obj.d = line[addrEnd + 1 + 5];

          obj.start = BigInt('0x' + (line.slice(addrPos, addrEnd) || '0'));
          obj.size = BigInt('0x' + (line.slice(sizePos, sizeEnd) || '0'));
        }

        if(obj.start == 0n && obj.size == 0n) continue;

        const key = getKey();
        if(!key) continue;

        const entry = fileDataMap.getOrInsertComputed(key, newEntry);

        entry.symbols.push(obj);
      } else if(inRelocTable) {
        if(/^OFFSET\s*TYPE\s*VALUE/.test(line)) continue;

        const parts = line.trim().split(/\s+/);
        if(parts.length < 3) continue;

        const offset = BigInt('0x' + parts[0]);
        const type = parts[1];
        const targetExpr = parts.slice(2).join(' ');

        let symbol = targetExpr;
        let addend = '0x0';

        const addendMatch = targetExpr.match(/^(.+?)([+-])(0x[0-9a-fA-F]+|[0-9]+)$/);
        if(addendMatch) {
          symbol = addendMatch[1].trim();
          addend = BigInt(addendMatch[3]);
        } else {
          symbol = targetExpr.trim();
        }

        const key = getKey();
        if(!key) continue;

        const entry = fileDataMap.get(key);

        (entry.relocs ??= []).push({
          symbol,
          offset,
          type,
          addend,
          section: inRelocTable,
        });
      }
    } catch(e) {
      throw new Error(`Parse error for line: ${line}: ${e.message} ${e.stack}`);
    }
  }

  parseLocalCalls(objectFiles, archiveFiles, fileDataMap);

  const result = {};

  for(const [key, entry] of fileDataMap.entries()) result[key] = entry;

  return result;
}

/**
 * Adds direct, unrelocated call/reference edges to fileDataMap entries by running
 * 'objdump -d' as its own invocation, separate from the '-h -t -r' pass above.
 *
 * References to symbols with external linkage (uppercase nm type) always go through a
 * relocation, even when the target is defined in the same object file, so those are already
 * captured by the RELOCATION RECORDS parsing. References to local/static symbols (lowercase
 * nm type, i.e. an 'l' binding here) are resolved by the assembler at compile time and never
 * appear in the relocation table - the only way to see them is in the disassembly:
 *   - a 'call' instruction whose target address lands exactly on a symbol's start (no
 *     '+offset') is a direct local call.
 *   - any other instruction (lea, mov, ...) with a RIP-relative operand landing exactly on a
 *     local *function* symbol's start is that function's address being taken to be passed
 *     around as a pointer (e.g. the js_<name>_init handed to JS_NewCModule rather than
 *     called directly) - not a call, but still a real dependency edge.
 *
 * This can't be folded into the '-h -t -r' pass by adding '-d' to that same objdump
 * invocation: combining -d with -r makes objdump inline relocation annotations directly into
 * the disassembly instead of printing separate RELOCATION RECORDS FOR tables, which would
 * silently empty out every file's `relocs` array (and with it, all external call edges).
 *
 * @param {string[]} objectFiles
 * @param {string[]} archiveFiles
 * @param {Map<string, Object>} fileDataMap - populated by the '-h -t -r' pass; mutated in place
 */
function parseLocalCalls(objectFiles, archiveFiles, fileDataMap) {
  if(objectFiles.length === 0 && archiveFiles.length === 0) return;

  const lines = runCommand('objdump', '-d', ...objectFiles, ...archiveFiles);

  const FUNC_LABEL_RE = /^[0-9A-Fa-f]+ <(.+)>:$/;
  const CALL_RE = /\bcallq?\s+[0-9A-Fa-f]+\s*<([^>+]+)>\s*$/;
  // Any RIP-relative operand (lea, mov, ...) that objdump could resolve gets an inline
  // '# <addr> <name>' comment - this is how a function's address gets taken to be passed
  // around as a pointer (e.g. the js_<name>_init passed to JS_NewCModule), not called directly.
  const RIP_REF_RE = /#\s*[0-9A-Fa-f]+\s*<([^>+]+)>\s*$/;

  let archiveName = null;
  let objectName = null;
  let currentFunction = null;

  const getKey = () => (archiveName ? `${archiveName}:${objectName}` : objectName);

  for(const line of lines) {
    if(!line) continue;

    const archiveMatch = /^In archive (.*):$/.exec(line);
    if(archiveMatch) {
      archiveName = archiveMatch[1];
      objectName = null;
      currentFunction = null;
      continue;
    }

    const fileFormatMatch = /^(.*):\s*file format (.*)$/.exec(line);
    if(fileFormatMatch) {
      objectName = fileFormatMatch[1];
      currentFunction = null;
      continue;
    }

    const funcLabel = FUNC_LABEL_RE.exec(line);
    if(funcLabel) {
      currentFunction = funcLabel[1];
      continue;
    }

    if(!currentFunction) continue;

    const callMatch = CALL_RE.exec(line);
    const refMatch = !callMatch && RIP_REF_RE.exec(line);
    if(!callMatch && !refMatch) continue;

    const callee = (callMatch ?? refMatch)[1];
    const key = getKey();
    const entry = key && fileDataMap.get(key);
    if(!entry) continue;

    // Only trust an exact-address target when it actually names a local symbol defined in
    // this file - an unrelocated external call/reference's zeroed-out displacement can, by
    // coincidence, disassemble to an exact match too. For a RIP_REF_RE match (pointer take,
    // not a call), also require the target to actually be a function, not arbitrary local data.
    const sym = entry.symbols.find(s => s.symbol === callee && s.type === 'l');
    if(!sym) continue;
    if(refMatch && sym.f !== 'F') continue;

    (entry.calls ??= []).push({ caller: currentFunction, callee });
  }
}
