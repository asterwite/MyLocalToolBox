#!/usr/bin/env python3
# 云养喝饮料 v4：自建流畅动画。人物 = 待机呼吸帧（呼吸浮动跟踪 + 喝时小踮步），
# 道具 = 软画风小尺寸 sprite（同色系柔边 + 竖向渐变 + 高光，无硬描边），
# 动作 = 分镜缓动（rise 过冲回弹 / 喝时摇摆 + 吞咽脉冲 / 落回弹跳），道具始终小于脸宽、
# 顶部不超过嘴线（不遮眼），奶茶吸管藏在杯后只露头顶（绝不横穿脸）。
# 注意：重跑 extract_pet_frames.py 后 manifest 重建，需要再跑一次本脚本。
# 用法: 在 qtclient 目录下  python tools/make_prop_drink_anims.py
# 依赖: pip install pillow numpy
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

BASE = '待机呼吸休闲'
MOUTH = (210, 133)
CHEST = (210, 174)
CX = 210
ANIM = 241


def ease_io(t):
    t = max(0.0, min(1.0, t))
    return t * t * (3 - 2 * t)


def ease_back(t, s=1.6):  # 过冲回弹
    t = max(0.0, min(1.0, t))
    return 1 + (s + 1) * (t - 1) ** 3 + s * (t - 1) ** 2


def seg(i, a, b):
    return max(0.0, min(1.0, (i - a) / float(b - a))) if b > a else (1.0 if i >= b else 0.0)


def soft(im, outline, blur=1.6, grow=2):
    """同色系柔边：放大的模糊剪影垫在下面当彩色软描边"""
    a = im.split()[3]
    pad = a.filter(ImageFilter.MaxFilter(grow * 2 + 1)).filter(ImageFilter.GaussianBlur(blur))
    under = Image.new('RGBA', im.size, outline + (0,))
    under.putalpha(pad.point(lambda v: int(v * 0.9)))
    out = Image.new('RGBA', im.size, (0, 0, 0, 0))
    out.alpha_composite(under)
    out.alpha_composite(im)
    return out


def gloss(base, box, alpha=90):
    """柔和高光块"""
    hl = Image.new('RGBA', base.size, (0, 0, 0, 0))
    d = ImageDraw.Draw(hl)
    d.ellipse(box, fill=(255, 255, 255, alpha))
    hl = hl.filter(ImageFilter.GaussianBlur(2))
    base.alpha_composite(hl)


# ---------------- 道具（小尺寸 · 软画风） ----------------

def prop_bottle():
    w, h = 30, 68
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([int(w*0.3), 0, int(w*0.7), int(h*0.14)], radius=2, fill=(250, 250, 253, 255))
    d.rounded_rectangle([int(w*0.10), int(h*0.12), int(w*0.90), h-1], radius=int(w*0.34),
                        fill=(178, 222, 252, 225))
    wy = int(h * 0.36)
    d.rounded_rectangle([int(w*0.13), wy, int(w*0.87), h-3], radius=int(w*0.28), fill=(134, 199, 244, 235))
    d.line([int(w*0.15), wy+2, int(w*0.85), wy+2], fill=(214, 238, 255, 255), width=2)
    d.rounded_rectangle([int(w*0.13), int(h*0.50), int(w*0.87), int(h*0.66)], radius=3,
                        fill=(255, 255, 255, 200))
    d.line([int(w*0.24), int(h*0.2), int(w*0.24), int(h*0.9)], fill=(255, 255, 255, 130), width=2)
    im = soft(im, (108, 150, 190))
    gloss(im, [int(w*0.14), int(h*0.2), int(w*0.4), int(h*0.75)], 95)
    return im


def prop_cap():
    im = Image.new('RGBA', (18, 10), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([1, 1, 16, 8], radius=2, fill=(250, 250, 253, 255))
    return soft(im, (108, 150, 190), 1.2, 1)


def prop_juice():
    w, h = 46, 64
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top, bot = int(h*0.06), h-1
    tw, bw = int(w*0.9), int(w*0.66)
    cx = w//2
    d.polygon([(cx-tw//2, top), (cx+tw//2, top), (cx+bw//2, bot), (cx-bw//2, bot)],
              fill=(238, 244, 250, 105), outline=(168, 186, 204, 220))
    jy = top + int((bot-top)*0.28)
    jwt = tw - int((tw-bw)*0.26)
    d.polygon([(cx-jwt//2, jy), (cx+jwt//2, jy), (cx+bw//2-2, bot-2), (cx-bw//2+2, bot-2)],
              fill=(255, 158, 64, 245))
    d.line([cx-jwt//2+2, jy, cx+jwt//2-2, jy], fill=(255, 214, 150, 255), width=2)
    d.line([cx-tw//2, top, cx+tw//2, top], fill=(222, 234, 246, 255), width=3)
    im = soft(im, (150, 168, 190))
    gloss(im, [int(w*0.2), top+4, int(w*0.42), bot-6], 85)
    return im


def prop_boba():
    w, h = 50, 64
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top, bot = int(h*0.10), h-1
    bw = int(w*0.84)
    cx = w//2
    d.rounded_rectangle([cx-bw//2, top, cx+bw//2, bot], radius=int(w*0.18), fill=(248, 233, 212, 235))
    ty = top + int(h*0.16)
    steps = 8
    for s in range(steps):
        y0, y1 = ty+(bot-ty)*s//steps, ty+(bot-ty)*(s+1)//steps
        col = (int(244-26*s/steps), int(212-56*s/steps), int(172-56*s/steps), 240)
        d.rectangle([cx-bw//2+3, y0, cx+bw//2-3, y1], fill=col)
    pr = 4
    for (fx, fy) in [(0.32, 0.88), (0.5, 0.94), (0.68, 0.87), (0.42, 0.79), (0.6, 0.76)]:
        d.ellipse([cx-bw//2+w*fx-pr, bot*fy-pr, cx-bw//2+w*fx+pr, bot*fy+pr], fill=(96, 62, 38, 255))
    d.rounded_rectangle([cx-bw//2, top, cx+bw//2, top+int(h*0.09)], radius=3,
                        fill=(238, 196, 138, 255), outline=(176, 141, 95, 220), width=2)
    d.ellipse([cx-5, top+3, cx+5, top+12], fill=(255, 214, 226, 255))  # 杯身小桃心
    im = soft(im, (170, 136, 96))
    gloss(im, [int(w*0.12), top+4, int(w*0.34), bot-6], 85)
    return im


def prop_straw():
    w, h = 10, 34
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.rounded_rectangle([w//2-3, 1, w//2+3, h-1], radius=2, fill=(247, 173, 199, 255))
    for y in range(4, h-4, 7):
        d.line([w//2-3, y, w//2+3, y+3], fill=(255, 220, 232, 255), width=2)
    return soft(im, (196, 130, 152), 1.0, 1)


def prop_coffee():
    w, h = 58, 40
    im = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    top, bot = int(h*0.16), h-1
    cw = int(w*0.68)
    cx = w//2
    hw = int(w*0.14)
    d.arc([cx+cw//2-hw//2, top+3, cx+cw//2+hw, bot-3], start=-80, end=95,
          fill=(248, 242, 232, 255), width=4)
    body = [(cx-cw//2, top), (cx+cw//2, top), (cx+int(cw*0.36), bot), (cx-int(cw*0.36), bot)]
    d.polygon(body, fill=(250, 245, 236, 252))
    d.ellipse([cx-cw//2+3, top-3, cx+cw//2-3, top+int(h*0.2)], fill=(84, 52, 30, 255))
    d.line([cx-int(cw*0.46), top+int(h*0.3), cx+int(cw*0.46), top+int(h*0.3)],
           fill=(216, 180, 90, 255), width=2)
    im = soft(im, (166, 152, 136))
    gloss(im, [int(w*0.16), top+2, int(w*0.36), bot-4], 100)
    return im


def draw_steam(base, cx, top, phase):
    d = ImageDraw.Draw(base)
    for k, dx in enumerate((-1, 1)):
        pts = []
        for s in range(6):
            yy = top - s * 5
            xx = cx + dx * 6 + int(math.sin(phase * 2.2 + s * 1.0 + k * 2.1) * (2 + s * 1.1))
            pts.append((xx, yy))
        for a, b in zip(pts[:-1], pts[1:]):
            d.line([a, b], fill=(255, 255, 255, int(max(25, 130 - (top - b[1]) * 15))), width=2)


def sparkle(base, x, y, r, alpha):
    d = ImageDraw.Draw(base)
    d.line([x - r, y, x + r, y], fill=(255, 255, 255, alpha), width=2)
    d.line([x, y - r, x, y + r], fill=(255, 255, 255, alpha), width=2)


def paste(base, sprite, cx, cy, ang=0.0, scale=1.0):
    if scale != 1.0:
        sprite = sprite.resize((max(1, int(sprite.width * scale)), max(1, int(sprite.height * scale))), Image.LANCZOS)
    if abs(ang) > 0.05:
        sprite = sprite.rotate(ang, expand=True, resample=Image.BICUBIC)
    base.alpha_composite(sprite, (int(cx - sprite.width / 2), int(cy - sprite.height / 2)))


def main():
    root = 'pet-frames'
    manifest_path = os.path.join(root, 'manifest.json')
    man = json.load(open(manifest_path, encoding='utf-8'))
    if BASE not in man:
        print('base missing:', BASE)
        return 2
    base_frames = man[BASE]['frames']
    n = min(ANIM, len(base_frames))
    idle, tops = [], []
    for rel in base_frames[:n]:
        im = Image.open(os.path.join(root, rel)).convert('RGBA')
        a = np.asarray(im)[:, :, 3]
        ys = np.nonzero((a > 200).any(axis=1))[0]
        tops.append(int(ys.min()) if len(ys) else 120)
        idle.append(im)
    med = float(np.median(tops))
    bob = [t - med for t in tops]

    sp = {'bottle': prop_bottle(), 'cap': prop_cap(), 'juice': prop_juice(),
          'boba': prop_boba(), 'straw': prop_straw(), 'coffee': prop_coffee()}

    def hop(base_src, k):
        """喝到吞咽点：人物整体轻踮 k 像素（内容上移）"""
        if k <= 0:
            return base_src
        out = Image.new('RGBA', base_src.size, (0, 0, 0, 0))
        out.alpha_composite(base_src, (0, -k))
        return out

    def compose(name, frame_fn):
        outdir = os.path.join(root, name)
        os.makedirs(outdir, exist_ok=True)
        frames = []
        for i in range(n):
            dy = bob[i]
            hopk = int(round(2.5 * max(0.0, math.sin((i - 108) / 82.0 * math.pi * 3)))) if 108 <= i <= 190 else 0
            base = hop(idle[i], hopk)
            frame_fn(base, i, dy - hopk)  # 道具跟随身体上踮
            f = '%03d.webp' % i
            base.save(os.path.join(outdir, f), 'WEBP', quality=88, method=4)
            frames.append(name + '/' + f)
        man[name] = {'count': len(frames), 'fps': man[BASE].get('fps', 24), 'frames': frames}
        print('built', name)

    def rest_y(prop_h):
        return CHEST[1] + prop_h * 0.5 - 6

    def mouth_y(prop_h):
        return MOUTH[1] + prop_h * 0.42

    # ---- 喝水：小瓶 + 拧盖飞出 + 仰头咕咚 + 拧回 ----
    def water(base, i, dy):
        bh = sp['bottle'].height
        t_tw = seg(i, 24, 58)
        t_up = ease_back(seg(i, 62, 96))
        t_dn = ease_io(seg(i, 182, 214))
        y = rest_y(bh) + dy
        y = y + (mouth_y(bh) + dy - y) * t_up - (mouth_y(bh) + dy - y) * t_dn
        ang = -12 * t_up + 12 * t_dn
        gulp = 1.0 + (0.04 * max(0.0, math.sin((i - 96) / 94.0 * math.pi * 3))) if 96 <= i <= 190 else 1.0
        if 24 <= i <= 32:
            y += int(2 * math.sin((i - 24) * 2.0))
        paste_y = y
        paste(base, sp['bottle'], CX, paste_y, ang=ang, scale=gulp if 96 <= i <= 190 else 1.0)
        # 盖：拧开飞出悬停（带闪烁）→ 喝完飞回
        ct = t_tw
        if i >= 200:
            ct = 1.0 - ease_io(seg(i, 200, 230))
        if ct > 0:
            cdx = 26 * ct + int(2 * math.sin(i * 0.2) * ct)
            cdy = -34 * ease_io(ct) + int(2 * math.sin(i * 0.27) * ct)
            paste(base, sp['cap'], CX + 20 + cdx, y - bh // 2 - 8 + cdy, ang=-300 * ct)
            if 60 <= i <= 180 and i % 40 in (0, 1):
                sparkle(base, CX + 20 + cdx, y - bh // 2 - 8 + cdy, 3, 160)
        if i in (112, 134):
            d = ImageDraw.Draw(base)
            d.text((CX + 18, y - bh // 2 - 4), '咕', fill=(255, 255, 255, 210))

    # ---- 喝橙汁：小冰橙汁端起轻晃 ----
    def juice(base, i, dy):
        jh = sp['juice'].height
        t_up = ease_back(seg(i, 48, 88))
        t_dn = ease_io(seg(i, 188, 224))
        y = rest_y(jh) + dy
        y = y + (mouth_y(jh) + dy - y) * t_up - (mouth_y(jh) + dy - y) * t_dn
        ang = -10 * t_up + 10 * t_dn + (2 * math.sin(i * 0.22) * t_up * (1 - t_dn))
        paste(base, sp['juice'], CX, y, ang=ang)
        if 100 <= i <= 180 and i % 30 == 0:
            sparkle(base, CX + 18, y - jh // 3, 3, 150)

    # ---- 喝奶茶：吸管"啵"地插下（只在杯上方）→ 端起吸 ----
    def boba(base, i, dy):
        bh = sp['boba'].height
        t_ins = seg(i, 20, 46)
        t_up = ease_back(seg(i, 66, 102))
        t_dn = ease_io(seg(i, 186, 220))
        recoil = int(3 * math.sin((i - 46) / 8.0 * math.pi)) if 46 <= i <= 54 else 0
        y = rest_y(bh) + dy + recoil
        y = y + (mouth_y(bh) + dy - y) * t_up - (mouth_y(bh) + dy - y) * t_dn
        ang = -13 * t_up + 13 * t_dn
        # 吸管画在杯后：只露杯口以上的一小截，绝不横穿脸
        straw_top = y - bh // 2 + 6
        if t_ins > 0:
            sy = straw_top - int((1 - ease_io(t_ins)) * 26)
            paste(base, sp['straw'], CX + 7, sy + sp['straw'].height // 2 + int(3 * math.sin(i * 0.8) * (1 - t_ins)), ang=ang * 0.5)
        paste(base, sp['boba'], CX, y, ang=ang)
        if 46 <= i <= 54:
            d = ImageDraw.Draw(base)
            r = 3 + (i - 46) * 2
            d.ellipse([CX + 7 - r, straw_top - r, CX + 7 + r, straw_top + r],
                      outline=(255, 255, 255, max(0, 210 - (i - 46) * 42)), width=2)
        if 120 <= i <= 170 and i % 26 == 0:
            sparkle(base, CX - 16, y - bh // 3, 3, 150)

    # ---- 喝咖啡：优雅轻啜 + 热气 ----
    def coffee(base, i, dy):
        ch = sp['coffee'].height
        t_up = ease_io(seg(i, 56, 108))
        t_dn = ease_io(seg(i, 196, 236))
        y = rest_y(ch) + dy
        y = y + (mouth_y(ch) + dy - y) * t_up - (mouth_y(ch) + dy - y) * t_dn
        ang = -9 * t_up + 9 * t_dn
        paste(base, sp['coffee'], CX, y, ang=ang)
        low = (y > MOUTH[1] + 20 + dy)
        if low:
            draw_steam(base, CX, y - ch // 2 - 3, phase=i * 0.2)

    compose('喝水', water)
    compose('喝橙汁', juice)
    compose('喝奶茶', boba)
    compose('喝咖啡', coffee)
    json.dump(man, open(manifest_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('manifest:', len(man), 'actions')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
