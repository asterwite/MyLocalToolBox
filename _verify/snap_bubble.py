# Snap pet window repeatedly, detect speech bubble (bright rounded rect at top of pet window).
import subprocess, time, sys, re
from PIL import ImageGrab, ImageChops

CS = r'D:\下载\Make\Make\qtclient\_verify\petrect.exe'
OUT = r'D:\下载\Make\Make\qtclient\_verify'

def pet_rect():
    out = subprocess.check_output([CS]).decode('gbk', 'replace')
    for line in out.splitlines():
        if 'area' not in line:
            continue
        m = re.search(r'rect (-?\d+),(-?\d+) (\d+)x(\d+) area (\d+)', line)
        if not m:
            continue
        L, T, w, h, area = map(int, m.groups())
        if 20000 < area < 500000 and w < 600:  # pet window (main is ~1.1M)
            return (L, T, L + w, T + h)
    return None

hits = []
prev = None
for i in range(26):
    r = pet_rect()
    if r is None:
        time.sleep(0.7)
        continue
    im = ImageGrab.grab(bbox=r, all_screens=True)
    # bubble zone: top 40px strip
    top = im.crop((0, 0, im.width, 40)).convert('L')
    hist = top.histogram()
    bright = sum(hist[200:]) / max(1, sum(hist))
    if bright > 0.15:
        hits.append((i, round(bright, 3), r))
        im.save(f'{OUT}\\bubble_{i}.png')
    if prev is not None and im.size == prev.size:
        diff = ImageChops.difference(im.convert('RGB'), prev.convert('RGB'))
        bbox = diff.getbbox()
    prev = im
    time.sleep(0.7)

print('hits:', hits if hits else 'NONE')
