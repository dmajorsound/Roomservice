#!/usr/bin/env python3
"""Layout test: reads the logs the plug-in writes with AHH_TEST_TEXTLOG=<file> (every text and filled area of one frame)
and reports texts that run into each other, leave the window or were cut short.   check_layout.py log [log ...]"""
import sys

def load(path):
    W = H = 0; items = []
    for n, ln in enumerate(open(path, encoding='utf-8', errors='replace').read().split('\n')):
        f = ln.split('\t')
        if f[0] == 'S': W, H = int(f[1]), int(f[2])
        elif f[0] == 'R' and len(f) >= 6: items.append(('R', n, int(f[1]), int(f[2]), int(f[3]), int(f[4]), int(f[5])))
        elif f[0] == 'T' and len(f) >= 13:
            x, y, tw, th, a = int(f[2]), int(f[3]), float(f[4]), float(f[5]), float(f[6])
            # ink box: cap top to a little under the base line (Mulish: base line at 0.80 of the height, capitals 0.56 high)
            items.append(('T', n, int(f[1]), x, y + 0.24 * th, tw, 0.62 * th, a, '\t'.join(f[12:]), int(f[7]), int(f[8]), int(f[10])))
    return W, H, items

def check(path):
    W, H, items = load(path); out = []
    texts = [t for t in items if t[0] == 'T' and t[7] >= 0.25 and t[8].strip()]
    rects = [r for r in items if r[0] == 'R']
    vis = []; part = {}
    for t in texts:
        _, n, d, x, y, w, h, a, s = t[:9]
        x0, y0, x1, y1 = int(x), int(y), int(x + w + 0.999), int(y + h + 0.999)
        bw, bh = max(1, x1 - x0), max(1, y1 - y0)
        m = bytearray(bw * bh); narrow = bytearray(bw * bh)   # which pixels of the ink box later areas paint over
        for r in rects:
            if r[1] < n or r[2] != d: continue
            ax0, ay0, ax1, ay1 = max(x0, r[3]), max(y0, r[4]), min(x1, r[3] + r[5]), min(y1, r[4] + r[6])
            if ax0 >= ax1 or ay0 >= ay1: continue
            for yy in range(ay0 - y0, ay1 - y0):
                for xx in range(ax0 - x0, ax1 - x0):
                    m[yy * bw + xx] = 1
                    if r[5] < 0.3 * W: narrow[yy * bw + xx] = 1
        cov = sum(m) / float(bw * bh)
        if cov >= 0.9: continue                                # hidden behind a menu or a page
        vis.append(t)
        if sum(narrow) / float(bw * bh) > 0.08: part[n] = sum(narrow) / float(bw * bh)
    for t in vis:
        _, n, d, x, y, w, h, a, s, clip, cx, cw = t
        if d == 1 and (x < 0 or x + w > W + 0.5): out.append('OUTSIDE  "%s" at %d,%d width %.0f (window %dx%d)' % (s, x, y, w, W, H))
        if clip and cw > 0 and (x < cx - 1 or x + w > cx + cw + 1.5): out.append('CUT      "%s" at %d,%d needs %.0f, has %d' % (s, x, y, w, cw))
        if n in part: out.append('COVERED  "%s" at %d,%d: %d %% hidden by something drawn over it' % (s, x, y, round(100 * part[n])))
        if s.rstrip().endswith(('…', '...')): out.append('SHORTENED "%s" at %d,%d' % (s, x, y))
    for i in range(len(vis)):
        a = vis[i]
        for j in range(i + 1, len(vis)):
            b = vis[j]
            if a[2] != b[2]: continue
            ox = min(a[3] + a[5], b[3] + b[5]) - max(a[3], b[3]); oy = min(a[4] + a[6], b[4] + b[6]) - max(a[4], b[4])
            if ox > 1.0 and oy > 1.0:
                if a[8] == b[8] and abs(a[3] - b[3]) <= 1 and abs(a[4] - b[4]) <= 1: continue   # the same text drawn twice
                out.append('OVERLAP  "%s" (%d,%d w %.0f) and "%s" (%d,%d w %.0f): %.0f x %.0f px' % (a[8], a[3], a[4], a[5], b[8], b[3], b[4], b[5], ox, oy))
            elif oy > 1.0 and -2.0 < ox <= 1.0 and min(a[5], b[5]) > 6:
                out.append('TOUCH    "%s" and "%s" at y %d: gap %.1f px' % (a[8], b[8], a[4], -ox))
    return len(vis), out

bad = 0
for p in sys.argv[1:]:
    n, out = check(p); bad += len(out)
    print('%-28s %3d texts  %s' % (p.split('/')[-1], n, 'ok' if not out else '%d findings' % len(out)))
    for o in sorted(set(out)): print('    ' + o)
print('findings', bad)
