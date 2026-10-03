#!/usr/bin/env python3
# 「喝水」桌宠动画 —— 全自制 rig 版（2026-10-02 重做定稿）。
# 与 v1（make_drink_water_anim.py，吃冰淇淋素材拼接）不同：本版不再借用任何
# 源视频帧的道具与手臂，逐帧由程序摆姿势创作：
#   底稿 = 待机呼吸休闲 原帧（角色本体，用户模型）
#   表情 = 供体帧脸部矩形对位换脸（同角色同头型，无缝）
#   手臂 = 按玩水枪帧的「深蓝袖+肤色小手」风格程序化绘制
#   水瓶/瓶盖/垃圾桶/星光/水滴 = 程序化精灵（复用 v1 渲染器）
#
# 用法: python tools/make_drink_rig_anim.py [--test 36,60,90]
# 产物: pet-frames/喝水/NNN.webp + manifest.json + _verify/rig_*.png/gif
import json
import math
import os
import sys

import numpy as np
from PIL import Image, ImageDraw

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'tools'))
import make_drink_water_anim as V1  # 复用 水瓶/瓶盖/垃圾桶/星光 渲染器

FR = os.path.join(ROOT, 'pet-frames')
OUT = os.path.join(FR, '喝水')
QA = os.path.join(ROOT, '_verify')
W, H = 420, 236

# ---------- 调色（采自角色本体） ----------
SKIN = (247, 222, 214, 255)
SKIN_SH = (216, 176, 166, 255)
SLEEVE = (58, 68, 112, 255)
OUTLINE = (46, 54, 94, 255)
CUFF = (243, 243, 247, 255)

# ---------- 底稿与换脸 ----------
BASE_ID = ('待机呼吸休闲', 72)
FACE_RECT = (170, 104, 252, 160)   # 脸部矩形（含眼/腮/嘴，边缘落在头发/平肤上）
EYE_RECT = (196, 112, 234, 136)    # 用于对位的相关窗

def load(fname):
    return np.array(Image.open(os.path.join(FR, fname)).convert('RGBA'))

def align_face(donor, base):
    """在 EYE_RECT 窗口 ±8px 内找最优 (dx,dy)。"""
    x0, y0, x1, y1 = EYE_RECT
    tpl = base[y0:y1, x0:x1, :3].astype(float)
    best = (1e18, 0, 0)
    for dy in range(-8, 9):
        for dx in range(-8, 9):
            patch = donor[y0 + dy:y1 + dy, x0 + dx:x1 + dx, :3].astype(float)
            d = float(np.abs(patch - tpl).mean())
            if d < best[0]:
                best = (d, dx, dy)
    return best[1], best[2]

def make_face(base, donor, name):
    dx, dy = align_face(donor, base)
    out = base.copy()
    x0, y0, x1, y1 = FACE_RECT
    patch = donor[y0 + dy:y1 + dy, x0 + dx:x1 + dx].copy()
    # 边缘 3px 羽化，避免矩形硬接缝
    h, w = patch.shape[:2]
    ramp = np.ones((h, w), float)
    edge = np.minimum(1.0, (np.arange(3) + 1) / 4.0)
    for k in range(3):
        ramp[k, :] = np.minimum(ramp[k, :], edge[k])
        ramp[h - 1 - k, :] = np.minimum(ramp[h - 1 - k, :], edge[k])
        ramp[:, k] = np.minimum(ramp[:, k], edge[k])
        ramp[:, w - 1 - k] = np.minimum(ramp[:, w - 1 - k], edge[k])
    a = (patch[:, :, 3:4].astype(float) / 255.0) * ramp[:, :, None]
    region = out[y0:y1, x0:x1].astype(float)
    out[y0:y1, x0:x1] = (patch.astype(float) * a + region * (1 - a)).astype(np.uint8)
    print('face %s aligned (%+d,%+d)' % (name, dx, dy))
    return Image.fromarray(out)

BASE = load('%s/%03d.webp' % BASE_ID)
FACES = {
    'open': Image.fromarray(BASE),
    'bliss': make_face(BASE, load('哈欠连天/180.webp'), 'bliss'),
    'bliss2': make_face(BASE, load('哈欠连天/176.webp'), 'bliss2'),
    'content': make_face(BASE, load('工作状态-雀跃庆祝/168.webp'), 'content'),
    'smile': make_face(BASE, load('大口吃零食/228.webp'), 'smile'),
}

# ---------- 手臂 ----------
SHOULDER = (262, 166)  # 藏在头发右缘后

def draw_arm(img, hand, sag=14):
    """从发缘后伸出的袖+手；hand=握瓶点。返回无（直接画在 img 上）。"""
    d = ImageDraw.Draw(img)
    sx, sy = SHOULDER
    hx, hy = hand
    mx, my = (sx + hx) / 2, (sy + hy) / 2 + sag  # 肘部自然下垂
    pts = [ (sx, sy), (mx, my), (hx + 7, hy + 2) ]
    d.line(pts, fill=SLEEVE, width=11, joint='curve')
    d.line(pts, fill=OUTLINE, width=13, joint='curve')
    d.line(pts, fill=SLEEVE, width=9, joint='curve')
    # 袖口
    d.ellipse([hx + 1, hy - 6, hx + 11, hy + 6], fill=CUFF, outline=OUTLINE)
    # 小手（盖在瓶身上）
    d.ellipse([hx - 7, hy - 6, hx + 7, hy + 7], fill=SKIN, outline=OUTLINE)
    d.ellipse([hx - 2, hy - 7, hx + 5, hy - 1], fill=SKIN)  # 拇指
    d.arc([hx - 7, hy - 6, hx + 7, hy + 7], 200, 320, fill=SKIN_SH, width=2)

# ---------- 水瓶摆位 ----------
def place_bottle(frame, mouth, pil, level):
    """瓶嘴钉在 mouth；pil=PIL角(正=顶向左倒向脸)。返回握点(手的位置)。"""
    spr = V1.bottle_sprite(level).rotate(pil, expand=True, resample=Image.BICUBIC)
    sw, sh = spr.size
    bw, bh = 24, 56
    beta = math.radians(pil)
    upx, upy = -math.sin(beta), -math.cos(beta)
    mx, my = mouth
    ox = int(round(mx - upx * (bh / 2.0 - 7) - sw / 2.0))
    oy = int(round(my - upy * (bh / 2.0 - 7) - sh / 2.0))
    frame.alpha_composite(spr, (ox, oy))
    grip = (mx - upx * (bh - 7 - 16), my - upy * (bh - 7 - 16))
    return grip

def place_cap(frame, pos, pil):
    c = V1.CAP.rotate(pil, expand=True, resample=Image.BICUBIC)
    frame.alpha_composite(c, (int(pos[0] - c.size[0] / 2), int(pos[1] - c.size[1] / 2)))

# ---------- 分镜 ----------
LIPS = (205, 137)     # 唇位
CHEST = (224, 170)    # 胸前持瓶位
BIN_X, GROUND = 322, 214

def build_timeline():
    tl = []
    def F(**kw):
        kw.setdefault('face', 'open'); kw.setdefault('mouth', LIPS)
        kw.setdefault('pil', 0); kw.setdefault('level', 100)
        kw.setdefault('cap', 'on'); kw.setdefault('arm', True)
        kw.setdefault('fx', None); kw.setdefault('bob', 0)
        tl.append(kw)
    # P1 举起开盖 (0-23)
    for i in range(24):
        t = i / 23.0
        mx = 252 + (LIPS[0] + 1 - 252) * t
        my = 205 + (LIPS[1] + 3 - 205) * t
        pil = 2 + 18 * t
        f = dict(mouth=(mx, my), pil=pil, face='smile',
                 cap='on' if i < 18 else 'hover',
                 fx='cappop' if i == 18 else ('cappop2' if i in (19, 20, 21) else None))
        F(**f)
    # P2 喝水 (24-77) 3 通吞咽
    levels = [(100, 70), (70, 40), (40, 14)]
    for g, (lv0, lv1) in enumerate(levels):
        for k in range(18):
            pulse = 8 if k in (5, 6, 7, 8, 9, 10) else 0
            lv = lv0 + (lv1 - lv0) * k / 17.0
            ease = min(k / 4.0, 1.0)
            F(mouth=(LIPS[0] + 1 - ease, LIPS[1] + 3 - ease * 3),
              pil=20 + 6 * ease + pulse, level=lv,
              face='bliss' if k % 18 < 16 else 'bliss2',
              cap=None, bob=1 if k in (6, 7, 8, 9) else 0,
              fx='bubble' if k in (6, 7, 8, 9, 10) else None)
    # P3 爽 (78-101)
    for i in range(24):
        t = i / 23.0
        mx = LIPS[0] + (CHEST[0] - LIPS[0]) * min(t * 1.6, 1)
        my = LIPS[1] + (CHEST[1] - LIPS[1]) * min(t * 1.6, 1)
        pil = 26 + (4 - 26) * min(t * 1.6, 1)
        face = 'bliss' if i < 8 else ('open' if i < 16 else 'content')
        F(mouth=(mx, my), pil=pil, level=14, face=face, cap=None,
          fx='ahh' if 6 <= i < 20 else None)
    # P4 盖盖 (102-119)
    for i in range(18):
        cap = ('flyback', min(i / 8.0, 1.0)) if i < 9 else 'on'
        F(mouth=CHEST, pil=4, level=14, face='smile', cap=cap,
          fx='click' if i == 10 else None)
    # P5 投瓶 (120-159)
    for i in range(40):
        if i < 6:   # 瓶离手小弧线
            t = i / 5.0
            F(mouth=(CHEST[0] + t * 26, CHEST[1] - t * 18), pil=4 + t * 20,
              level=14, face='open', cap='on', fx=('fly', i))
        else:
            F(face='open' if not (14 <= i <= 15) else 'bliss', cap='gone',
              fx=('toss', i - 6), arm=False)
    # P6 结尾 (160-179)
    for i in range(20):
        F(face='content', cap='gone', arm=False, fx=None)
    return tl

# ---------- 合成 ----------
def compose_frame(kw, idx):
    bob = kw.get('bob', 0) + (1 if 24 <= idx < 78 and idx % 18 in (6, 7, 8, 9) else 0)
    breath = math.sin(idx * 0.33) * 0.8
    frame = FACES[kw['face']].copy()
    fx_layer = Image.new('RGBA', (W, H), (0, 0, 0, 0))
    d = ImageDraw.Draw(fx_layer)
    fx = kw['fx']
    cap = kw['cap']
    ax = 0
    # 垃圾桶（最底层画，无遮挡关系）
    tossk = None
    if isinstance(fx, tuple) and fx[0] == 'toss':
        tossk = fx[1]
    if tossk is not None:
        by = GROUND - 60
        if tossk < 8:
            t = tossk / 7.0
            paste_y = GROUND - (1 - t) * (1 - t) * 90 - 60
            V1_paste_bin(frame, BIN_X, paste_y, 0)
        else:
            lid = 0.0
            if 2 <= tossk < 8:
                lid = (tossk - 2) / 3.0 if tossk < 5 else (8 - tossk) / 3.0
            V1_paste_bin(frame, BIN_X, by, min(1, max(0, lid)))
            if tossk == 9:
                V1.draw_star(d, BIN_X + 27, by - 6, 7, (255, 255, 255, 235))
                V1.draw_star(d, BIN_X + 44, by - 16, 4, (178, 226, 255, 220))
            if 9 < tossk < 14:
                for sxy, r in (((BIN_X + 27, by - 10), 5), ((BIN_X + 46, by - 22), 3)):
                    al = max(0, 235 - (tossk - 9) * 55)
                    V1.draw_star(d, sxy[0], sxy[1] - (tossk - 9) * 3, r, (255, 255, 255, al))
            if 16 <= tossk < 24:
                wig = math.sin((tossk - 16) * 1.3) * 4 * (1 - (tossk - 16) / 8.0)
                V1_paste_bin(frame, BIN_X, by, 0, wig)
            if tossk >= 24:
                t = (tossk - 24) / 15.0
                V1_paste_bin(frame, BIN_X, by + t * t * 150, 0)
    # 瓶子
    cap_hover_pos = (246, 96)
    if cap != 'gone':
        grip = place_bottle(frame, kw['mouth'], kw['pil'], kw['level'])
        if kw.get('arm', True):
            draw_arm(frame, grip)
        beta = math.radians(kw['pil'])
        upx, upy = -math.sin(beta), -math.cos(beta)
        mpos = (kw['mouth'][0] - upx * 2, kw['mouth'][1] - upy * 2)
        if cap == 'on':
            place_cap(frame, mpos, kw['pil'])
        elif cap == 'hover':
            b = math.sin(idx * 0.9) * 2
            place_cap(frame, (cap_hover_pos[0], cap_hover_pos[1] + b), 12 * math.sin(idx * 0.7))
        elif isinstance(cap, tuple) and cap[0] == 'flyback':
            t = cap[1]
            sx, sy = cap_hover_pos
            place_cap(frame, (sx + (mpos[0] - sx) * t, sy + (mpos[1] - sy) * t), 40 * (1 - t))
    elif isinstance(fx, tuple) and fx[0] == 'fly':
        # 离手小弧线（尚未进垃圾桶阶段）
        pass
    elif isinstance(fx, tuple) and fx[0] == 'toss':
        k = fx[1]
        fly_t = (k - 0) / 10.0
        if 0 <= fly_t <= 1:
            sx, sy = CHEST[0] + 26, CHEST[1] - 18
            tx, ty = BIN_X + 2, GROUND - 52
            mx2, my2 = (sx + tx) / 2, min(sy, ty) - 44
            px = (1 - fly_t) ** 2 * sx + 2 * (1 - fly_t) * fly_t * mx2 + fly_t ** 2 * tx
            py = (1 - fly_t) ** 2 * sy + 2 * (1 - fly_t) * fly_t * my2 + fly_t ** 2 * ty
            if k in (1, 2):
                d.line([(px - 16, py + 10), (px - 4, py + 2)], fill=(255, 255, 255, 150), width=2)
            V1.paste_rot(frame, V1.bottle_sprite(8), px, py, 6 + fly_t * 260)
    # 特效
    if fx == 'cappop':
        V1.draw_star(d, 232, 84, 6, (255, 255, 255, 235))
        V1.draw_star(d, 258, 100, 4, (255, 255, 255, 225))
    elif fx == 'cappop2':
        V1.draw_star(d, 238, 78 - (idx % 3) * 2, 4, (255, 255, 255, 200))
        V1.draw_star(d, 262, 96, 3, (255, 255, 255, 190))
    elif fx == 'bubble':
        d.ellipse([kw['mouth'][0] + 7 + math.sin(idx * 2.1) * 3, kw['mouth'][1] - 13 - (idx % 5) * 3,
                   kw['mouth'][0] + 11 + math.sin(idx * 2.1) * 3, kw['mouth'][1] - 9 - (idx % 5) * 3],
                  fill=(200, 235, 252, 210))
    elif fx == 'ahh':
        ph = idx % 7
        for sx2, r in ((LIPS[0] - 52, 5), (LIPS[0] - 66, 4), (LIPS[0] - 12, 6)):
            yy2 = 96 - ph * 4
            al = max(0, 225 - ph * 34)
            d.ellipse([sx2 - r, yy2 - r * 1.3, sx2 + r, yy2 + r * 1.3], fill=(168, 224, 250, al))
        if idx % 4 == 0:
            V1.draw_star(d, LIPS[0] - 40, 78, 4, (255, 255, 255, 210))
    elif fx == 'click':
        V1.draw_star(d, CHEST[0], CHEST[1] - 30, 8, (255, 255, 255, 245))
        V1.draw_star(d, CHEST[0] + 12, CHEST[1] - 22, 4, (178, 226, 255, 225))
    frame.alpha_composite(fx_layer)
    # 呼吸位移
    if bob or abs(breath) > 0.01:
        frame = frame.transform(frame.size, Image.AFFINE,
                                (1, 0, 0, 0, 1, -(bob + breath)),
                                resample=Image.BICUBIC)
    return frame

def V1_paste_bin(frame, x, y, lid, rot=0):
    V1.paste_rot(frame, V1.render_bin(lid), x + 27, y + 36, rot)

# ---------- 主流程 ----------
def main():
    os.makedirs(OUT, exist_ok=True)
    os.makedirs(QA, exist_ok=True)
    test = None
    if len(sys.argv) > 2 and sys.argv[1] == '--test':
        test = [int(x) for x in sys.argv[2].split(',')]
    tl = build_timeline()
    print('总帧数', len(tl))
    if test:
        for i in test:
            f = compose_frame(tl[i], i)
            vis = Image.new('RGBA', (W, H), (48, 48, 58, 255))
            vis.alpha_composite(f)
            vis.convert('RGB').save(os.path.join(QA, 'rig_f%03d.png' % i))
            print('saved rig_f%03d.png' % i)
        return
    # 清旧帧
    for old in os.listdir(OUT):
        os.remove(os.path.join(OUT, old))
    man = json.load(open(os.path.join(FR, 'manifest.json'), encoding='utf-8'))
    frames = []
    gif = []
    for idx, kw in enumerate(tl):
        f = compose_frame(kw, idx)
        p = os.path.join(OUT, '%03d.webp' % idx)
        f.save(p, 'WEBP', quality=88)
        frames.append('喝水/%03d.webp' % idx)
        vis = Image.new('RGBA', f.size, (56, 56, 66, 255))
        vis.alpha_composite(f)
        gif.append(vis.convert('RGB').resize((300, 168), Image.LANCZOS))
    man['喝水'] = {'count': len(frames), 'fps': 24.0, 'frames': frames}
    json.dump(man, open(os.path.join(FR, 'manifest.json'), 'w', encoding='utf-8'),
              ensure_ascii=False, indent=1)
    print('输出', len(frames), '帧')
    # QA 接触表
    qa_ids = [0, 12, 18, 30, 42, 54, 66, 78, 88, 98, 108, 116, 126, 136, 146, 156, 166, 176]
    TW, TH = 210, 118
    cols = 6
    rows = (len(qa_ids) + cols - 1) // cols
    sheet = Image.new('RGB', (TW * cols, (TH + 12) * rows), (40, 40, 48))
    dd = ImageDraw.Draw(sheet)
    for k, i in enumerate(qa_ids):
        if i >= len(gif):
            continue
        im = gif[i].resize((TW, TH))
        x, y = (k % cols) * TW, (k // cols) * (TH + 12)
        sheet.paste(im, (x, y + 12))
        dd.text((x + 2, y + 1), 'f%d' % i, fill=(255, 255, 0))
    sheet.save(os.path.join(QA, 'rig_sheet.png'))
    gif[0].save(os.path.join(QA, 'rig_preview.gif'), save_all=True,
                append_images=gif[1:], duration=int(1000 / 24), loop=0)
    print('QA ->', os.path.join(QA, 'rig_sheet.png'), '& rig_preview.gif')

if __name__ == '__main__':
    main()
