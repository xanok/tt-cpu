#!/usr/bin/env python3
"""Make SVG pictures of some views of the schematic pages, for docs/schematics.md
(GitHub draws SVG inline, not HTML). Needs Chrome or Chromium.
Run from anywhere:  python3 schematics/pictures.py   (schematics/make.sh runs it)."""
import html, os, re, shutil, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, '..', 'docs', 'schematics')
VIEWS = [   # page, view in the page, picture
    ('chip.html', 'tt_um_xanok_cpu8', 'chip.svg'),
    ('chip.html', 'tt_um_xanok_cpu8/core', 'cpu8.svg'),
    ('chip.html', 'tt_um_xanok_cpu8/core/!state', 'cpu8_states.svg'),
    ('tangnano20k.html', 'tangnano20k_top', 'tangnano20k.svg'),
    ('testbench.html', 'tb', 'testbench.svg'),
]
SCRIPT = """<script>
setTimeout(() => {
  const api = window.__vlogview;
  document.documentElement.setAttribute('data-theme', 'light');
  api.goPath(%r);
  document.body.setAttribute('data-svg', api.svgText().text);
}, 200);
</script>"""

chrome = next((c for c in ('google-chrome', 'google-chrome-stable', 'chromium', 'chromium-browser') if shutil.which(c)), None)
if not chrome:
    sys.exit('pictures.py: no Chrome or Chromium found; the pictures are left as they are')
os.makedirs(OUT, exist_ok=True)
with tempfile.TemporaryDirectory() as tmp:
    for page, view, pic in VIEWS:
        text = open(os.path.join(HERE, page), encoding='utf-8').read()
        test = os.path.join(tmp, 'p.html')
        open(test, 'w', encoding='utf-8').write(text.replace('</body>', SCRIPT % view + '</body>'))
        dom = subprocess.run([chrome, '--headless=new', '--no-sandbox', '--disable-gpu', '--window-size=1600,1000',
                              '--virtual-time-budget=5000', '--dump-dom', 'file://' + test],
                             capture_output=True, text=True).stdout
        m = re.search(r'data-svg="([^"]*)"', dom)
        if not m:
            sys.exit('pictures.py: no picture from %s %s' % (page, view))
        open(os.path.join(OUT, pic), 'w', encoding='utf-8').write(html.unescape(m.group(1)) + '\n')
        print('docs/schematics/' + pic)
