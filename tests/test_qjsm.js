import * as os from 'os';
import * as std from 'std';
import { inspect, format, formatWithOptions } from 'util';
import util from 'util';

function main(...args) {
  //console.log('console.options:', console.options);

  console.log('format:', format);
  console.log('regexp:', /TEST/);
  console.log('util:', inspect(util));
  console.log('util.hasBuiltIn:', util.hasBuiltIn);
  console.log(`util.hasBuiltIn({}, 'toString'):`, util.hasBuiltIn({}, 'toString'));
  console.log(`util.hasBuiltIn(function(){}, 'toString'):`,
    util.hasBuiltIn(function () {}, 'toString')
  );
  console.log('os:', inspect(os, { /* compact: false,*/ breakLength: 80 }));
  std.puts(format('%s %o\n', 'TEST', { x: 1, y: 2, z: 3 }));

  console.log('modules:', moduleList);
  let moduleExports = moduleList.map(module => getModuleObject(module) /*({
    name: getModuleName(module),
    exports: getModuleExports(module),
    namespace: getModuleNamespace(module),
    func: getModuleFunction(module),
    exception: getModuleException(module),
    meta: getModuleMetaObject(module)
  })*/);
  console.log('moduleExports:', moduleExports);

  std.gc();
}

try {
  main(...scriptArgs.slice(1));
} catch(error) {
  console.log(`FAIL: ${error.message}\n${error.stack}`);
  std.exit(1);
} finally {
  console.log('SUCCESS');
}
