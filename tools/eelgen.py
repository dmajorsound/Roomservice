# EEL2 AST -> C++ code generator for the roomservice port.
# Semantics follow the REAPER/ysfx x64 EEL2 JIT (see notes in rt.h).
import sys, re, collections
sys.setrecursionlimit(200000)
from eelparse import N, isnum, pure, is_trivial

HOST_SCALARS = ['srate', 'samplesblock', 'play_state', 'play_position', 'beat_position', 'tempo', 'ts_num', 'ts_denom',
                'num_ch', 'ext_noinit', 'ext_nodenorm', 'pdc_delay', 'pdc_bot_ch', 'pdc_top_ch', 'pdc_midi',
                'gfx_r', 'gfx_g', 'gfx_b', 'gfx_a', 'gfx_x', 'gfx_y', 'gfx_w', 'gfx_h', 'gfx_mode', 'gfx_clear',
                'gfx_dest', 'gfx_texth', 'gfx_ext_retina', 'mouse_x', 'mouse_y', 'mouse_cap', 'mouse_wheel', 'mouse_hwheel']

BUILTIN_ARGC = {
    'abs': 1, 'atan2': 2, 'ceil': 1, 'cos': 1, 'exp': 1, 'floor': 1, 'log': 1, 'log10': 1, 'max': 2, 'min': 2,
    'sin': 1, 'sqr': 1, 'sqrt': 1, 'tan': 1, 'pow': 2, 'sign': 1,
    'fft': 2, 'ifft': 2, 'fft_permute': 2, 'fft_ipermute': 2, 'memset': 3, 'memcpy': 3,
    'dlyread': 4, 'dlyreadhq': 4, 'bqrun': 4,   # native: high-quality fractional delay read (VST3 / AU / CLAP only, src/rt.cpp)
    'file_avail': 1, 'file_mem': 3, 'file_var': 2,
    'slider': 1, 'sliderchange': 1, 'time_precise': 0,
    'sprintf': -2, 'strcpy': 2, 'strcat': 2, 'strcmp': 2, 'strlen': 1, 'str_getchar': -2, 'str_setlen': 2,
    'gfx_arc': -5, 'gfx_blit': -3, 'gfx_circle': -3, 'gfx_drawstr': -1, 'gfx_getchar': None, 'gfx_gradrect': -8,
    'gfx_line': -4, 'gfx_measurestr': 3, 'gfx_rect': -4, 'gfx_roundrect': -5, 'gfx_ahhicon': 3, 'gfx_setcursor': -1,
    'gfx_setfont': -1, 'gfx_setimgdim': 3, 'gfx_triangle': -6,
}
WRAP1 = {'sin': 'rt_sin', 'cos': 'rt_cos', 'tan': 'rt_tan', 'exp': 'rt_exp', 'log': 'rt_log', 'log10': 'rt_log10'}

def tov(c, k):
    return '(%s ? 1.0 : 0.0)' % c if k == 'B' else c

def cnum(v):
    if v != v: return 'RT_NAN'
    if v == float('inf'): return 'RT_INF'
    if v == float('-inf'): return '(-RT_INF)'
    r = repr(v)
    if 'e' not in r and '.' not in r: r += '.0'
    if v < 0: r = '(' + r + ')'
    return r

def cident(name):
    return re.sub(r'[^A-Za-z0-9_]', lambda m: '_%02X' % ord(m.group(0)), name)

class Gen:
    def __init__(self, funcs):
        self.funcs = funcs
        self.globals = set(); self.strs = {}; self.lits = {}; self.ntemp = 0
        self.scope = None; self.fname = None
        self.warn = collections.Counter()
        self.fmembers = []
        self.promoted = set()
        self.cached = set()          # globals held in C++ locals by an enclosing loop
        self.fglob = function_globals(funcs)
        self.ncached = 0
        self.temps = set()           # globals turned into per-body C++ locals
        self.used_temps = set()

    # ---------- names ----------
    def var(self, name):
        if self.scope is not None and name in self.scope:
            if name in self.promoted: return 'l_' + cident(name)
            return 'L_%s_%s' % (cident(self.fname), cident(name))
        m = re.fullmatch(r'slider(\d+)', name)
        if m and 1 <= int(m.group(1)) <= 256: return 'sl[%s]' % m.group(1)
        m = re.fullmatch(r'spl(\d+)', name)
        if m and int(m.group(1)) < 64: return 'spl[%s]' % m.group(1)
        if name in HOST_SCALARS: return 'v_' + name
        if name in self.temps:
            self.used_temps.add(name); return 't_' + cident(name)
        self.globals.add(name)
        if name in self.cached: return 'c_' + cident(name)
        return 'g_' + cident(name)

    def strid(self, name):
        if name == '#':
            self.ntemp += 1; key = '#tmp%d' % self.ntemp
            self.strs[key] = 190000 + self.ntemp
            return '%d.0' % self.strs[key]
        if name not in self.strs: self.strs[name] = 90000 + sum(1 for k in self.strs if not k.startswith('#tmp'))
        return '%d.0' % self.strs[name]

    def litid(self, s):
        if s not in self.lits: self.lits[s] = 10000 + len(self.lits)
        return '%d.0' % self.lits[s]

    # ---------- loops: globals in registers ----------
    # EEL variables are not part of the memory array, but in C++ both are doubles: after every store through M() the
    # compiler has to assume that any g_ variable changed and loads it again. Inside a loop the globals that no called
    # function touches are therefore held in C++ locals (c_name) and written back when the loop is done.
    def cache_enter(self, nodes):
        scope = self.scope or set()
        direct, callees, written = set(), set(), set()
        for x in nodes: collect(x, scope, direct, callees, written)
        touched = set()
        for c in callees: touched |= self.fglob.get(c, set())
        mine = sorted(direct - touched - self.cached - self.temps)
        if not mine: return '', '', (lambda: None)
        self.cached |= set(mine); self.ncached += len(mine)
        for nm in mine: self.globals.add(nm)
        pro = 'double ' + ', '.join('c_%s = g_%s' % (cident(nm), cident(nm)) for nm in mine) + '; '
        epi = ''.join('g_%s = c_%s; ' % (cident(nm), cident(nm)) for nm in mine if nm in written)
        def undo(): self.cached -= set(mine)
        return pro, epi, undo

    # ---------- expressions: returns (code, kind) kind in L (double&), V (double), B (bool) ----------
    def v(self, n):
        c, k = self.g(n)
        return tov(c, k)
    def b(self, n):
        c, k = self.g(n)
        return c if k == 'B' else 'B(%s)' % c
    def lv(self, n):
        c, k = self.g(n)
        if k != 'L':
            raise SyntaxError('line %d: not an lvalue: %r' % (n.line, n))
        return c

    def g(self, n):
        if n is None: return '0.0', 'V'
        t = n.t
        if t == 'num': return cnum(n.a[0]), 'V'
        if t == 'var': return self.var(n.a[0]), 'L'
        if t == 'sid': return self.strid(n.a[0]), 'V'
        if t == 'lit': return self.litid(n.a[0]), 'V'
        if t == 'mem': return 'M(%s)' % self.v(n.a[0]), 'L'
        if t == 'seq':
            parts = ['(void)(%s)' % self.v(x) if not (x.t in ('asg',)) else '(void)(%s)' % self.g(x)[0] for x in n.a[:-1]]
            c, k = self.g(n.a[-1])
            return '(%s, %s)' % (', '.join(parts), c), k
        if t == 'if':
            c = self.b(n.a[0])
            a, b_ = n.a[1], n.a[2]
            if a is not None and b_ is not None:
                ca, ka = self.g(a); cb, kb = self.g(b_)
                if ka == 'L' and kb == 'L': return '(%s ? %s : %s)' % (c, ca, cb), 'L'
                if ka == 'B' and kb == 'B': return '(%s ? %s : %s)' % (c, ca, cb), 'B'
                return '(%s ? (double)%s : (double)%s)' % (c, tov(ca, ka), tov(cb, kb)), 'V'
            if a is not None: return '(%s ? (double)%s : 0.0)' % (c, self.v(a)), 'V'
            return '(%s ? 0.0 : (double)%s)' % (c, self.v(b_)), 'V'
        if t == 'neg': return '(-%s)' % self.v(n.a[0]), 'V'
        if t == 'not': return '(!%s)' % self.b(n.a[0]), 'B'
        if t == 'notnot': return self.b(n.a[0]), 'B'
        if t == 'or0': return 'rt_or0(%s)' % self.v(n.a[0]), 'V'
        if t == 'asg': return self.asg(n)
        if t == 'call': return self.call(n)
        if t == 'while2':
            pro, epi, undo = self.cache_enter([n.a[0], n.a[1]])
            cond = self.b(n.a[0]); body = self.v(n.a[1]); undo()
            return ('[&]() -> double { int c_ = 1048576; %swhile (%s) { (void)(%s); if (--c_ <= 0) break; } %sreturn 0.0; }()'
                    % (pro, cond, body, epi)), 'V'
        # binary
        l, r = n.a
        if t in ('&&', '||'):
            return '(%s %s %s)' % (self.b(l), t, self.b(r)), 'B'
        both_impure = not pure(l) and not pure(r)
        if both_impure: self.warn['binary both impure'] += 1
        def two(fmt):
            if both_impure:
                return '[&]() -> double { const double a_ = %s; const double b_ = %s; return %s; }()' % (
                    self.v(l), self.v(r), fmt % ('a_', 'b_'))
            return fmt % (self.v(l), self.v(r))
        if t in ('+', '-', '*', '/'): return two('(%%s %s %%s)' % t), 'V'
        if t == '%': return two('rt_mod(%s, %s)'), 'V'
        if t == '^': return two('rt_pow(%s, %s)'), 'V'
        if t == '&': return two('rt_and(%s, %s)'), 'V'
        if t == '|': return two('rt_or(%s, %s)'), 'V'
        if t == '~': return two('rt_xor(%s, %s)'), 'V'
        if t == '<<': return two('rt_shl(%s, %s)'), 'V'
        if t == '>>': return two('rt_shr(%s, %s)'), 'V'
        if t in ('<', '>', '<=', '>=', '===', '!=='):
            op = {'===': '==', '!==': '!='}.get(t, t)
            if both_impure:
                return '[&]() -> bool { const double a_ = %s; const double b_ = %s; return a_ %s b_; }()' % (
                    self.v(l), self.v(r), op), 'B'
            return '(%s %s %s)' % (self.v(l), op, self.v(r)), 'B'
        if t in ('==', '!='):
            f = 'EQ' if t == '==' else 'NE'
            if both_impure:
                return '[&]() -> bool { const double a_ = %s; const double b_ = %s; return %s(a_, b_); }()' % (
                    self.v(l), self.v(r), f), 'B'
            return '%s(%s, %s)' % (f, self.v(l), self.v(r)), 'B'
        raise SyntaxError('unknown node %s' % t)

    def asg(self, n):
        op, lhs, rhs = n.a
        L = self.lv(lhs)
        simple_lhs = lhs.t == 'var'
        if op == '=':
            R = self.v(rhs)
            if is_trivial(rhs) and (simple_lhs or pure(lhs)): return '(%s = %s)' % (L, R), 'L'
            # a value in a C++ local lives for one call only: the flush of denormal / non-finite values (dnf) is left
            # to the assignment that stores it for good (variable or memory). Same numbers for every finite, normal value.
            if simple_lhs and (self.scope is not None and lhs.a[0] in self.promoted or L.startswith('t_')): return '(%s = %s)' % (L, R), 'L'
            if simple_lhs or pure(rhs) and pure(lhs):
                return 'ASG(%s, %s)' % (L, R), 'L'
            return '[&]() -> double& { double& r_ = %s; r_ = dnf(%s); return r_; }()' % (L, R), 'L'
        R = self.v(rhs)
        fn = {'%=': 'rt_mod', '^=': 'rt_pow', '|=': 'rt_or', '&=': 'rt_and', '~=': 'rt_xor'}.get(op)
        if simple_lhs or (pure(rhs) and pure(lhs)):
            if fn: return 'CASG(%s, %s(%s, %s))' % (L, fn, L, R), 'L'
            return '(%s %s %s)' % (L, op, R), 'L'
        expr = '%s(r_, %s)' % (fn, R) if fn else 'r_ %s %s' % (op[0], R)
        return '[&]() -> double& { double& r_ = %s; r_ = %s; return r_; }()' % (L, expr), 'L'

    def call(self, n):
        name = n.a[0]; args = n.a[1:]
        if name in self.funcs:
            f = self.funcs[name]
            if len(args) != len(f.params):
                raise SyntaxError('line %d: %s() expects %d args' % (n.line, name, len(f.params)))
            cname = 'F_' + cident(name)
            if not args: return cname + '()', 'V'
            if all(pure(a) for a in args):
                return '%s(%s)' % (cname, ', '.join(self.v(a) for a in args)), 'V'
            # EEL: non-trivial params left to right, then trivial ones (vars/consts) are read
            self.warn['eel call impure args'] += 1
            pre = []; names = []
            for i, a in enumerate(args):
                if is_trivial(a): names.append(None)
                else:
                    pre.append('const double a%d_ = %s;' % (i, self.v(a))); names.append('a%d_' % i)
            callargs = [nm if nm else self.v(a) for nm, a in zip(names, args)]
            return '[&]() -> double { %s return %s(%s); }()' % (' '.join(pre), cname, ', '.join(callargs)), 'V'
        if name == 'loop':
            if len(args) != 2: raise SyntaxError('loop args line %d' % n.line)
            cnt = self.v(args[0])
            pro, epi, undo = self.cache_enter([args[1]])
            body = self.v(args[1]); undo()
            return ('[&]() -> double { int n_ = rt_i32(%s); if (n_ > 1048576) n_ = 1048576; %sfor (; n_ > 0; --n_) { (void)(%s); } %sreturn 0.0; }()'
                    % (cnt, pro, body, epi)), 'V'
        if name == 'while':
            if len(args) != 1: raise SyntaxError('while args line %d' % n.line)
            pro, epi, undo = self.cache_enter([args[0]])
            cond = self.b(args[0]); undo()
            return ('[&]() -> double { int c_ = 1048576; %swhile (%s) { if (--c_ <= 0) break; } %sreturn 0.0; }()'
                    % (pro, cond, epi)), 'V'
        if name not in BUILTIN_ARGC:
            raise SyntaxError('line %d: unknown function %s' % (n.line, name))
        want = BUILTIN_ARGC[name]
        if want is not None and (want >= 0 and len(args) != want or want < 0 and len(args) < -want):
            raise SyntaxError('line %d: %s() arg count %d' % (n.line, name, len(args)))
        if name == 'slider': return 'SL(%s)' % self.v(args[0]), 'L'
        if name == 'slider_automate':
            pass
        imp = sum(1 for a in args if not pure(a))
        if imp >= 2: self.warn['builtin multi impure args'] += 1
        vals = [self.v(a) for a in args]
        if name in WRAP1: return '%s(%s)' % (WRAP1[name], vals[0]), 'V'
        simple = {'abs': 'std::fabs', 'floor': 'rt_floor', 'ceil': 'rt_ceil', 'sqrt': 'rt_sqrt', 'sqr': 'rt_sqr',
                  'min': 'rt_min', 'max': 'rt_max', 'atan2': 'rt_atan2', 'pow': 'rt_pow', 'sign': 'rt_sign'}
        if name in simple: return '%s(%s)' % (simple[name], ', '.join(vals)), 'V'
        if name in ('file_var',):
            return 'file_var(%s, %s)' % (vals[0], self.lv(args[1])), 'V'
        if name == 'gfx_measurestr':
            return 'gfx_measurestr(%s, %s, %s)' % (vals[0], self.lv(args[1]), self.lv(args[2])), 'V'
        if name in ('sprintf', 'gfx_drawstr', 'gfx_setfont', 'gfx_arc', 'gfx_blit', 'gfx_circle', 'gfx_getchar',
                    'gfx_gradrect', 'gfx_line', 'gfx_rect', 'gfx_roundrect', 'gfx_ahhicon', 'gfx_setcursor', 'gfx_triangle', 'str_getchar',
                    'slider_automate_idx', 'slider_automate_mask'):
            return '%s({%s})' % (name, ', '.join(vals)), 'V'
        return '%s(%s)' % (name, ', '.join(vals)), 'V'

def slider_automate_fix(n):
    """slider_automate(sliderN[, touch]) / sliderchange(sliderN): pass the slider index, not its value."""
    if n is None or not isinstance(n, N): return
    if n.t == 'call' and n.a[0] in ('slider_automate', 'sliderchange'):
        a0 = n.a[1]
        m = re.fullmatch(r'slider(\d+)', a0.a[0]) if a0.t == 'var' else None
        if m:
            n.a[0] = n.a[0] + '_idx'; n.a[1] = N('num', float(m.group(1)))
        else:
            n.a[0] = n.a[0] + '_mask'
    for x in n.a:
        if isinstance(x, N): slider_automate_fix(x)

BUILTIN_ARGC.update({'slider_automate_idx': -1, 'slider_automate_mask': -1, 'sliderchange_idx': 1, 'sliderchange_mask': 1})


# ---------- locals that never carry a value from one call to the next ----------
# An EEL function's parameters and locals are static storage. A name that is assigned on every path before it is read
# can live in a C++ local instead (a register): nothing can tell the difference, and the optimizer is no longer forced
# to store and reload it around every memory write. Everything doubtful stays static.
LV_ARGS = {'gfx_measurestr': (1, 2), 'file_var': (1,)}

def read_before_assigned(body, interesting, start=()):
    """names (for which interesting(name) holds) that may be read before they are assigned when body runs"""
    unsafe = set()
    def rd(name, A):
        if name not in A and interesting(name): unsafe.add(name)
    def walk(n, A):
        """reads of n checked against A; returns the names assigned for certain when n has been evaluated"""
        if n is None or not isinstance(n, N): return set()
        t = n.t
        if t in ('num', 'sid', 'lit'): return set()
        if t == 'var': rd(n.a[0], A); return set()
        if t == 'seq':
            got = set()
            for x in n.a: got |= walk(x, A | got)
            return got
        if t == 'if':
            c = walk(n.a[0], A)
            ga = walk(n.a[1], A | c) if n.a[1] is not None else set()
            gb = walk(n.a[2], A | c) if n.a[2] is not None else set()
            return c | (ga & gb)
        if t == 'asg':
            op, lhs, rhs = n.a
            if lhs.t == 'var':
                if op != '=': rd(lhs.a[0], A)
                got = walk(rhs, A)
                if op != '=': return got
                return got | {lhs.a[0]}
            g1 = walk(lhs, A); g2 = walk(rhs, A)      # (order not relied on)
            return g1 | g2
        if t == 'while2':
            c = walk(n.a[0], A); walk(n.a[1], A | c)
            return c                                   # the condition runs at least once
        if t in ('&&', '||'):
            g1 = walk(n.a[0], A); walk(n.a[1], A | g1)
            return g1
        if t == 'call':
            name = n.a[0]; args = n.a[1:]
            if name == 'loop':
                g1 = walk(args[0], A); walk(args[1], A | g1)
                return g1                              # the body may run zero times
            if name == 'while':
                walk(args[0], A); return set()
            got = set(); lvs = LV_ARGS.get(name, ())
            for i, x in enumerate(args):
                if i in lvs and isinstance(x, N) and x.t == 'var': got.add(x.a[0])
                else: got |= walk(x, A)                # every argument against A: the order is not relied on
            return got
        got = set()
        for x in n.a:
            if isinstance(x, N): got |= walk(x, A)     # operands against A: the order is not relied on
        return got
    walk(body, set(start))
    return unsafe

def promotable(f):
    scope = set(f.params) | set(f.locals)
    return scope - read_before_assigned(f.body, lambda nm: nm in scope, f.params)

# ---------- globals that are temporaries ----------
# A global that every piece of code assigns before it reads it, and that no called function touches in between, never
# carries a value from one run to the next: each function / section gets its own C++ local for it (t_name). Variables
# the C++ side reads or writes by name (HOST_VISIBLE) always stay real.
HOST_VISIBLE = {'inch', 'vopt', 'updav', 'updgo', 'sysdark', 'fbw', 'mango', 'updprog', 'gs', 'hlpon', 'kovl', 'helpon'}

def temp_globals(funcs, trees, fglob):
    bodies = [(f.body, set(f.params) | set(f.locals)) for f in funcs.values()] + [(t, set()) for t in trees.values() if t is not None]
    bad = set(HOST_VISIBLE); seen = set()
    for body, scope in bodies:
        if body is None: continue
        d, c, w = set(), set(), set()
        collect(body, scope, d, c, w)
        seen |= d
        bad |= read_before_assigned(body, lambda nm: is_global(nm, scope))
        for callee in c: bad |= d & fglob.get(callee, set())     # a called function uses it too: shared, stays real
    return seen - bad


# ---------- which globals does a piece of code use ----------
def is_global(name, scope):
    if name in scope: return False
    if re.fullmatch(r'slider(\d+)', name) and 1 <= int(name[6:]) <= 256: return False
    if re.fullmatch(r'spl(\d+)', name) and int(name[3:]) < 64: return False
    return name not in HOST_SCALARS

def collect(n, scope, direct, callees, written):
    """globals named in n (direct), user functions called (callees), globals that may be assigned (written)"""
    if n is None or not isinstance(n, N): return
    t = n.t
    if t == 'var':
        if is_global(n.a[0], scope): direct.add(n.a[0])
        return
    if t == 'asg':
        w = set(); collect(n.a[1], scope, w, set(), set()); written |= w      # every variable on the left side
    if t == 'call':
        name = n.a[0]
        if name in LV_ARGS:
            for i in LV_ARGS[name]:
                if i + 1 < len(n.a): collect(n.a[i + 1], scope, written, set(), set())
        if name not in ('loop', 'while'): callees.add(name)      # (built-ins have no entry in the table: harmless)
    for x in n.a:
        if isinstance(x, N): collect(x, scope, direct, callees, written)

def function_globals(funcs):
    """per user function: every global it or a function it calls may read or write"""
    own = {}; calls = {}
    for name, f in funcs.items():
        d, c, w = set(), set(), set()
        collect(f.body, set(f.params) | set(f.locals), d, c, w)
        own[name] = d; calls[name] = c
    changed = True
    while changed:
        changed = False
        for name in funcs:
            for c in calls[name]:
                add = own.get(c, set()) - own[name]
                if add: own[name] |= add; changed = True
    return own
