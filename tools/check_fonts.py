#!/usr/bin/env python3
"""Every character the plug-in can draw must be in its built-in font.

The VST3/AU/CLAP draws all text with the embedded Arimo fonts (resources/fonts) and never asks the system for a
font. This check reads every string literal of the JSFX and fails if one holds a character that neither Arimo
Regular nor Arimo Bold contains (the check mark U+2713 is drawn by the plug-in itself, src/JuceGfx.cpp).

    python3 tools/check_fonts.py jsfx/Ahh.jsfx        (needs: pip install fonttools)
"""
import re, sys
from fontTools.ttLib import TTFont

DRAWN = {0x2713}   # drawn as a path, not taken from a font

def main(path):
    src = open(path, encoding='utf-8').read()
    cmaps = [set(TTFont('resources/fonts/' + f).getBestCmap()) for f in ('Mulish-Regular.ttf', 'Mulish-SemiBold.ttf')]
    have = cmaps[0] & cmaps[1]
    used, bad = set(), []
    for n, line in enumerate(src.split('\n'), 1):
        code = line.split('//')[0] if '"' not in line.split('//')[0] else line   # keep lines whose strings hold //
        for s in re.findall(r'"((?:[^"\\]|\\.)*)"', code):
            for ch in s:
                if ord(ch) < 128: continue
                used.add(ch)
                if ord(ch) not in have and ord(ch) not in DRAWN: bad.append((n, ch))
    if bad:
        for n, ch in bad: print('line %d: U+%04X %s is not in the built-in font' % (n, ord(ch), ch))
        return 1
    print('check_fonts: ok (%d special characters in use: %s)' % (len(used), ' '.join(sorted(used))))
    return 0

if __name__ == '__main__':
    sys.exit(main(sys.argv[1] if len(sys.argv) > 1 else 'jsfx/Ahh.jsfx'))
