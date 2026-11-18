# SPDX-License-Identifier: MIT
# Draws the Mend icon: a waveform that is clipped and clicky on the left and
# clean on the right, mended at the dot. Writes mend-icon.svg and
# mend-icon-small.svg (for 16-32 px).
import math, sys
def wave_path(x0, x1, cy, A, N=600):
    pts=[]
    for i in range(N+1):
        t=i/N; x=x0+(x1-x0)*t
        y=math.sin(t*2*2*math.pi)
        if t<0.5:
            y=max(-0.6,min(0.6,y*1.35))                  # clipped
            y+=1.25*math.exp(-((t-0.135)/0.007)**2)       # a click
        pts.append((x,cy-A*y))
    return "M"+" L".join(f"{x:.1f},{y:.1f}" for x,y in pts)

def icon(size_note=""):
    x0,x1,cy,A=180,844,532,165
    d=wave_path(x0,x1,cy,A)
    return f'''<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1024 1024" width="1024" height="1024">
  <title>Mend</title>
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#262b35"/>
      <stop offset="1" stop-color="#111317"/>
    </linearGradient>
    <linearGradient id="wave" x1="{x0}" y1="0" x2="{x1}" y2="0" gradientUnits="userSpaceOnUse">
      <stop offset="0" stop-color="#f2a65a"/>
      <stop offset="0.42" stop-color="#f2a65a"/>
      <stop offset="0.58" stop-color="#40d6c4"/>
      <stop offset="1" stop-color="#40d6c4"/>
    </linearGradient>
    <radialGradient id="glow" cx="0.5" cy="0.5" r="0.5">
      <stop offset="0" stop-color="#40d6c4" stop-opacity="0.40"/>
      <stop offset="1" stop-color="#40d6c4" stop-opacity="0"/>
    </radialGradient>
  </defs>
  <rect x="32" y="32" width="960" height="960" rx="216" fill="url(#bg)"/>
  <rect x="33.5" y="33.5" width="957" height="957" rx="214.5" fill="none" stroke="#ffffff" stroke-opacity="0.08" stroke-width="3"/>
  <circle cx="512" cy="{cy}" r="170" fill="url(#glow)"/>
  <path d="{d}" fill="none" stroke="url(#wave)" stroke-width="48" stroke-linecap="round" stroke-linejoin="round"/>
  <circle cx="512" cy="{cy}" r="42" fill="#40d6c4" stroke="#15181d" stroke-width="16"/>
</svg>
'''
if __name__ == '__main__':
    import re
    full = icon()
    open('mend-icon.svg', 'w').write(full)
    # small sizes: thicker line, bigger dot, no glow
    small = full.replace('stroke-width="48"', 'stroke-width="76"')
    small = small.replace('r="42" fill="#40d6c4" stroke="#15181d" stroke-width="16"',
                          'r="60" fill="#40d6c4" stroke="#15181d" stroke-width="22"')
    small = re.sub(r'  <circle cx="512" cy="\d+" r="170" fill="url\(#glow\)"/>\n', '', small)
    open('mend-icon-small.svg', 'w').write(small)
