# SPDX-License-Identifier: MIT
# Writes the wordmark and logo SVGs, with the text converted to outlines
# (DejaVu Sans) so they render the same everywhere. Needs fontTools.
import re, sys
from fontTools.ttLib import TTFont
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
sys.path.insert(0,'.')


font=TTFont('/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf')
gs=font.getGlyphSet(); cmap=font.getBestCmap(); upm=font['head'].unitsPerEm
def text_path(s, size, x, baseline, track=-0.03):
    scale=size/upm; pen=SVGPathPen(gs); cx=x
    for ch in s:
        g=cmap[ord(ch)]
        tp=TransformPen(pen,(scale,0,0,-scale,cx,baseline))
        gs[g].draw(tp)
        cx+=gs[g].width*scale+track*size
    return pen.getCommands(), cx

def wordmark(fill, size=200, x=0, baseline=0):
    d,end=text_path('mend',size,x,baseline)
    dot_r=size*0.075
    return d, end+dot_r*1.6, dot_r

def write_wordmark(name, fill):
    size=200; base=150
    d,dx,r=wordmark(fill,size,10,base)
    w=dx+r+14
    open(name,'w').write(f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} 200" width="{w:.0f}" height="200">
  <title>Mend</title>
  <path d="{d}" fill="{fill}"/>
  <circle cx="{dx:.1f}" cy="{base-r:.1f}" r="{r:.1f}" fill="#40d6c4"/>
</svg>
''')

def write_logo(name, fill):
    # icon (scaled) + wordmark
    icon=open('mend-icon.svg').read()
    inner=re.search(r'<svg[^>]*>(.*)</svg>',icon,re.S).group(1)
    inner=inner.replace('id="bg"','id="lbg"').replace('url(#bg)','url(#lbg)').replace('id="wave"','id="lwave"').replace('url(#wave)','url(#lwave)').replace('id="glow"','id="lglow"').replace('url(#glow)','url(#lglow)')
    inner=re.sub(r'<title>.*?</title>','',inner)
    s=240/1024
    size=200; base=178
    d,dx,r=wordmark(fill,size,270,base)
    w=dx+r+16
    open(name,'w').write(f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {w:.0f} 240" width="{w:.0f}" height="240">
  <title>Mend</title>
  <g transform="scale({s})">{inner}</g>
  <path d="{d}" fill="{fill}"/>
  <circle cx="{dx:.1f}" cy="{base-r:.1f}" r="{r:.1f}" fill="#40d6c4"/>
</svg>
''')

write_wordmark('mend-wordmark.svg','#e8ebf0')
write_wordmark('mend-wordmark-dark.svg','#15181d')
write_logo('mend-logo.svg','#e8ebf0')
write_logo('mend-logo-dark.svg','#15181d')
