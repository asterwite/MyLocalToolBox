#!/usr/bin/env python3
# 从 dsh-pet 源视频重提取桌宠帧，保留 alpha 通道（RGBA）。
#
# 关键点（都是实际踩过的坑）：
#  1. 源视频是真 WebM（EBML）+ VP9 Profile 0 + 容器 ALPHA_MODE=1：alpha 存在
#     Matroska BlockAdditions 里。ffmpeg 原生 vp9 解码器不读它（解码出 yuv420p 全不透明），
#     必须显式 -c:v libvpx-vp9（libvpx 看到 ALPHA_MODE 会合并附加块 → yuva420p）。
#  2. PyAV 的 Packet 不透传 BlockAdditional 侧数据，所以 PyAV 两条路都拿不到 alpha，
#     只能走 ffmpeg CLI 的 stdout 管道（dsh-pet 官方 encode_hevc_alpha.sh 同款思路）。
#  3. 中文路径坑：ffmpeg.exe 走 Windows 宽字符 argv，直接传绝对路径没问题。
#  4. 旧提取丢 alpha 黑底烙进帧里，导致桌宠背景发黑 —— 本脚本即重提取修复版。
#
# 用法: python extract_pet_frames.py <dsh-pet>/dsh-pet/assets/webm <qtclient>/pet-frames
# 依赖: pip install imageio-ffmpeg pillow
# 产物: pet-frames/<动作名>/NNN.webp (RGBA 420x236) + manifest.json (UTF-8 无 BOM)
import glob
import json
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

import imageio_ffmpeg
from PIL import Image

W, H = 420, 236  # 「大」档窗口原生尺寸：大档解码后 1:1 直出（对齐 dsh-pet 的单次缩放），中/小档从高质量源缩小
RAW = W * H * 4


def probe_fps(ff, src):
    r = subprocess.run([ff, "-hide_banner", "-i", src, "-t", "0.05", "-f", "null", "-"],
                       capture_output=True)
    m = re.search(rb"([\d.]+) fps", r.stderr)
    return float(m.group(1)) if m else 24.0


def extract_one(job):
    ff, src, dstdir, name = job
    os.makedirs(dstdir, exist_ok=True)
    fps = probe_fps(ff, src)
    proc = subprocess.Popen(
        [ff, "-v", "error", "-c:v", "libvpx-vp9", "-i", src,
         "-vf", "scale=%d:%d:flags=lanczos" % (W, H),
         "-f", "rawvideo", "-pix_fmt", "rgba", "-"],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    files = []
    alpha_bad = None
    try:
        while True:
            raw = proc.stdout.read(RAW)
            if not raw:
                break
            if len(raw) < RAW:
                break  # 尾包不完整，丢弃
            i = len(files)
            img = Image.frombytes("RGBA", (W, H), raw)
            if i == 0:
                amin, amax = img.getchannel("A").getextrema()
                if not (amin < 100 and amax > 200):
                    alpha_bad = (amin, amax)  # 与 dsh-pet check_alpha.py 同判据
            fn = "%03d.webp" % i
            img.save(os.path.join(dstdir, fn), "WEBP", quality=88, method=5)
            files.append(name + "/" + fn)
    finally:
        proc.stdout.close()
        err = proc.stderr.read().decode("utf-8", "replace")
        proc.wait()
    if proc.returncode != 0:
        print("  [FFMPEG ERR] %s: %s" % (name.encode("unicode_escape").decode(), err[:200]))
    return name, fps, files, alpha_bad


def main():
    srcdir, dstdir = sys.argv[1], sys.argv[2]
    vids = sorted(glob.glob(os.path.join(srcdir, "*.webm")))
    if not vids:
        print("no webm found in", srcdir)
        return 2
    print("videos:", len(vids))
    ff = imageio_ffmpeg.get_ffmpeg_exe()
    jobs = [(ff, v, os.path.join(dstdir, os.path.splitext(os.path.basename(v))[0]),
             os.path.splitext(os.path.basename(v))[0]) for v in vids]
    manifest = {}
    bad = 0
    with ThreadPoolExecutor(max_workers=6) as ex:
        for name, fps, files, alpha_bad in ex.map(extract_one, jobs):
            manifest[name] = {"count": len(files), "fps": fps, "frames": files}
            if alpha_bad:
                bad += 1
                print("  [ALPHA LOST] %s extrema=%s" % (name.encode("unicode_escape").decode(), alpha_bad))
    with open(os.path.join(dstdir, "manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, ensure_ascii=False, indent=2)
    total = sum(m["count"] for m in manifest.values())
    print("done: %d actions, %d frames, alpha-lost=%d" % (len(manifest), total, bad))
    return 1 if bad else 0


if __name__ == "__main__":
    raise SystemExit(main())
