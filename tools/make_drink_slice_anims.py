#!/usr/bin/env python3
# 云养喝饮料 v4：「晨间刷牙」的后半段本身就是端杯喝水（手是原装的、杯子是原画风格的、全程在动）。
# 从 f140（泡沫已消、杯子在嘴边）切片到结尾：
#   喝水   : 原样保留（含 f182 搞笑的吐水彩蛋）
#   喝橙汁 : 杯子乘法混合暖橙色调
#   喝奶茶 : 粉调杯子 + 开头吸管落进杯里的小动作；剪掉吐水段
#   喝咖啡 : 暖褐调杯子 + 喝的时候杯口飘热气；剪掉吐水段
# 杯子掩码 = 浅蓝灰簇（种子 #9cb4cc/#9cc0cc，TOL45；头发是深饱和蓝距离远，安全）。
# 注意：重跑 extract_pet_frames.py 后 manifest 重建，需要再跑一次本脚本。
# 用法: 在 qtclient 目录下  python tools/make_drink_slice_anims.py
# 依赖: pip install pillow numpy
import json
import math
import os

import numpy as np
from PIL import Image, ImageDraw

BASE = '晨间刷牙'
START = 140
SPIT_RANGE = (174, 192)  # 吐水段：除喝水外都剪掉
SEEDS = [(156, 180, 204), (156, 192, 204)]
TOL = 45.0

VARIANTS = [
    ('喝水', None, {'spit': True, 'straw': False, 'steam': False}),
    ('喝橙汁', (1.0, 0.90, 0.74), {'spit': False, 'straw': False, 'steam': False}),
    ('喝奶茶', (1.0, 0.85, 0.87), {'spit': False, 'straw': True, 'steam': False}),
    ('喝咖啡', (0.93, 0.87, 0.79), {'spit': False, 'straw': False, 'steam': True}),
]


def mug_mask(arr):
    rgb = arr[..., :3].astype(np.float32)
    seeds = np.array(SEEDS, dtype=np.float32)
    dist = np.sqrt(((rgb[..., None, :] - seeds[None, None, ...]) ** 2).sum(-1).min(axis=-1))
    return dist < TOL


def draw_straw(base, cx, cy_top, drop):
    """粉色条纹吸管，drop=下落进度 0..1（落进杯里）"""
    im = Image.new('RGBA', (16, 46), (0, 0, 0, 0))
    d = ImageDraw.Draw(im)
    d.line([8, 2, 8, 44], fill=(244, 166, 190, 255), width=7)
    d.line([5, 2, 5, 44], fill=(255, 214, 228, 255), width=2)
    yy = int(cy_top - 40 + 40 * drop)
    base.alpha_composite(im, (int(cx - 8), int(yy)))


def draw_steam(base, cx, top, phase):
    d = ImageDraw.Draw(base)
    for k, dx in enumerate((-1, 1)):
        pts = []
        for s in range(6):
            yy = top - s * 6
            xx = cx + dx * 7 + int(math.sin(phase * 2.2 + s * 1.0 + k * 2.1) * (2 + s * 1.2))
            pts.append((xx, yy))
        for a, b in zip(pts[:-1], pts[1:]):
            d.line([a, b], fill=(255, 255, 255, int(max(25, 140 - (top - b[1]) * 16))), width=2)


def main():
    root = 'pet-frames'
    manifest_path = os.path.join(root, 'manifest.json')
    man = json.load(open(manifest_path, encoding='utf-8'))
    if BASE not in man:
        print('base missing:', BASE)
        return 2
    src_frames = man[BASE]['frames']
    fps = man[BASE].get('fps', 24)

    for name, tint, opts in VARIANTS:
        outdir = os.path.join(root, name)
        os.makedirs(outdir, exist_ok=True)
        frames = []
        li = 0  # 切片内帧号（吸管/热气动画用）
        for si in range(START, len(src_frames)):
            if not opts['spit'] and SPIT_RANGE[0] <= si <= SPIT_RANGE[1]:
                continue
            im = Image.open(os.path.join(root, src_frames[si])).convert('RGBA')
            arr = np.asarray(im).copy()
            m = mug_mask(arr)
            ys, xs = np.nonzero(m)
            mcx = float(xs.mean()) if len(xs) else 210
            mcy = float(ys.mean()) if len(ys) else 140
            if tint:
                rgb = arr[..., :3].astype(np.float32)
                rgb[m] = np.clip(rgb[m] * np.array(tint, dtype=np.float32), 0, 255)
                arr[..., :3] = rgb.astype('uint8')
            base = Image.fromarray(arr, 'RGBA')
            if opts['straw'] and li <= 16:
                # 吸管落到杯口（杯子此时正举在嘴边），到位后轻弹一下
                drop = min(1.0, li / 10.0)
                drop = drop * drop  # ease-in 加速
                bounce = int(4 * math.sin((li - 10) / 6.0 * math.pi)) if li > 10 else 0
                draw_straw(base, mcx + 6, mcy - 26 + bounce, drop)
            if opts['steam'] and li <= 44:
                draw_steam(base, mcx, mcy - 20, phase=li * 0.25)
            f = '%03d.webp' % li
            base.save(os.path.join(outdir, f), 'WEBP', quality=88, method=4)
            frames.append(name + '/' + f)
            li += 1
        man[name] = {'count': len(frames), 'fps': fps, 'frames': frames}
        print('built %s: %d frames (%.1fs)' % (name, len(frames), len(frames) / fps))
    json.dump(man, open(manifest_path, 'w', encoding='utf-8'), ensure_ascii=False, indent=2)
    print('manifest:', len(man), 'actions')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
