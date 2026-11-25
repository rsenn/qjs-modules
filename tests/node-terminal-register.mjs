/* node --import tests/node-terminal-register.mjs: makes the bare names of lib/terminal*.js resolve */
import { register } from 'node:module';

register('./node-terminal-hooks.mjs', import.meta.url);
