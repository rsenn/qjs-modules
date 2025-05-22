import { loadFile, popen } from 'std';
import { extname, join, isFile, isDirectory, isAbsolute, skip } from 'path';

function loadJSON(file) {
  let s;
  if((s = loadFile(file))) return JSON.parse(s);
}

export function loader(arg) {
  let module = arg;

  if(skip(module) == -1) {
    if(isFile('package.json')) {
      let p = join('node_modules', module);

      if(isDirectory(p)) {
        const pkg = loadJSON(join(p, 'package.json'));

        if('exports' in pkg && pkg.exports && '.' in pkg.exports) {
          let f;
          if((f = pkg['exports']['.']?.[0]?.['import'])) module = join(p, f);
          else if((f = pkg['exports']['.']?.['import'])) module = join(p, f);
          else throw new Error('exports');
        } else if('module' in pkg) {
          module = join(p, pkg.module);
        } else if('main' in pkg) {
          module = join(p, pkg.main);
        } else {
          console.log('pkg', pkg);
        }
      }
    }
  }

  if(arg != module) console.log('loader', { arg, module });
  return module;
}

export default function() {
  moduleLoader(loader, module => {
    console.log(`.ts loader '${module}'`);
    if(extname(module) == '.ts') {
      if(isFile(module)) {
        let f;
        if((f = popen(`swc '${module}' 2>/dev/null`, 'r'))) {
          let s = f.readAsString();
          f.close();

          if(s) {
            console.log(`transpiled '${module}'`);
            module = `data:application/javascript,${s}`;
          }
        }
      }
    }
    return module;
  });
  console.log('installed module loader');
}
