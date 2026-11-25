import { LineEditor } from "../../lib/terminal/lineeditor.js";

const ed = new LineEditor({ history: ["hello world", "goodbye world", "hello qjs"] });

// Set up search
ed.key({ name: "ctrlR", ctrl: false, meta: false, shift: false });
ed.key({ name: "h", ctrl: false, meta: false, shift: false });
ed.key({ name: "e", ctrl: false, meta: false, shift: false });
ed.key({ name: "l", ctrl: false, meta: false, shift: false });
ed.key({ name: "l", ctrl: false, meta: false, shift: false });
ed.key({ name: "o", ctrl: false, meta: false, shift: false });

print("After typing:");
print("  searchMode: " + ed.searchMode);
print("  searchIndex: " + ed.searchIndex);
print("  cmd: " + ed.cmd);
print("  cursor: " + ed.cursor);

// Press enter
const result = ed.key({ name: "enter", ctrl: false, meta: false, shift: false });
print("After enter:");
print("  result: " + JSON.stringify(result));
print("  searchMode: " + ed.searchMode);
print("  cmd: " + ed.cmd);
print("  cursor: " + ed.cursor);