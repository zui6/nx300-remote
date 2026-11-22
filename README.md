# NX300 Remote

用 Android 手机远程控制三星 NX300 相机（Tizen 1.0，不用原厂 Smart Camera App）。

相机端跑一个静态链接的 C daemon（放在 SD 卡上，不碰固件），手机端是一个 20 KB 的原生 App。

## 功能

| 功能 | 实现 | 性能 |
|---|---|---|
| 实时取景 | 读 LCD 视频缓冲 → NV12 直接喂 libjpeg | 见 [取景器](#取景器mjpeg-长连接)：38 / 19 / 14 fps |
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

## 取景器（MJPEG 长连接）

daemon 只提供「每帧一次 HTTP 请求」的 JPEG 接口，瓶颈不在编码而在**WiFi 往返**：
每帧一次握手要 30 ms，20 fps 的 50 ms 里一大半浪费在这上面。

`tools/camera/mjpeg_proxy.c` 把它转成一条长连接推流：

```
浏览器 ──一条 multipart 长连接──> mjpeg_proxy :8081 ──每帧短连接──> daemon :80
```

**daemon 一个字节都不改**，纯外挂。三个档位由 URL 参数决定：

| 档位 | URL | 分辨率 | 浏览器实测 | 码率 |
|---|---|---|---|---|
| 流畅 | `http://相机:8081/?scale=3` | 266×160 | **38 fps** | 184 KB/s |
| 高清 | `http://相机:8081/?scale=2&q=85` | 400×240 | **19 fps** | ~250 KB/s |
| 超清 | `http://相机:8081/?scale=1` | 800×480 | **14 fps** | 581 KB/s |

超清档是相机**原生取景尺寸**，不是插值放大。

### 用法

```sh
# 相机上（ssh/telnet 进去）
cd /mnt/mmc
setsid sh -c './mjpeg_proxy 8081 3 70 0' < /dev/null &

# 电脑上打开网页
viewer/mjpeg_view.html
```

代理参数：`mjpeg_proxy <端口> [默认scale] [默认q] [fps]`，`fps=0` 表示不限速。

### 效果对比

改造前（每帧一次请求）→ 改造后：266×160 **19.8 → 24.9 fps**，400×240 **14.2 → 23.4 fps**。

细节、踩过的坑、帧率天花板分析见 [docs/mjpeg-viewer.md](docs/mjpeg-viewer.md)。

## 性能笔记

单核 Cortex-A9 @1.4GHz，相机自己的 `di-camera-app` 常态占用 100% CPU，
所以 daemon 做 JPEG 编码时只能分到约一半算力。

经 mjpeg_proxy 推流的实测（同一台相机、同一个 daemon）：

| 分辨率 | 质量 | 单帧 | 代理能推 | 浏览器渲染 |
|---|---|---|---|---|
| 266×160 (scale 3) | q70 | 3.5 KB | 38.8 fps | 38 fps |
| 400×240 (scale 2) | q85 | 13.9 KB | 22.1 fps | 19 fps |
| 800×480 (scale 1) | q70 | 23.3 KB | 24.9 fps | 14 fps |

> 同一路流用普通 socket 读取能到 24.9 fps，Chrome 只到 14 —— 超清档的瓶颈在
> **客户端解码**，不在相机、也不在网络。

四个关键优化：

1. **长连接代替每帧请求**：WiFi 每次往返 30 ms，这是最大的固定开销
2. **直接喂 NV12**：`jpeg_write_raw_data` + `raw_data_in`，零色彩转换
3. **平均降采样**：2×2 平均代替抽点，噪点被抹平，帧更小、编码更快
4. **浏览器只解最新帧**：解码跟不上时丢掉中间帧，避免延迟越积越大

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
viewer/           电脑端
  mjpeg_view.html 取景器网页（三档画质 + fps 显示）
tools/camera/
  mjpeg_proxy.c   MJPEG 推流代理（相机端跑，daemon 不改）
camera/           SD 卡上的启动脚本
  start.sh        带兜底：新 daemon 起不来就回退原版
  restore-orig.sh 一条命令返回原版
docs/
  nx300-params-reverse-engineering.md   参数逆向全过程
  mjpeg-viewer.md                       取景器架构、踩坑、帧率天花板
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

## 已知问题

| 问题 | 影响 | 状态 |
|---|---|---|
| `daemon/` 里的源码**编不出能用的 daemon** | 重编后 `params` 正常但 `jpeg` 无响应直到超时；对照实验（去掉全部新增代码）仍复现，未定位 | 所以现用二进制固定为 `nx-remote-controller-daemon.bak3` |
| daemon 判断「有没有新帧」只哈希**顶部两行** | 画面下方在动、顶部没动时返回旧缓冲，产生约 23% 重复帧 | 需改 daemon 并重编，被上一条卡住 |
| `mjpeg_proxy` **不会开机自启** | 相机每次重启后要手动拉一次 | 可加进 `autoexec.sh`（该文件与另一个项目共用，暂未改动） |

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
