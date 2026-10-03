import { exists, extname, isDirectory, isFile, join, skip } from 'path';
import { loadFile, popen } from 'std';

function loadJSON(file) {
  let s;
  if((s = loadFile(file))) return JSON.parse(s);
}

/* file:///a/b.ts -> /a/b.ts */
function urlToPath(url) {
  return url.startsWith('file://') ? decodeURIComponent(url.slice(7)) : url;
}

export default function() {
  /* hooks run last-registered first: the .ts hooks are registered before the
     node_modules ones, so a specifier goes node_modules -> .ts -> engine */
  registerHooks({
    resolve(specifier, context, nextResolve) {
      let file = specifier;
      const path = context.parentURL ? urlToPath(context.parentURL) : '';

      if(extname(file) == '' && extname(path) == '.ts' && exists(file + '.ts')) file += '.ts';
      if(extname(file) == '' && extname(path) == '.ts' && exists('node_modules/' + file + '/src/index.ts')) file = 'node_modules/' + file + '/src/index.ts';
      else if(extname(file) == '' && exists('node_modules/' + file + '/src/index.js')) file = 'node_modules/' + file + '/src/index.js';

      return nextResolve(file, context);
    },
    load(url, context, nextLoad) {
      const module = urlToPath(url);

      if(extname(module) == '.ts' && isFile(module)) {
        let f;

        console.log(`.ts loader '${module}'`);

        if((f = popen(`swc '${module}' 2>/dev/null`, 'r'))) {
          let s = f.readAsString();
          f.close();

          if(s) {
            console.log(`transpiled '${module}'`);
            return { format: 'module', source: s, shortCircuit: true };
          }
        }
      }

      return nextLoad(url, context);
    },
  });

  registerHooks({
    resolve(specifier, context, nextResolve) {
      let module = specifier;

      if(extname(module) == '' && !exists(module) && exists(module + '.js')) module += '.js';

      if(skip(module) == -1 && isFile('package.json')) {
        let p = join('node_modules', module);

        if(isDirectory(p)) {
          const pkg = loadJSON(join(p, 'package.json'));

          if('exports' in pkg && pkg.exports && '.' in pkg.exports) {
            let f;
            if((f = pkg['exports']['.']?.[0]?.['import'])) module = join(p, f);
            else if((f = pkg['exports']['.']?.['import'])) module = join(p, f);
            //else throw new Error('exports');
          }

          if('module' in pkg) {
            module = join(p, pkg.module);
          } else if('main' in pkg) {
            module = join(p, pkg.main);
          } else if(isFile(join(p, 'index.js'))) {
            module = join(p, 'index.js');
          } else {
            console.log('pkg', pkg);
            throw new Error(`Could not find entry point in '${p}'`);
          }
        }
      }

      if(specifier != module) console.log('loader', { arg: specifier, module });

      return nextResolve(module, context);
    },
  });

  console.log('installed module loader');
}
