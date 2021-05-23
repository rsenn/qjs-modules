import {getPerformanceCounter } from 'misc';

let startTime = getPerformanceCounter();

export const performance = {
	now() { return getPerformanceCounter() - startTime; }
};

export default performance;