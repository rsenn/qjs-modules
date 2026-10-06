(module
  (import "./env.js" "f" (func $f (param i32) (result i32)))
  (global (export "g") i32 (i32.const 7))
  (memory (export "memory") 1)
  (func (export "twice") (param i32) (result i32) local.get 0 call $f))
