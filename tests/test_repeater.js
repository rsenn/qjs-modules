import { Console } from 'console';
import { Repeater } from 'repeater';
import { err as stderr } from 'std';

async function collect(iterable) {
  const out = [];
  for await(const value of iterable) out.push(value);
  return out;
}

async function combinators() {
  const a = new Repeater(async (push, stop) => {
    await push(1);
    await push(2);
    await stop();
  });
  const b = new Repeater(async (push, stop) => {
    await push('a');
    await push('b');
    await stop();
  });
  console.log('Repeater.zip', await collect(Repeater.zip([a, b])));

  const c = new Repeater(async (push, stop) => {
    await push('x');
    await push('y');
    await stop();
  });
  console.log('Repeater.merge', await collect(Repeater.merge([c, 42])));

  const d = new Repeater(async (push, stop) => {
    await push('fast1');
    await push('fast2');
    await stop();
  });
  const e = new Repeater(async (push, stop) => {
    await push('slow1');
    await stop();
  });
  console.log('Repeater.race', await collect(Repeater.race([d, e])));

  const f = new Repeater(async (push, stop) => {
    await push('f1');
    await push('f2');
    await stop();
  });
  const g = new Repeater(async (push, stop) => {
    await push('g1');
    await stop();
  });
  console.log('Repeater.latest', await collect(Repeater.latest([f, g])));

  console.log('Repeater.zip([])', await collect(Repeater.zip([])));
  console.log('Repeater.merge([])', await collect(Repeater.merge([])));
  console.log('Repeater.race([])', await collect(Repeater.race([])));
  console.log('Repeater.latest([])', await collect(Repeater.latest([])));
}

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

  await combinators();
}

main();