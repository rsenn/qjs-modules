/* runs lib/repl.js in piped mode on stdin, for test-repl.js */
import { REPL } from '../../../lib/repl.js';

await new REPL('t', false).run();
