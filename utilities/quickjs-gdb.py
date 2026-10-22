"""quickjs-gdb.py: gdb commands that dump QuickJS stack frames.
requires: quickjs.c built with debug info (-g), a JSContext* in scope.

  source utilities/quickjs-gdb.py
  jsstack            frames of ctx->rt->current_stack_frame
  jsstack ctx        same, for an explicit context expression
  jsframe sf         one JSStackFrame* (fields and function)

output per frame:  #0 0x7fffffff8340 destroy (native)
                   #1 0x7fffffff8ff0 run (tests/x.js:130:19)

then runs sublime_text on the locations, outermost frame first, current
frame last (DISPLAY defaults to :0). Enabled when sublime_text is in PATH,
switch with: set jsstack-sublime on|off
"""
import os
import re
import shutil
import subprocess

import gdb


def _cstr(expr):
    try:
        v = gdb.parse_and_eval(expr)
        return v.string() if int(v) else None
    except gdb.error:
        return None


def _rel(path):
    """path relative to the current directory when it lies below it."""
    r = os.path.relpath(path)
    return path if r.startswith('..') else r


def _native_pos(name, addr):
    """" at file:line" of a native function: its live gdb frame, else its definition."""
    f = gdb.newest_frame()
    while f:
        if f.name() == name:
            sal = f.find_sal()
            if sal.symtab:
                return ' at %s:%d' % (_rel(sal.symtab.filename), sal.line)
        f = f.older()

    sal = gdb.find_pc_line(addr)
    return ' at %s:%d' % (_rel(sal.symtab.filename), sal.line) if sal.symtab else ''


def _bytecode_classes():
    """class ids of functions that carry bytecode (js_class_has_bytecode is static)."""
    names = ('BYTECODE_FUNCTION', 'GENERATOR_FUNCTION', 'ASYNC_FUNCTION', 'ASYNC_GENERATOR_FUNCTION')
    return {int(gdb.parse_and_eval('(int)JS_CLASS_' + n)) for n in names}


def _leb128(mem, i, signed):
    """decode one (s)leb128 from bytes, returns (value, next index)."""
    r = shift = 0
    while True:
        c = mem[i]
        i += 1
        r |= (c & 0x7f) << shift
        shift += 7
        if not c & 0x80:
            break
    if signed and c & 0x40:
        r -= 1 << shift
    return r, i


def _pc2line(b, pc_value):
    """line and column of pc_value, decoding b->debug.pc2line_buf like find_line_num."""
    dbg = b['debug']
    n = int(dbg['pc2line_len'])
    if not n:
        return 0, 0
    mem = bytes(gdb.selected_inferior().read_memory(int(dbg['pc2line_buf']), n))
    line, i = _leb128(mem, 0, False)
    col, i = _leb128(mem, i, False)
    line += 1
    col += 1
    pc = 0
    while i < n:
        op = mem[i]
        i += 1
        if op == 0:
            d, i = _leb128(mem, i, False)
            pc += d
            v, i = _leb128(mem, i, True)
            new_line = line + v
        else:
            op -= 1  # PC2LINE_OP_FIRST
            pc += op // 5  # PC2LINE_RANGE
            new_line = line + op % 5 - 1  # PC2LINE_BASE
        v, i = _leb128(mem, i, True)
        if pc_value < pc:
            break
        line, col = new_line, col + v
    return line, col


def _describe(ctx, sf):
    """name and source position of sf->cur_func, read through quickjs.c helpers."""
    func = sf['cur_func']
    if int(func['tag']) != -1:  # JS_TAG_OBJECT
        return '<not an object>'

    obj = '((JSObject*)%d)' % int(func['u']['ptr'])
    cls = int(gdb.parse_and_eval('%s->class_id' % obj))

    if cls not in _bytecode_classes():
        cfn = gdb.parse_and_eval('%s->u.cfunc.c_function.generic' % obj)
        name = str(cfn).split('>')[0].split('<')[-1]
        return '%s (native%s)' % (name, _native_pos(name, int(cfn)))

    b = gdb.parse_and_eval('%s->u.func.function_bytecode' % obj)
    name = _cstr('JS_AtomToCString((JSContext*)%d, %d)' % (int(ctx), int(b['func_name']))) or '<anonymous>'
    if not int(b['has_debug']):
        return '%s (no debug info)' % name

    line, col = _pc2line(b, int(sf['cur_pc']) - int(b['byte_code_buf']) - 1)
    file = _cstr('JS_AtomToCString((JSContext*)%d, %d)' % (int(ctx), int(b['debug']['filename'])))
    return '%s (%s:%d:%d)' % (name, file, line, col)


class _SublimeParam(gdb.Parameter):
    """set jsstack-sublime on|off: open the frame locations in sublime_text."""
    set_doc = 'Set whether jsstack opens its locations in sublime_text.'
    show_doc = 'Whether jsstack opens its locations in sublime_text.'

    def __init__(self):
        super().__init__('jsstack-sublime', gdb.COMMAND_DATA, gdb.PARAM_BOOLEAN)
        self.value = shutil.which('sublime_text') is not None

    def get_set_string(self):
        return ''

    def get_show_string(self, svalue):
        return 'jsstack-sublime is %s' % svalue


_sublime = _SublimeParam()


def _location(text):
    """absolute file:line[:col] at the end of a frame description, or None."""
    m = re.search(r'(?:\(native at |\()(.+?)(:\d+(?::\d+)?)\)$', text)
    return os.path.abspath(m.group(1)) + m.group(2) if m else None


class JsStack(gdb.Command):
    """jsstack [CTX]: print the JS stack frames of a JSContext."""

    def __init__(self):
        super().__init__('jsstack', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        ctx = gdb.parse_and_eval(arg.strip() or 'ctx')
        sf = ctx['rt']['current_stack_frame']
        n = 0
        locs = []
        while int(sf):
            text = _describe(ctx, sf.dereference())
            gdb.write('#%d %#x %s\n' % (n, int(sf), text))
            locs.append(_location(text))
            sf = sf['prev_frame']
            n += 1

        locs = [l for l in reversed(locs) if l]
        if locs and _sublime.value:
            env = dict(os.environ)
            env.setdefault('DISPLAY', ':0')
            subprocess.Popen(['sublime_text'] + locs, env=env, start_new_session=True,
                             stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            gdb.write('\nopened %d locations in sublime_text\n' % len(locs))


class JsFrame(gdb.Command):
    """jsframe SF: print one JSStackFrame*."""

    def __init__(self):
        super().__init__('jsframe', gdb.COMMAND_USER)

    def invoke(self, arg, from_tty):
        sf = gdb.parse_and_eval(arg)
        ctx = gdb.parse_and_eval('ctx')
        f = sf.dereference()
        gdb.write('prev_frame %#x  arg_count %d  js_mode %#x\n' % (int(f['prev_frame']), int(f['arg_count']), int(f['js_mode'])))
        gdb.write('%s\n' % _describe(ctx, f))


JsStack()
JsFrame()
