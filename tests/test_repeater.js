import { Console } from 'console';
import { Repeater } from 'repeater';
import { err as stderr } from 'std';
async function main(...args) {
  globalThis.console = new Console(stderr, {
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: true,
      showHidden: false,
    },
  });

  console.log('Repeater', Repeater);

  let rpt = new Repeater(async (push, stop) => {
    try {
      console.log('Repeater', { push, stop });
      console.log('await push(1234) =', await push(1234));
      console.log('await push("blah") =', await push('blah'));
      console.log('await push("blah") =', await push(Symbol.toStringTag));

      //console.log('await stop( )) =', await stop( ));
      await stop('end');
    } catch(err) {
      console.log('Repeater err=', err);
    }
  });

  let it,
    count = 0;

  const states = ['INITIAL', 'STARTED', 'STOPPED', 'DONE', 'REJECTED'];

  while((it = rpt.next(count++))) {
    //console.log(`it[${count}]`, it);

    try {
      it = await it;
      //console.log(`it[${count}]`, typeof it);
    } catch(err2) {
      console.log('Repeater err2=', err2);
    }

    console.log(`it[${count}]`, console.config({ compact: true }), it);
    console.log(`rpt.state`, states[rpt.state]);

    if(it.done) break;
  }

  let r = new Repeater(async (push, stop) => (await push(1), await push(2), await push('x'), await stop()));
  console.log(`r`, r, r.state);

  for await(let value of r) console.log('value', value);
  console.log(`r`, r, r.state);
}

main();