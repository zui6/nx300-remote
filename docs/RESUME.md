# 重启后从这里继续（2026-11-22 14:50）

## 一句话现状

**取景器已经做完并发布**：网页版三档画质，38 / 19 / 14 fps，daemon 一个字节没改。
H.264 那条路**放弃**（三进程把相机压垮，反复重启）。

## 已完成并发布

GitHub：**https://github.com/zui6/nx300-remote** — `master` + 标签 `v0.5.0`

| 档位 | URL | 分辨率 | 浏览器实测 | 码率 |
|---|---|---|---|---|
| 流畅 | `http://192.168.1.142:8081/?scale=3` | 266×160 | **38 fps** | 184 KB/s |
| 高清 | `?scale=2&q=85` | 400×240 | **19 fps** | ~250 KB/s |
| 超清 | `?scale=1` | 800×480（原生）| **14 fps** | 581 KB/s |

架构：

```
浏览器 ──一条 multipart 长连接──> mjpeg_proxy :8081 ──每帧短连接──> daemon :80
```

改造前是每帧一次 HTTP 请求，266×160 只有 19.8 fps、400×240 只有 14.2 fps。
**瓶颈一直在 WiFi 往返（30 ms/次），不在编码。**

## 现在相机上的进程

| 进程 | PID | 说明 |
|---|---|---|
| `nx-remote-controller-daemon` | 373 | 主 daemon，**不能重编**（见下）|
| `mjpeg_proxy` | 2200 | 取景代理，手动启动 |

启动代理：

```sh
cd /mnt/mmc
setsid sh -c './mjpeg_proxy 8081 3 70 0' < /dev/null &
```

参数：`<端口> [默认scale] [默认q] [fps]`，`fps=0` = 不限速。

## ⚠️ 三条硬约束

1. **`daemon/` 里的源码编不出能用的 daemon**
   重编后 `params` 返回 200，但 `jpeg` 无响应直到超时。
   做过对照实验：把新加的代码全删掉再编，**照样挂死**，是源码树某个累计差异，未定位。
   → **现用二进制固定为 `nx-remote-controller-daemon.bak3`（2,456,868 字节）**，别覆盖。

2. **daemon 判断新帧只哈希顶部两行**
   `for (j = 0; j < frame_width*2; j++) hash += p[j];`
   画面下方在动、顶部没动时会返回旧缓冲 → 约 23% 重复帧。要修必须重编，被第 1 条卡住。

3. **`mjpeg_proxy` 不会开机自启**
   相机每次重启后要手动拉一次。可加进 `autoexec.sh`，但那个文件和别的项目共用，暂未动。

## H.264 为什么放弃（结论存档）

- 编码器本身可用：打过平台字符串补丁的 `libs264.so` + `savsenc_h264`，码流合法，ffmpeg 零错误解码
- 但需要 **3 个进程**（feed + gst-launch + h264serve），gst 一家十几 MB，
  CPU 100%、空闲内存掉到 5 MB → **相机反复重启**
- 单进程方案（直接链 `libs264` 调 `s264e_*`）理论上能省内存，但需要逆完参数结构体，且仍有风险
- **结论：实时 H.264 推流在这台机器上不划算**，JPEG + 长连接已经够用

## 本地源码位置

| 文件 | 说明 |
|---|---|
| `daemon-build/mjpeg_proxy.c` | 取景代理（已编译 `daemon-build/mjpeg_proxy`）|
| `viewer/mjpeg_view.html` | 网页取景器 |
| `daemon-build/feed.c` | H.264 方案的取景喂送器（已弃用，留档）|
| `daemon-build/h264serve.c` | H.264 分发服务器（已弃用，留档）|
| `docs/mjpeg-viewer.md` | 完整记录：架构、五个踩坑、帧率天花板分析 |

编译命令：

```powershell
D:\AndroidSdk\zig\zig-windows-x86_64-0.13.0\zig.exe cc `
  -target arm-linux-musleabihf -mcpu=cortex_a9 -O2 -static `
  -o <out> <src.c>
```

## 下一步（按价值排序）

1. **让代理开机自启**（改 `autoexec.sh`，需与另一个 agent 协调）
2. **定位重编 daemon 的 JPEG 挂死**（唯一的硬坎，解开后能修两件事：
   daemon 顶部两行检测、把 JPEG 编码再优化）
3. 手机端接入这个流（现在是电脑网页，Android 可直接用 WebView 打开同一页）

## 上机注意事项

- FTP 上传**必须先 kill 目标进程再覆盖**，否则 `Text file busy`，上传静默失败
- 启动后台进程用 `setsid ... < /dev/null &`，`nohup` 不够，telnet 一断就死
- 相机重启后 uptime 归零，`mjpeg_proxy` 需要重新拉起
