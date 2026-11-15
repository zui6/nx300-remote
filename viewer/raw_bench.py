#!/usr/bin/env python3
"""
raw_bench.py -- 原始 NV12 通道测速 + 解码验证

从 /api/v1/liveview/raw 拉原始帧，本地做 NV12 -> RGB，统计帧率和带宽。
用法: python raw_bench.py [ip] [scale] [frames]
"""
import http.client
import sys
import time

import numpy as np
from PIL import Image


def nv12_to_rgb(buf, w, h):
    """NV12 -> RGB，纯 numpy 向量化（无循环）。"""
    y = np.frombuffer(buf, dtype=np.uint8, count=w * h).reshape(h, w).astype(np.float32)
    uv = np.frombuffer(buf, dtype=np.uint8, offset=w * h,
                       count=w * h // 2).reshape(h // 2, w // 2, 2).astype(np.float32)
    u = uv[:, :, 0]
    v = uv[:, :, 1]
    # 色度上采样到全分辨率
    u = np.repeat(np.repeat(u, 2, axis=0), 2, axis=1)
    v = np.repeat(np.repeat(v, 2, axis=0), 2, axis=1)
    # BT.601 limited range
    yy = (y - 16.0) * 1.164
    r = yy + 1.596 * (v - 128.0)
    g = yy - 0.813 * (v - 128.0) - 0.391 * (u - 128.0)
    b = yy + 2.018 * (u - 128.0)
    rgb = np.stack([r, g, b], axis=-1)
    return np.clip(rgb, 0, 255).astype(np.uint8)


def fetch(c, scale):
    c.request("GET", f"/api/v1/liveview/raw?scale={scale}")
    r = c.getresponse()
    w = int(r.getheader("X-NX-Width") or 0)
    h = int(r.getheader("X-NX-Height") or 0)
    body = r.read()
    return w, h, body


def main():
    ip = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.142"
    scale = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 30

    c = http.client.HTTPConnection(ip, 80, timeout=20)
    w, h, body = fetch(c, scale)
    if w == 0 or not body:
        print("raw 接口不可用（相机是否部署了 raw 版 daemon？）")
        return 1
    print(f"分辨率 {w}x{h}，首帧 {len(body):,} B")

    # 单帧解码验证
    t0 = time.time()
    rgb = nv12_to_rgb(body, w, h)
    dec_ms = (time.time() - t0) * 1000
    Image.fromarray(rgb).save("raw_frame.png")
    print(f"NV12->RGB 解码 {dec_ms:.1f} ms，已存 raw_frame.png")

    # 连续拉取测速
    t0 = time.time()
    total = 0
    for _ in range(n):
        w2, h2, b2 = fetch(c, scale)
        total += len(b2)
        nv12_to_rgb(b2, w2, h2)
    dt = time.time() - t0
    print(f"{n} 帧 / {dt:.2f}s = {n/dt:.1f} fps")
    print(f"带宽 {total/dt/1024/1024:.2f} MB/s，平均 {total//n:,} B/帧")
    print(f"（其中客户端解码约 {dec_ms:.1f} ms/帧）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
