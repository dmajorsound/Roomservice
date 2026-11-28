#!/usr/bin/env python3
"""Builds the plug-in's embedded fonts: Mulish Regular and SemiBold (SIL Open Font License, resources/fonts/OFL-Mulish.txt)
plus the two characters the interface uses that Mulish does not have - the Greek alpha and the right arrow - taken
from Arimo (resources/fonts/OFL.txt) and scaled to Mulish's grid.

    python3 tools/build_fonts.py <folder with Mulish-Regular.ttf and Mulish-SemiBold.ttf>     (needs: pip install fonttools)
"""
import sys
from fontTools.ttLib import TTFont
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.pens.transformPen import TransformPen

ADD = [0x03B1, 0x2192]   # alpha, right arrow

def build(mulish, arimo, out):
    f, a = TTFont(mulish), TTFont(arimo)
    k = f['head'].unitsPerEm / a['head'].unitsPerEm
    gs, acm = a.getGlyphSet(), a.getBestCmap()
    for cp in ADD:
        if cp in f.getBestCmap(): continue
        name = 'uni%04X' % cp
        pen = TTGlyphPen(None)
        gs[acm[cp]].draw(TransformPen(pen, (k, 0, 0, k, 0, 0)))
        f['glyf'][name] = pen.glyph()
        f['hmtx'][name] = (round(a['hmtx'][acm[cp]][0] * k), round(a['hmtx'][acm[cp]][1] * k))
        for t in f['cmap'].tables:
            if t.isUnicode(): t.cmap[cp] = name
    f.setGlyphOrder(f['glyf'].glyphOrder)
    f.save(out)

if __name__ == '__main__':
    src = sys.argv[1].rstrip('/')
    build(src + '/Mulish-Regular.ttf', 'resources/fonts/Arimo-Regular.ttf', 'resources/fonts/Mulish-Regular.ttf')
    build(src + '/Mulish-SemiBold.ttf', 'resources/fonts/Arimo-Bold.ttf', 'resources/fonts/Mulish-SemiBold.ttf')
    print('built resources/fonts/Mulish-Regular.ttf and Mulish-SemiBold.ttf')
