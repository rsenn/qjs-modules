import { Console } from 'console';
import { Repeater } from 'repeater';
import * as std from 'std';


async function main(...args) {
  globalThis.console = new Console(std.err, {
    inspectOptions: {
      depth: 8,
      maxStringLength: Infinity,
      maxArrayLength: 256,
      compact: 3,
      showHidden: false
    }
  });
  console.log('Repeater', Repeater);

  let rpt = new Repeater(async (push, stop) => {
    try {
      console.log('Repeater', { push, stop });
      console.log('await push(1234) =', await push(1234));
      console.log('await push("blah") =', await push('blah'));
      //  console.log('await stop( )) =', await stop( ));
      stop();
    } catch(err) {
      console.log('Repeater err=', err);
    }
  });
  let it,
    count = 0;
  const states = ['INITIAL', 'STARTED', 'STOPPED', 'DONE', 'REJECTED'];

  while((it = rpt.next(count++))) {
    console.log(`it[${count}]`, it);
    try {
      it = await it;
      console.log(`it[${count}]`, typeof it);
    } catch(err2) {
      console.log('Repeater err2=', err2);
    }
    console.log(`it[${count}]`, console.config({ compact: 2 }), it);
    console.log(`rpt.state`, states[rpt.state]);
    if(it.done) break;
  }
  console.log(`rpt.state`, states[rpt.state]);
  /*
  for await(let value of rpt) {
    console.log('value', value);
  }*/
}

main();
