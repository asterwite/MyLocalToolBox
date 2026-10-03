# Verify sleep state: pet window should stay put (no walking) while animation keeps playing.
import subprocess, re, time
from PIL import ImageGrab, ImageChops

CS = r'D:\下载\Make\Make\qtclient\_verify\petrect.exe'

def pet_rect():
    try:
        out = subprocess.check_output([CS], timeout=5).decode('gbk', 'replace')
    except Exception:
        return None
    for line in out.splitlines():
        m = re.search(r'rect (-?\d+),(-?\d+) (\d+)x(\d+) area (\d+)', line)
        if not m:
            continue
        L, T, w, h, area = map(int, m.groups())
        if 20000 < area < 500000 and w < 600:
            return (L, T, w, h)
    return None

pos = []
frames = []
for i in range(14):
    r = pet_rect()
    if r:
        pos.append((r[0], r[1]))
        if i in (2, 7, 12):
            im = ImageGrab.grab(bbox=(r[0], r[1], r[0] + r[2], r[1] + r[3]), all_screens=True)
            frames.append(im.convert('RGB'))
            im.save(rf'D:\下载\Make\Make\qtclient\_verify\sleep_{i}.png')
    time.sleep(2)

if not pos:
    print('PET-NOT-VISIBLE')
else:
    xs = set(p[0] for p in pos); ys = set(p[1] for p in pos)
    sizes = set()
    r = pet_rect()
    if r: sizes.add((r[2], r[3]))
    print('samples:', len(pos), 'x-set:', xs, 'y-set:', ys, 'size:', sizes)
    print('MOVED' if len(xs) > 1 or len(ys) > 1 else 'STAYED-PUT')
    if len(frames) == 3 and frames[0].size == frames[1].size == frames[2].size:
        d01 = sum(1 for p in ImageChops.difference(frames[0], frames[1]).getdata() if p != (0, 0, 0))
        d12 = sum(1 for p in ImageChops.difference(frames[1], frames[2]).getdata() if p != (0, 0, 0))
        print('frame-diff 0-1:', d01, ' 1-2:', d12, '=>', 'ANIMATING' if d01 > 800 and d12 > 800 else 'STATIC?')
