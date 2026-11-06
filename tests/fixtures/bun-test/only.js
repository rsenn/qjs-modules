import { describe, expect, test } from 'bun:test';

test('not run', () => expect(1).toBe(2));
test.only('run', () => expect(1).toBe(1));
describe.only('suite', () => test('also run', () => expect(2).toBe(2)));
