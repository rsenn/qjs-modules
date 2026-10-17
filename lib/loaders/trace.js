/* loaders/trace.js: a registerHooks() tracer, the JS counterpart of DEBUG=modules.
 * depends on: the qjsm global registerHooks (and console for the default output).
 * rule: it only observes; every call goes on to the next hook unchanged.
 *
 * ```js
 * import { trace } from 'loaders_trace';
 *
 * const t = trace();                       // lines on stderr from now on
 * const u = trace({ log: l => lines.push(l), filter: /^file:/ });
 * t.deregister();                          // stop
 * ```
 *
 *   function  options.log     receives each line, default console.error
 *   RegExp    options.filter  only specifiers / urls that match are traced
 *
 *   returns  the registerHooks() handle ({ deregister() })
 *
 * line shapes:
 *   resolve  <specifier> <- <parentURL>   => <url>
 *   resolve  <specifier> <- <parentURL>   FAIL <message>
 *   load     <url> (<format>)
 *   load     <url>   FAIL <message>
 */

const describe = v => (typeof v == 'string' ? v : (v?.url ?? String(v)));

/** Installs the tracing hooks; later registered hooks run first, so register this one last to see their results. */
export function trace({ log = line => console.error(line), filter } = {}) {
  const wanted = text => !filter || filter.test(text);

  return registerHooks({
    resolve(specifier, context, nextResolve) {
      const from = context?.parentURL ?? '(entry)';

      try {
        const result = nextResolve(specifier, context);

        if(wanted(specifier)) log(`resolve  ${specifier} <- ${from}   => ${describe(result)}`);

        return result;
      } catch(e) {
        if(wanted(specifier)) log(`resolve  ${specifier} <- ${from}   FAIL ${e?.message ?? e}`);

        throw e;
      }
    },
    load(url, context, nextLoad) {
      try {
        const result = nextLoad(url, context);

        if(wanted(url)) log(`load     ${url} (${result?.format ?? context?.format ?? '?'})`);

        return result;
      } catch(e) {
        if(wanted(url)) log(`load     ${url}   FAIL ${e?.message ?? e}`);

        throw e;
      }
    },
  });
}

export default trace;
