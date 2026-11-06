import { afterAll, afterEach, beforeAll, beforeEach, describe, expect, test } from 'bun:test';

const log = [];

beforeAll(() => log.push('beforeAll'));
afterAll(() => console.log('LOG ' + log.join(',')));
beforeEach(() => log.push('be'));
afterEach(() => log.push('ae'));

test('passes', () => expect(1 + 1).toBe(2));
test('fails', () => expect(1).toBe(2));
test.skip('skipped', () => {});
test.todo('todo');

describe('group', () => {
  beforeEach(() => log.push('inner'));

  test('async passes', async () => {
    await new Promise(r => setTimeout(r, 5));
    expect({ a: 1 }).toEqual({ a: 1 });
  });
  test.each([[1, 2, 3], [2, 3, 5]])('adds %d and %d', (a, b, c) => expect(a + b).toBe(c));
});

test('times out', () => new Promise(() => {}), 50);
