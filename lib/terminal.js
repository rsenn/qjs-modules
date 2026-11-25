/* terminal.js: the whole terminal API in one import, everything of lib/terminal/*.js re-exported.
 *
 * depends on: terminal/core, keys, capabilities, style, screen, widgets, scrollview, lineeditor.
 * rule: nothing imports this file, the modules of lib/ import terminal/core (or the one
 * they need), so there is no cycle and a module that only needs ttyWrite does not load the rest.
 *
 *   import { Screen, readKey, boxLines, windowSize } from 'terminal';
 */

export * from 'terminal/core';
export * from 'terminal/keys';
export * from 'terminal/capabilities';
export * from 'terminal/style';
export * from 'terminal/screen';
export * from 'terminal/widgets';
export * from 'terminal/scrollview';
export * from 'terminal/lineeditor';
