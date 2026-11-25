/* resolve hook for node: the bare names 'terminal' and 'terminal/<name>' map to lib/terminal.js and lib/terminal/<name>.js */
const lib = new URL('../lib/', import.meta.url);

export async function resolve(specifier, context, next) {
  if(specifier == 'terminal') return { url: new URL('terminal.js', lib).href, shortCircuit: true };
  if(specifier.startsWith('terminal/')) return { url: new URL(specifier + '.js', lib).href, shortCircuit: true };

  return next(specifier, context);
}
