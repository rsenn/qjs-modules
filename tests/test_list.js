import { List } from 'list';
let l = new List([6, 5, 4, 3, 2, 1]);

let letter = 'a';

const next = () => {
  let r = letter;
  letter = String.fromCodePoint(letter.codePointAt(0) + 1);
  return r;
};

const append = () => {
  l.push(next());
  console.log('l', l);
};

const insert = () => {
  l.insert(it, next());
  console.log('l', l);
};

/*if(0) while(!v || !v.done) {
  skip();
  insert();
}*/

append();
append();
append();
let it = l[Symbol.iterator](1);

const skip = () => {
  let { done, value } = it.next();
  console.log('skip', value, done);
  return done;
};

skip();
insert();
while(!skip()) {}

insert();
console.log('it', it);

//insert();
//console.log('l', l);
