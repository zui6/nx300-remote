# NX300 Remote

用 Android 手机远程控制三星 NX300 相机（Tizen 1.0，不用原厂 Smart Camera App）。

相机端跑一个静态链接的 C daemon（放在 SD 卡上，不碰固件），手机端是一个 20 KB 的原生 App。

## 功能

| 功能 | 实现 | 性能 |
|---|---|---|
| 实时取景 | 读 LCD 视频缓冲 → NV12 直接喂 libjpeg | 400×240 ≈ 30 fps（轻负载） |
| 读取拍摄参数 | `process_vm_readv` 读相机 App 内存 | ISO / 快门 / 光圈，延迟 ~34 ms |
| 按键注入 | `st key` | 菜单、方向键、快门、半按 |
| 快门 | `st key push/click/release s1` | 支持常按对焦 |
| 相机界面截图 | `xwd` + 旋转 + JPEG | 800×480 ≈ 2 fps（内存开销大，默认关闭） |
| 画面 + UI 叠加 | UI 图层缓存 + YUV 混合 | ≈ 20 fps（按键后刷新 UI） |

## 架构

```
┌─────────────┐   HTTP/1.1 keep-alive    ┌──────────────────┐
│ Android App │ ───────────────────────► │ daemon (相机内)   │
│  (Java)     │ ◄─────────────────────── │ 静态 ARMv7 二进制  │
└─────────────┘      JPEG / JSON         └──────────────────┘
                                                  │
                                    ┌─────────────┼─────────────┐
                                    ▼             ▼             ▼
                              /dev/mem      di-camera-app    st key
                            (视频缓冲)      (内存读参数)    (按键注入)
```

## 关键接口

```
GET /api/v1/liveview/jpeg?q=70&scale=2    # 传感器画面
GET /api/v1/params                        # {"iso":3200,"shutter_num":1,...}
GET /api/v1/camera/info                   # 机型 / 固件 / MAC
GET /api/v1/camera/status                 # 电量 / 模式
GET /api/v1/input/inject?key=Menu         # 按键
GET /api/v1/shutter/normal                # 快门
GET /api/v1/lcd/on|off                    # 屏幕
```

## 性能笔记

单核 Cortex-A9 @1.4GHz，相机自己的 `di-camera-app` 常态占用 100% CPU，
所以 daemon 做 JPEG 编码时只能分到约一半算力。实测（ISO 200 室内场景）：

| 分辨率 | 质量 | 帧大小 | 每帧 | fps |
|---|---|---|---|---|
| 267×160 (scale 3) | q70 | 7.2 KB | 61 ms | 16 |
| 400×240 (scale 2) | q70 | 12.4 KB | 91 ms | 11 |
| 800×480 (scale 1) | q85 | 70 KB | 224 ms | 4.5 |

三个关键优化：

1. **复用 TCP 连接**：每帧新建连接要 70 ms，复用后 5 ms
2. **直接喂 NV12**：`jpeg_write_raw_data` + `raw_data_in`，零色彩转换
3. **平均降采样**：2×2 平均代替抽点，噪点被抹平，帧更小、编码更快

## 结构

```
daemon/           相机端 daemon 的增加/改动
  jpeg_nv12.c     NV12 → JPEG（直通，无色彩转换）
  params.c        读 di-camera-app 内存拿曝光参数
  screen_jpeg.c   抓屏（可选）
  ui_overlay.c    UI 图层叠加（可选）
  memdump.c       进程内存导出工具（逆向用）
app/              手机端
  MainActivity.java
  build-apk.ps1   不用 Gradle，手工 aapt2/d8/zipalign/apksigner
camera/           SD 卡上的启动脚本
  start.sh        带兜底：新 daemon 起不来就回退原版
  restore-orig.sh 一条命令返回原版
docs/
  nx300-params-reverse-engineering.md   参数逆向全过程
```

## 构建

### daemon（Linux 交叉编译，静态）

```sh
zig cc -target arm-linux-musleabi -mcpu=cortex_a9 -static -O2 \
  -fno-PIE -no-pie -I externals/mongoose -I externals/libjpeg -I . \
  -o nx-remote-controller-daemon \
  api_server.c command.c input.c lcd.c led.c liveview.c main.c \
  externals/mongoose/mongoose.c network.c nx_model.c osd.c shutter.c \
  status.c util.c gallery.c jpeg_nv12.c screen_jpeg.c ui_overlay.c params.c \
  <libjpeg objects>
```

libjpeg-turbo 3.0.4 需要手动配置 `jconfig.h`（`WITH_SIMD 0`）并补齐
`jsimd_stub.c` / `jinit_stub.c` 等桩文件；NEON 版本见 `docs/`。

### App

```powershell
powershell -File app/build-apk.ps1
```

需要 JDK 17 + Android SDK build-tools 34（`aapt2` / `d8` / `zipalign` / `apksigner`）。

## 安全

**只改 SD 卡，不动固件。** 所有文件都在 `/mnt/mmc/remote/` 下：

- 原版 daemon 备份为 `nx-remote-controller-daemon.orig`
- `restore-orig.sh` 可一键回退
- `start.sh` 带启动兜底
- 拔卡开机 = 完全原厂行为，不存在变砖风险

## 逆向记录

相机不提供任何读取 ISO / 快门 / 白平衡的接口（`st`、`prefman`、dbus 都不行），
所以直接读 `di-camera-app` 的进程内存。方法、地址、验证数据见
[docs/nx300-params-reverse-engineering.md](docs/nx300-params-reverse-engineering.md)。

## License

MIT
