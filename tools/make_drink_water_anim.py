#!/usr/bin/env python3
# 生成「喝水」桌宠动画：把 吃冰淇淋融化 的甜筒逐帧替换成矿泉水瓶，
# 外加开盖、水位消耗、爽星光、盖盖、滑入垃圾桶六段分镜。
#
# 与被否掉的四版方案不同点（2026-10-01 定稿）：
#  - 基座整段沿用源动作（人物全程有原生动画，不是待机悬浮道具）；
#  - 瓶子沿甜筒同一握点轴替换，手部像素回贴在瓶子之上（手指 visible 握瓶）；
#  - 甜筒贴嘴帧由瓶身整体盖住嘴部，无需修补嘴唇；
#  - 伸舌帧(#56-#78)全部跳过；垃圾桶滑入到手边接瓶，避免冻结手臂挥掷穿模。
#
# 用法: python tools/make_drink_water_anim.py [--qa]
# 产物: pet-frames/喝水/NNN.webp + manifest.json 合并 + _verify/drink_water_*.png/gif
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, 'pet-frames', '吃冰淇淋融化')
OUT = os.path.join(ROOT, 'pet-frames', '喝水')
QA = os.path.join(ROOT, '_verify')
W, H = 420, 236

# ---------------- 分镜配置 ----------------
# 每段: (源帧号列表, 水位%, 瓶倾角deg[顶向脸为正], 瓶盖状态, 额外特效)
# cap: 'on' 在瓶上 / 'hover' 悬浮头顶 / ('flyback', t0..1) 飞回 / None 空瓶无盖
def build_timeline():
    tl = []  # each: dict(src, level, base_tilt, cap, fx)
    for i in range(10, 41):
        TH_LIMITS[i] = (-50, 15)  # 举起段道具大幅摆动
    # base_tilt 为瓶轴基础角（PIL 约定：负=瓶顶向右倒向脸），实际角=base_tilt+对齐角th
    # P1 举起开盖 #10-#40：瓶子随道具自然角度；#20 盖子弹出悬浮
    for i in range(10, 41):
        cap = 'on' if i < 20 else 'hover'
        fx = 'cappop' if i == 20 else ('cappop2' if i in (21, 22, 23) else None)
        tl.append(dict(src=i, level=100, base_tilt=0, cap=cap, fx=fx, mdx=6, mdy=6))
    # P2 喝水 #80-#90 x2 通循环：水位 100->70->35，吞咽时多倒 8 度
    for gulp, (lv0, lv1) in enumerate(((100, 68), (68, 33))):
        for k, i in enumerate(range(80, 91)):
            bt = -30 - (8 if k in (3, 4, 5) else 0)
            lv = lv0 + (lv1 - lv0) * k / 10.0
            fx = 'bubble' if k in (2, 3, 4, 5, 6) else None
            tl.append(dict(src=i, level=lv, base_tilt=bt, cap=None, fx=fx, mdx=16, mdy=14))
    # P3 爽 #84-#88 循环 + 水滴星光：最后一口 33->8，瓶慢慢回正
    for k in range(14):
        i = (84, 85, 86, 87, 88, 87, 86, 85)[k % 8]
        lv = 33 + (8 - 33) * min(k / 8.0, 1.0)
        tl.append(dict(src=i, level=lv, base_tilt=-26 + min(k, 6) * 2.0, cap=None,
                       fx='ahh' if k >= 2 else None, mdx=16, mdy=14))
    # P4 盖盖 #104-#119：瓶回正，盖子飞回瓶口，末端咔哒闪光
    for k, i in enumerate(range(104, 120)):
        cap = ('flyback', min(k / 9.0, 1.0)) if k < 10 else 'on'
        fx = 'click' if k == 11 else None
        tl.append(dict(src=i, level=8, base_tilt=-20 + k * 0.7, cap=cap, fx=fx, mdx=4, mdy=6))
    # P5 投瓶：基座定格 #119，垃圾桶滑入->瓶抛入->盖翻->摆动->沉没
    for k in range(40):
        tl.append(dict(src=119, level=8, base_tilt=-12, cap='gone', fx=('toss', k), mdx=4, mdy=6))
    # P6 结尾 #120-#131 微笑站立
    for i in range(120, 132):
        tl.append(dict(src=i, level=8, base_tilt=-12, cap='gone', fx=None, mdx=4, mdy=6))
    return tl

# ---------------- 甜筒检测 ----------------
def cone_core_mask(rgb, a):
    r, g, b = rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int), rgb[:, :, 2].astype(int)
    cream = (r > 226) & (g > 200) & (b > 188) & ((r - g) >= 10) & ((r - g) <= 42) & ((g - b) >= 6) & ((g - b) <= 32)
    tan = (r >= 194) & (r <= 228) & (g >= 142) & (g <= 178) & (b >= 98) & (b <= 134) & ((r - b) > 60)
    return (cream | tan) & (a > 180)

def label_blobs(mask):
    lab = np.zeros(mask.shape, int)
    cur = 0
    Hh, Ww = mask.shape
    for y in range(Hh):
        for x in range(Ww):
            if mask[y, x] and lab[y, x] == 0:
                cur += 1
                stack = [(y, x)]
                lab[y, x] = cur
                while stack:
                    cy, cx = stack.pop()
                    for dy in (-1, 0, 1):
                        for dx in (-1, 0, 1):
                            ny, nx = cy + dy, cx + dx
                            if 0 <= ny < Hh and 0 <= nx < Ww and mask[ny, nx] and lab[ny, nx] == 0:
                                lab[ny, nx] = cur
                                stack.append((ny, nx))
    return lab, cur

def dilate(m, it=1):
    for _ in range(it):
        p = np.pad(m, 1)
        m = p[0:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, 0:-2] | p[1:-1, 2:] | p[1:-1, 1:-1]
    return m

def core_blob(rgb, a):
    """当前帧甜筒核心色块（用于质心跟踪）。"""
    core = cone_core_mask(rgb, a)
    box = np.zeros_like(core)
    box[20:195, 100:190] = True
    core &= box
    lab, n = label_blobs(core)
    best, bestn = 0, 0
    for k in range(1, n + 1):
        ys, xs = np.where(lab == k)
        if len(ys) < 40:
            continue
        cx = xs.mean()
        if 105 <= cx <= 190 and len(ys) > bestn:
            bestn, best = len(ys), k
    if not best:
        return None
    return lab == best

def build_template():
    """在 #24 基准帧上生长完整道具剪影（伞+勺+筒），返回 (mask, centroid, mouth_offset)。"""
    img = np.array(Image.open(os.path.join(SRC, '024.webp')).convert('RGBA'))
    rgb, a = img[:, :, :3], img[:, :, 3]
    r, g, b = rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int), rgb[:, :, 2].astype(int)
    blob = core_blob(rgb, a)
    hair = (b - r > 40) & (b > 110)
    neutral = (r > 205) & (g > 205) & (b > 205) & (np.ptp(np.stack([r, g, b]), axis=0) < 14)
    bgm = a < 60

    def grow(seed, allow_dark, ylim, xlim, iters):
        m = seed.copy()
        for _ in range(iters):
            p = np.pad(m, 1)
            ne = p[0:-2, 1:-1] | p[2:, 1:-1] | p[1:-1, 0:-2] | p[1:-1, 2:]
            excl = (hair | neutral | bgm) if not allow_dark else (hair | neutral)
            cand = ne & ~m & ~excl & (a > 120)
            lim = np.zeros_like(m)
            lim[ylim[0]:ylim[1], xlim[0]:xlim[1]] = True
            cand &= lim
            if not cand.any():
                break
            m |= cand
        return m

    g1 = grow(blob, False, (0, H), (0, W), 10)
    ys, xs = np.where(blob)
    mx = np.stack([r, g, b], axis=0)
    satur = (mx.max(axis=0) - mx.min(axis=0)) > 45
    useed = satur & (a > 120) & ~hair
    ulim = np.zeros_like(blob)
    ulim[80:130, 112:170] = True
    useed &= ulim
    umb = grow(useed, True, (78, 132), (108, 175), 10)
    cseed = np.zeros_like(blob)
    cseed[ys.max() - 6:ys.max() + 2, xs.min():xs.max() + 1] = g1[ys.max() - 6:ys.max() + 2, xs.min():xs.max() + 1]
    cone = grow(cseed, True, (ys.max() - 6, 200), (xs.min() - 4, xs.max() + 5), 16)
    full = (g1 | umb | cone) & ~hair & ~neutral & (a > 100)
    full = dilate(full, 1)
    ys, xs = np.where(full)
    cx, cy = float(xs.mean()), float(ys.mean())
    band = full[:, int(cx) - 6:int(cx) + 7]
    top_y = int(np.where(band.any(axis=1))[0].min())
    mouth_off = (6.0, top_y - cy + 6)
    return full, (cx, cy), mouth_off

TEMPLATE, C24, MOUTH_OFF = build_template()
# 模板核心块（用于旋转对齐打分）
_img24 = np.array(Image.open(os.path.join(SRC, '024.webp')).convert('RGBA'))
CORE24 = core_blob(_img24[:, :, :3], _img24[:, :, 3])

TH_LIMITS = {}  # src -> (lo, hi)；举起段大角度，其余段小幅

def align_pose(blob_t, lo=-12, hi=12):
    """暴力搜索 (θ,dx,dy) 使旋转平移后的模板核心与当前帧核心块 IoU 最大。"""
    ys, xs = np.where(blob_t)
    c = (float(xs.mean()), float(ys.mean()))
    tim = Image.fromarray((CORE24 * 255).astype(np.uint8))
    best = (-1, 0, 0, 0)
    for th in range(lo, hi + 1, 5):
        rot = tim.rotate(th, center=C24, resample=Image.NEAREST)
        ra = np.array(rot) > 100
        rys, rxs = np.where(ra)
        for ddx in range(-6, 7, 2):
            for ddy in range(-6, 7, 2):
                ys2, xs2 = rys + ddy, rxs + ddx
                ok = (ys2 >= 0) & (ys2 < H) & (xs2 >= 0) & (xs2 < W)
                inter = blob_t[ys2[ok], xs2[ok]].sum()
                score = inter / max(1, len(ys))
                if score > best[0]:
                    best = (score, th, ddx, ddy)
    # 细搜
    _, th0, dx0, dy0 = best
    for th in range(th0 - 4, th0 + 5, 2):
        rot = tim.rotate(th, center=C24, resample=Image.NEAREST)
        ra = np.array(rot) > 100
        rys, rxs = np.where(ra)
        for ddx in range(dx0 - 2, dx0 + 3):
            for ddy in range(dy0 - 2, dy0 + 3):
                ys2, xs2 = rys + ddy, rxs + ddx
                ok = (ys2 >= 0) & (ys2 < H) & (xs2 >= 0) & (xs2 < W)
                inter = blob_t[ys2[ok], xs2[ok]].sum()
                score = inter / max(1, len(ys))
                if score > best[0]:
                    best = (score, th, ddx, ddy)
    _, th, ddx, ddy = best
    return th, c[0] + ddx, c[1] + ddy

def rotated_template(th, cx, cy):
    """完整剪影绕 C24 旋 th 后平移到 (cx,cy)。"""
    tim = Image.fromarray((TEMPLATE * 255).astype(np.uint8))
    rot = tim.rotate(th, center=C24, resample=Image.NEAREST)
    ra = np.array(rot) > 100
    rys, rxs = np.where(ra)
    dy, dx = int(round(cy - C24[1])), int(round(cx - C24[0]))
    ys2, xs2 = rys + dy, rxs + dx
    m = np.zeros((H, W), bool)
    ok = (ys2 >= 0) & (ys2 < H) & (xs2 >= 0) & (xs2 < W)
    m[ys2[ok], xs2[ok]] = True
    return m

def rot_pt(pt, th, cx, cy):
    a = math.radians(th)
    px, py = pt[0] - C24[0], pt[1] - C24[1]
    rx = px * math.cos(a) - py * math.sin(a)
    ry = px * math.sin(a) + py * math.cos(a)
    return (cx + rx, cy + ry)

def inpaint(img, hole, srcok=None):
    """扩散修补：hole=True 的像素用邻域有效像素逐步填充，保 RGBA。
    srcok: 额外限制哪些像素可作填充种子（如排除道具色，防彩虹扩散）。"""
    out = img.astype(float).copy()
    valid = (~hole).copy()
    if srcok is not None:
        valid &= srcok
    filled = valid.copy()
    for _ in range(80):
        pending = hole & ~filled
        if not pending.any():
            break
        vf = filled.astype(float)
        acc = np.zeros_like(out)
        cnt = np.zeros(hole.shape)
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if dy == 0 and dx == 0:
                    continue
                ys = slice(max(0, dy), min(H, H + dy))
                xs = slice(max(0, dx), min(W, W + dx))
                yt = slice(max(0, -dy), min(H, H - dy))
                xt = slice(max(0, -dx), min(W, W - dx))
                v = np.zeros_like(vf)
                v[yt, xt] = vf[ys, xs]
                for c in range(4):
                    ch = np.zeros_like(out[:, :, c])
                    ch[yt, xt] = out[ys, xs, c] * vf[ys, xs]  # 只累计已填充邻居
                    acc[:, :, c] += ch
                cnt += v
        newly = pending & (cnt > 0)
        if not newly.any():
            break
        for c in range(4):
            out[:, :, c][newly] = (acc[:, :, c][newly] / cnt[newly])
        filled |= newly
    return out.astype(np.uint8)

# ---------------- 水瓶精灵 ----------------
OUTLINE = (46, 54, 94, 255)
CAPBLUE = (91, 138, 214, 255)
WATER = (114, 186, 235, 242)
WATER_HI = (168, 218, 246, 255)
PLASTIC = (235, 244, 252, 60)
LABEL = (250, 252, 255, 235)

def render_bottle(level, ss=4):
    """竖直水瓶（修长款），(bw,bh) 内容区；level 0-100 水位。返回 RGBA Image。"""
    bw, bh = 24, 56
    im = Image.new('RGBA', (bw * ss, bh * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = ss
    body_l, body_r = 3 * s, (bw - 3) * s
    neck_l, neck_r = (bw // 2 - 4) * s, (bw // 2 + 4) * s
    shoulder_y = 13 * s
    neck_y = 7 * s
    bot_y = (bh - 2) * s
    # 塑料体
    d.polygon([(body_l, bot_y - 2 * s), (body_l, shoulder_y), (neck_l, neck_y),
               (neck_r, neck_y), (body_r, shoulder_y), (body_r, bot_y - 2 * s),
               (body_r - 2 * s, bot_y), (body_l + 2 * s, bot_y)], fill=PLASTIC)
    # 水
    if level > 0:
        wl, wr = body_l + 1 * s, body_r - 1 * s
        top = bot_y - (bot_y - shoulder_y - 1 * s) * (level / 100.0)
        wave = 1.0 * s
        d.polygon([(wl, top + wave), (wr, top - wave * 0.4), (wr, bot_y - 1 * s),
                   (wl, bot_y - 1 * s)], fill=WATER)
        d.line([(wl, top + wave), (wr, top - wave * 0.4)], fill=WATER_HI, width=s)
    # 高光
    d.line([(body_l + 3 * s, shoulder_y + 4 * s), (body_l + 3 * s, bot_y - 4 * s)],
           fill=(255, 255, 255, 95), width=2 * s)
    # 标签
    ly0, ly1 = (bh // 2 - 5) * s, (bh // 2 + 5) * s
    d.rectangle([body_l + 1 * s, ly0, body_r - 1 * s, ly1], fill=LABEL)
    d.ellipse([bw // 2 * s - 2 * s, ly0 + 2 * s, bw // 2 * s + 2 * s, ly1 - 2 * s],
              fill=(120, 186, 235, 235))
    # 轮廓
    d.line([(body_l, bot_y - 2 * s), (body_l, shoulder_y), (neck_l, neck_y),
            (neck_r, neck_y), (body_r, shoulder_y), (body_r, bot_y - 2 * s),
            (body_r - 2 * s, bot_y), (body_l + 2 * s, bot_y), (body_l, bot_y - 2 * s)],
           fill=OUTLINE, width=s, joint='curve')
    d.ellipse([neck_l, neck_y - 2 * s, neck_r, neck_y + 2 * s], fill=OUTLINE)  # 瓶口
    return im.resize((bw, bh), Image.LANCZOS)

def render_cap(ss=4):
    cw, ch = 16, 9
    im = Image.new('RGBA', (cw * ss, ch * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = ss
    d.rounded_rectangle([1 * s, 1 * s, (cw - 1) * s, (ch - 1) * s], radius=2 * s, fill=CAPBLUE)
    d.rounded_rectangle([1 * s, 1 * s, (cw - 1) * s, (ch - 1) * s], radius=2 * s, outline=OUTLINE, width=s)
    for k in range(4):  # 竖纹
        x = (3 + k * 2.6) * s
        d.line([(x, 2 * s), (x, (ch - 2) * s)], fill=(70, 110, 180, 255), width=s)
    return im.resize((cw, ch), Image.LANCZOS)

def render_bin(lid_open, ss=4):
    """垃圾桶，lid_open 0-1 盖翻开度。"""
    bw, bh = 54, 62
    im = Image.new('RGBA', (bw * ss, (bh + 10) * ss), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    s = ss
    body_l, body_r = 5 * s, (bw - 5) * s
    top_y, bot_y = 12 * s, (bh - 2) * s
    d.polygon([(body_l + 3 * s, top_y), (body_r - 3 * s, top_y),
               (body_r - 8 * s, bot_y), (body_l + 8 * s, bot_y)],
              fill=(150, 172, 202, 255))
    for k in range(1, 4):  # 竖条
        x = body_l + (body_r - body_l) * k / 4.0
        d.line([(x, top_y + 3 * s), (x - 2 * s, bot_y - 3 * s)], fill=(126, 150, 186, 255), width=s)
    d.polygon([(body_l + 3 * s, top_y), (body_r - 3 * s, top_y),
               (body_r - 8 * s, bot_y), (body_l + 8 * s, bot_y)],
              outline=OUTLINE, width=s)
    if lid_open > 0.05:
        # 掀开的盖（绕左铰链旋转）
        lid = Image.new('RGBA', (40 * ss, 10 * ss), (0, 0, 0, 0))
        dl = ImageDraw.Draw(lid)
        dl.rounded_rectangle([0, 1 * s, 39 * s, 8 * s], radius=3 * s, fill=(135, 158, 192, 255))
        dl.rounded_rectangle([0, 1 * s, 39 * s, 8 * s], radius=3 * s, outline=OUTLINE, width=s)
        lid = lid.rotate(-70 * lid_open, expand=True, resample=Image.BICUBIC)
        im.alpha_composite(lid, (int(body_l) - 12, int(top_y) - lid.size[1] + 6))
    else:
        d.rounded_rectangle([body_l - 4 * s, top_y - 7 * s, body_r + 4 * s, top_y + 1 * s],
                            radius=3 * s, fill=(135, 158, 192, 255))
        d.rounded_rectangle([body_l - 4 * s, top_y - 7 * s, body_r + 4 * s, top_y + 1 * s],
                            radius=3 * s, outline=OUTLINE, width=s)
        d.line([(bw // 2 - 5 * s, top_y - 7 * s), (bw // 2 - 5 * s, top_y - 3 * s)], fill=OUTLINE, width=s)
        d.line([(bw // 2 + 5 * s, top_y - 7 * s), (bw // 2 + 5 * s, top_y - 3 * s)], fill=OUTLINE, width=s)
    return im.resize((bw, bh + 10), Image.LANCZOS)

def draw_star(d, cx, cy, r, fill):
    pts = []
    for k in range(8):
        rad = r if k % 2 == 0 else r * 0.38
        a = math.pi / 4 * k - math.pi / 2
        pts.append((cx + rad * math.cos(a), cy + rad * math.sin(a)))
    d.polygon(pts, fill=fill)

# ---------------- 合成 ----------------
BOTTLE_CACHE = {}
def bottle_sprite(level):
    key = int(level // 4)
    if key not in BOTTLE_CACHE:
        BOTTLE_CACHE[key] = render_bottle(max(4, key * 4))
    return BOTTLE_CACHE[key]

CAP = render_cap()

def paste_rot(base, spr, cx, cy, deg):
    if deg:
        spr = spr.rotate(-deg, expand=True, resample=Image.BICUBIC)
    base.alpha_composite(spr, (int(cx - spr.size[0] / 2), int(cy - spr.size[1] / 2)))

def hair_mask(rgb, a):
    r, g, b = rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int), rgb[:, :, 2].astype(int)
    return (b - r > 40) & (b > 110) & (a > 180)

def propish_mask(rgb, a):
    """道具色族：饱和粉彩(伞)+奶白/粉(勺)+棕(筒)。用作捕获与种子排除。"""
    r, g, b = rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int), rgb[:, :, 2].astype(int)
    mx = np.stack([r, g, b], axis=0)
    satur = (mx.max(axis=0) - mx.min(axis=0)) > 70   # 伞面粉彩；皮肤(max-min≈48)排除
    warmish = (r > 215) & ((r - b) > 10) & ((g - b) >= 4)
    tanish = (r >= 190) & (r <= 235) & ((r - b) > 40) & ((g - b) > 22)
    return (satur | warmish | tanish) & (a > 120)

BASE_CACHE = {}
def prep_base(src_id):
    """擦除甜筒并修补。模板经 (θ,dx,dy) 旋转平移对齐后取掩码，瓶嘴=掩码顶部带。"""
    if src_id in BASE_CACHE:
        return BASE_CACHE[src_id]
    img = np.array(Image.open(os.path.join(SRC, '%03d.webp' % src_id)).convert('RGBA'))
    rgb, a = img[:, :, :3], img[:, :, 3]
    r, g, b = rgb[:, :, 0].astype(int), rgb[:, :, 1].astype(int), rgb[:, :, 2].astype(int)
    hair = ((b - r) > 40) & (b > 110)
    neutral = (r > 205) & (g > 205) & (b > 205) & (np.ptp(np.stack([r, g, b]), axis=0) < 14)
    blob = core_blob(rgb, a)
    if blob is None:
        BASE_CACHE[src_id] = (Image.fromarray(img), None, None, None, 0.0, a < 40, None, 0.0)
        return BASE_CACHE[src_id]
    lo, hi = TH_LIMITS.get(src_id, (-12, 12))
    th, cx, cy = align_pose(blob, lo, hi)
    mask = rotated_template(th, cx, cy) | blob
    # 嘴位用核心块质心带（伞在左侧会拉偏均值，不能用全掩码均值）
    bys, bxs = np.where(blob)
    bcx = float(bxs.mean())
    band = mask[:, int(bcx) - 5:int(bcx) + 8]
    top_y = float(np.where(band.any(axis=1))[0].min())
    mouth = (bcx + 6, top_y + 4)
    anchor = (cx, cy)
    # 道具色捕获：伞/撑杆总在道具质心左侧，粉与棕杆在左走廊内加大半径捕获
    pish = propish_mask(rgb, a) & ~hair & ~neutral
    left_zone = np.zeros((H, W), bool)
    left_zone[:, :int(bcx) - 2] = True
    pinkish = (r > 195) & ((r - g) >= 36) & ((g - b) >= -15) & ((g - b) <= 15)
    pole = ((r - b) > 50) & ((g - b) > 25) & (r > 130)
    catch = ((pish & dilate(mask, 10)) | (pinkish & dilate(mask, 22)) | (pole & dilate(mask, 22))) & left_zone
    hole = (dilate(mask, 7) | catch | (pish & dilate(mask, 10))) & (a > 0)
    # 统一扩散修补：透明背景与人物色都可作为种子（RGBA 一起扩散），
    # 道具色族禁止作种子；最后对洞内低 alpha 阈值化
    warm_near = ((pish | pinkish | pole) & ~hole & dilate(hole, 3)) & left_zone
    srcok = ~((pish & ~hole) | warm_near)
    out = inpaint(img, hole, srcok)
    lowalpha = hole & (out[:, :, 3] < 110)
    out[lowalpha] = 0
    res = (Image.fromarray(out), hole, anchor, mouth, float(th), a < 40, bcx, float(np.where(hole.any(axis=1))[0].max()))
    BASE_CACHE[src_id] = res
    return res

def compose_bottle(frame, hole, bgfree, mouth, pil, level, bcx=None, hole_bot=0.0):
    """画瓶子：瓶嘴钉在 mouth，瓶轴角 pil（PIL 约定，负=顶向右倒向脸）。
    显示范围 = 擦除区 ∪ 透明背景 ∪ 前景特区(洞右上,喝水时瓶身本在脸前)。"""
    spr = bottle_sprite(level).rotate(pil, expand=True, resample=Image.BICUBIC)
    sw, sh = spr.size
    bw, bh = 24, 56
    beta = math.radians(pil)
    upx, upy = -math.sin(beta), -math.cos(beta)
    mx, my = mouth
    ox = int(round(mx - upx * (bh / 2.0 - 7) - sw / 2.0))
    oy = int(round(my - upy * (bh / 2.0 - 7) - sh / 2.0))
    x0, y0 = max(0, ox), max(0, oy)
    x1, y1 = min(W, ox + sw), min(H, oy + sh)
    if x1 <= x0 or y1 <= y0:
        return
    region = spr.crop((x0 - ox, y0 - oy, x1 - ox, y1 - oy))
    if hole is not None:
        allowed = dilate(hole, 1) | bgfree
        if bcx is not None:
            yy, xx = np.mgrid[0:H, 0:W]
            front = dilate(hole, 18) & (xx >= bcx - 6) & (yy <= hole_bot + 4)
            allowed |= front
    else:
        allowed = np.ones((H, W), bool)
    sub = allowed[y0:y1, x0:x1]
    arr = np.array(region)
    arr[~sub] = 0
    frame.alpha_composite(Image.fromarray(arr), (x0, y0))

def compose(tl, qa_idx=None):
    strip = []
    for idx, fr in enumerate(tl):
        base, hole, anchor, mouth, th, bgfree, bcx, hole_bot = prep_base(fr['src'])
        frame = base.copy()
        d = ImageDraw.Draw(frame)
        if hole is None:
            strip.append((idx, frame))
            continue
        ax, ay = anchor
        cap = fr['cap']
        level = fr['level']
        fx = fr['fx']
        pil = fr.get('base_tilt', 0.0) + th   # 本帧瓶轴角（PIL 约定）
        mx, my = mouth[0] + fr.get('mdx', 4), mouth[1] + fr.get('mdy', 4)
        if cap != 'gone':
            compose_bottle(frame, hole, bgfree, (mx, my), pil, level, bcx, hole_bot)
            if cap == 'on':
                beta = math.radians(pil)
                capx = mx + (-math.sin(beta)) * 2
                capy = my + (-math.cos(beta)) * 2
                frame.alpha_composite(CAP.rotate(pil, expand=True, resample=Image.BICUBIC),
                                      (int(capx - CAP.size[0] / 2), int(capy - CAP.size[1] / 2)))
            elif cap == 'hover':
                bob = math.sin(idx * 0.9) * 2
                paste_rot(frame, CAP, ax + 46, ay - 58 + bob, 14 * math.sin(idx * 0.7))
            elif isinstance(cap, tuple) and cap[0] == 'flyback':
                t = cap[1]
                sx, sy = ax + 46, ay - 58
                fxp = sx + (mx - sx) * t
                fyp = sy + (my - sy) * t
                paste_rot(frame, CAP, fxp, fyp, 40 * (1 - t))
        elif isinstance(fx, tuple) and fx[0] == 'toss':
            k = fx[1]
            bin_x = 316
            ground_y = 216
            if k < 8:
                t = k / 7.0
                by = ground_y - (1 - t) * (1 - t) * 90
                paste_rot(frame, render_bin(0), bin_x, by - 60, 0)
            else:
                by = ground_y - 60
                lid = 0.0
                fly_t = (k - 6) / 10.0
                if 0 <= fly_t <= 1:
                    sx, sy = ax + 6, ay - 26
                    tx, ty = bin_x + 2, ground_y - 52
                    mx2, my2 = (sx + tx) / 2, min(sy, ty) - 46
                    px = (1 - fly_t) ** 2 * sx + 2 * (1 - fly_t) * fly_t * mx2 + fly_t ** 2 * tx
                    py = (1 - fly_t) ** 2 * sy + 2 * (1 - fly_t) * fly_t * my2 + fly_t ** 2 * ty
                    if k in (7, 8):
                        d.line([(px - 16, py + 10), (px - 4, py + 2)], fill=(255, 255, 255, 150), width=2)
                        d.line([(px - 20, py + 20), (px - 8, py + 12)], fill=(255, 255, 255, 110), width=2)
                    paste_rot(frame, bottle_sprite(6), px, py, -12 + fly_t * 240)
                if 8 <= k < 14:
                    lid = (k - 8) / 3.0 if k < 11 else (14 - k) / 3.0
                paste_rot(frame, render_bin(min(1, max(0, lid))), bin_x, by, 0)
                if k == 15:
                    draw_star(d, bin_x + 27, by - 6, 7, (255, 255, 255, 235))
                    draw_star(d, bin_x + 44, by - 16, 4, (178, 226, 255, 220))
                if 15 < k < 20:
                    for sxy, r in (((bin_x + 27, by - 10), 5), ((bin_x + 46, by - 22), 3)):
                        al = max(0, 235 - (k - 15) * 55)
                        draw_star(d, sxy[0], sxy[1] - (k - 15) * 3, r, (255, 255, 255, al))
                if 22 <= k < 30:
                    wig = math.sin((k - 22) * 1.3) * 4 * (1 - (k - 22) / 8.0)
                    paste_rot(frame, render_bin(0), bin_x, by, wig)
                if k >= 30:
                    t = (k - 30) / 9.0
                    paste_rot(frame, render_bin(0), bin_x, by + t * t * 130, 0)
        if fx == 'cappop':
            draw_star(d, ax + 30, ay - 66, 6, (255, 255, 255, 235))
            draw_star(d, ax + 58, ay - 48, 4, (255, 255, 255, 225))
        elif fx == 'cappop2':
            draw_star(d, ax + 36, ay - 72 - (idx % 3) * 2, 4, (255, 255, 255, 200))
            draw_star(d, ax + 62, ay - 52, 3, (255, 255, 255, 190))
        elif fx == 'bubble':
            bx2 = mx + 6 + math.sin(idx * 2.1) * 3
            by2 = my - 12 - (idx % 5) * 3
            d.ellipse([bx2 - 2, by2 - 2, bx2 + 2, by2 + 2],
                      fill=(200, 235, 252, 200))
        elif fx == 'ahh':
            ph = idx % 7
            for sx2, r in ((ax - 40, 5), (ax - 52, 4), (ax + 12, 6)):
                yy2 = ay - 60 - ph * 4
                al = max(0, 225 - ph * 34)
                d.ellipse([sx2 - r, yy2 - r * 1.3, sx2 + r, yy2 + r * 1.3], fill=(168, 224, 250, al))
            if idx % 4 == 0:
                draw_star(d, ax - 30, ay - 74, 4, (255, 255, 255, 210))
        elif fx == 'click':
            draw_star(d, mx, my - 4, 8, (255, 255, 255, 245))
            draw_star(d, mx + 12, my + 6, 4, (178, 226, 255, 225))
        strip.append((idx, frame))
        if qa_idx is not None and idx in qa_idx:
            vis = Image.new('RGBA', (W, H), (45, 45, 55, 255))
            vis.alpha_composite(frame)
            vis.convert('RGB').save(os.path.join(QA, 'dw_f%03d.png' % idx))
    return strip


def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(QA, exist_ok=True)
    tl = build_timeline()
    print('总帧数', len(tl))
    qa = {0, 10, 20, 36, 46, 60, 70, 80, 95, 110, 125, 140, 155, 165}
    strip = compose(tl, qa_idx=qa)
    # 输出 webp
    man = json.load(open(os.path.join(ROOT, 'pet-frames', 'manifest.json'), encoding='utf-8'))
    frames = []
    for idx, f in strip:
        p = os.path.join(OUT, '%03d.webp' % idx)
        bg = Image.new('RGBA', f.size, (0, 0, 0, 0))
        bg.alpha_composite(f)
        bg.save(p, 'WEBP', quality=88, lossless=False)
        frames.append('喝水/%03d.webp' % idx)
    man['喝水'] = {'count': len(frames), 'fps': 24.0, 'frames': frames}
    json.dump(man, open(os.path.join(ROOT, 'pet-frames', 'manifest.json'), 'w', encoding='utf-8'),
              ensure_ascii=False, indent=1)
    print('已输出', len(frames), '帧 ->', OUT)
    # QA 接触表
    ids = qa
    tiles = []
    for i in sorted(ids):
        if i < len(strip):
            tiles.append(strip[i][1])
    if tiles:
        TW, TH = 210, 118
        cols = 5
        rows = (len(tiles) + cols - 1) // cols
        sheet = Image.new('RGB', (TW * cols, (TH + 12) * rows), (40, 40, 48))
        dd = ImageDraw.Draw(sheet)
        for k, f in enumerate(tiles):
            im = f.resize((TW, TH))
            x, y = (k % cols) * TW, (k // cols) * (TH + 12)
            sheet.paste(im, (x, y + 12), im)
            dd.text((x + 2, y + 1), 'frame %d' % sorted(ids)[k], fill=(255, 255, 0))
        sheet.save(os.path.join(QA, 'drink_water_sheet.png'))
    # GIF 预览
    gif_frames = []
    for idx, f in strip:
        vis = Image.new('RGBA', f.size, (56, 56, 66, 255))
        vis.alpha_composite(f)
        gif_frames.append(vis.convert('RGB').resize((280, 157), Image.LANCZOS))
    gif_frames[0].save(os.path.join(QA, 'drink_water_preview.gif'), save_all=True,
                       append_images=gif_frames[1:], duration=int(1000 / 24), loop=0)
    print('QA ->', os.path.join(QA, 'drink_water_sheet.png'), '& drink_water_preview.gif')

if __name__ == '__main__':
    main()
