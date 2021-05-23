import { getPerformanceCounter } from 'misc';

let timeOrigin = getPerformanceCounter();

export const performance = {
  now,
  timeOrigin
};

export function now() {
  return getPerformanceCounter() - timeOrigin;
}

export default performance;
