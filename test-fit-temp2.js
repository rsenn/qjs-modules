import { fitTable } from './lib/terminal.js';
const mkBasic = () => [
  ['Model', 'Rate', 'Status', 'Probe', 'Says'],
  ['gemini-3.7-flash', '\x1b[32m 59\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[32m✔ usable\x1b[0m 4.3s no tools', 'I am Qwen, developed by Alibaba Cloud and a long identity sentence'],
  ['glm-5.3', '\x1b[33m 45\x1b[0m', '\x1b[32mREADY\x1b[0m', '\x1b[31m✗ All 2 routed attempts failed upstream\x1b[0m', ''],
];
console.log('mkBasic:', mkBasic());
console.log('mkBasic length:', mkBasic().length);
console.log('mkBasic[0] length:', mkBasic()[0]?.length);
const t = fitTable(200, mkBasic(), [4, 3, 0], [4, 3]);
console.log('Success, width:', Math.max(...t.split('\n').map(l => [...l.replace(/\x1b\[[0-9;]*m/g, '')].length)));
