import { fdopen } from 'std';
import { exec, pipe, close, waitpid, readdir } from 'os';
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
   * Map<string, string>
   * A private index mapping a symbol name (String) to the single object file path (String)
   * that defines/exports it. Throws an error if multiple object files define the same symbol.
   */
  #symbolOwners = new Map();
  #symbolUsers = new Map();
  #symbols;

  constructor(symbols) {
    this.#symbols = new Map(Object.entries(symbols));

    // Populate and validate symbol owners; throw on duplicate symbols across different files
    for(const [objFile, typesObj] of this.#symbols)
      for(const [type, names] of Object.entries(typesObj)) {
        if(type == 'U') {
          for(const name of names) this.#symbolUsers.getOrInsertComputed(name, () => new Set()).add(objFile);
        } else if(/^[A-TV-Z]$/.test(type))
          for(const name of names) {
            if(this.#symbolOwners.has(name)) {
              const existingFile = this.#symbolOwners.get(name);
              if(existingFile !== objFile) throw new Error(`Duplicate symbol '${name}' defined in both '${existingFile}' and '${objFile}'`);
            } else this.#symbolOwners.set(name, objFile);
          }
      }
  }

  get objects() {
    return [...this.#symbols.keys()];
  }

  /**
   * Computes the dependency graph lazily for a given entry point.
   *
   * @param {string} entryPoint - The starting symbol name (e.g., 'main' or 'js_init_module')
   * @returns {Object} An object containing includedObjects, unusedObjects, and unresolvedSymbols arrays
   */
  compute(entryPoint) {
    const objRefs = new Map(); // object file -> Set of referenced symbols ('U')

    for(const [objFile, typesObj] of this.#symbols) {
      const refs = new Set();

      for(const [type, names] of Object.entries(typesObj)) for (const name of names) if(type === 'U') refs.add(name);

      objRefs.set(objFile, refs);
    }

    const includedObjects = new Set();
    const worklist = [];

    // Seed the graph with the entry point symbol's owner
    const entryObj = this.#symbolOwners.get(entryPoint);

    if(entryObj) {
      includedObjects.add(entryObj);
      worklist.push(entryObj);
    }

    // Iteratively resolve dependencies (archive/object linking simulation)
    const processedObjs = new Set();

    while(worklist.length > 0) {
      const obj = worklist.pop();
      if(processedObjs.has(obj)) continue;
      processedObjs.add(obj);

      const refs = objRefs.get(obj);

      if(refs)
        for(const ref of refs) {
          const defObj = this.#symbolOwners.get(ref);

          if(defObj && !includedObjects.has(defObj)) {
            includedObjects.add(defObj);
            worklist.push(defObj);
          }
        }
    }

    const unusedObjects = this.objects.filter(obj => !includedObjects.has(obj));
    const unresolvedSymbols = new Set();

    for(const obj of includedObjects) {
      const refs = objRefs.get(obj);

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

Object.assign(globalThis, { parseNmSymbols, parseObjdumpSymbols, ObjectDependencyGraph });

main(...scriptArgs.slice(1));

function main(...args) {
  const symbols = parseNmSymbols(args);

  console.log(symbols);
  const dependencies = new ObjectDependencyGraph(symbols).compute('js_init_module');

  const { includedObjects, unusedObjects, unresolvedSymbols } = dependencies;

  console.log({ includedObjects, unresolvedSymbols });

  startInteractive();

  os.kill(os.getpid(), os.SIGUSR1);
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
 * and builds a nested object hash: { objectFileName: { symbolType: [symbolNames...] } }
 *
 * @param {string[]} filePaths - Array of paths to .a or .o files
 * @returns {Object} Nested hash of object files, symbol types, and symbol name arrays
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

    // Build the nested object hash structure
    if(!symbolType || symbolType?.(type)) ((result[objFileName] ??= {})[type] ??= []).push(name);
  }

  return result;
}

/**
 * Runs 'objdump -h -t -r' on a batch of files (object files first, then archives),
 * tracking archive/object headers, section headers, symbol tables, and relocation tables to build
 * a plain-object hash containing sections, symbol definitions, and relocation arrays.
 *
 * @param {string[]} paths - Array of file paths or directory paths
 * @returns {Object} Hash keyed by file/archive member -> object with sections, symbols, and relocs
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
            const entry = fileDataMap.getOrInsertComputed(key, () => ({ sections: [], symbols: [], relocs: [] }));
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

        const entry = fileDataMap.getOrInsertComputed(key, () => ({ sections: [], symbols: [], relocs: [] }));

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

  const result = {};

  for(const [key, entry] of fileDataMap.entries()) result[key] = entry;

  return result;
}
