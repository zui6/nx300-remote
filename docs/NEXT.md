# 下一步 —— 取景器收尾

> 2026-11-22 14:50
> 相机当前状态：在线，daemon 正常（pid 373），mjpeg_proxy 手动运行中（pid 2200）

## 已完成

取景器做完并发布到 GitHub（`master` + 标签 `v0.5.0`）：
**网页三档画质，38 / 19 / 14 fps，daemon 零改动。**

完整说明见 [RESUME.md](RESUME.md) 和 [docs/mjpeg-viewer.md](repo/nx300-remote/docs/mjpeg-viewer.md)。

---

## H.264 那条路：已放弃（结论存档）

下面这些是今天上午的成果，技术上成立，但**整机不划算**，留档备查。

### 1. H.264 编码器可用（关键突破）

`savsenc_h264`（GStreamer 插件）+ `libs264.so` 本身没问题。之前失败是因为
`/proc/cpuinfo` 的 `Hardware: Samsung-DRIMeIV-NX300` 与库内平台表
`Samsung-DRIMeIV-ES` 不匹配 → `s264_init` 返回 -2000。

**修复**：把 `libs264.so` 里 `Samsung-DRIMeIV-ES` 等长替换为 `Samsung-DRIMeIV-NX`，
放在 `/mnt/mmc/tmpbin/`，用 `LD_LIBRARY_PATH` 覆盖。**不写系统分区。**

补丁文件：`bin/dlna/libs264-nx300.so`（本地）
运行环境：

```sh
export GST_REGISTRY=/tmp/gst2.reg
export GST_PLUGIN_PATH=/usr/lib/gstreamer-0.10
export LD_LIBRARY_PATH=/mnt/mmc/tmpbin
```

### 2. 编码器只吃 I420，不吃 SN12

送 SN12 会报 `Fail to create handle of s264 encoder:[-102]`。
取景是 NV12，送之前必须拆成 I420（UV 平面分离，开销小）。

### 3. 网络发送的 CPU 成本（实测）

推 10 MB 走 WiFi（2.24 MB/s）：相机 CPU `us 7% + sy 42% = 49%` 单核。

**所以原始 YUV 方案不可取**：1.84 MB/s 的裸流光网络就吃掉约 40% CPU。

### 4. 为什么最终放弃

- 需要 **3 个进程**（feed + gst-launch + h264serve），gst 一家十几 MB
- 跑起来 CPU 100%、空闲内存掉到 5 MB → **相机反复重启**
- 单进程方案要逆完 `s264e_create` 的参数结构体（已拿到字段顺序和调用点，
  见 `daemon-build/` 里的分析记录），但仍有风险

---

## 待办（按价值排序）

### 1. 让 `mjpeg_proxy` 开机自启 ★★★

现在相机每次重启都要手动拉。改 `autoexec.sh` 加一行即可，但那个文件和
另一个项目（LUT）共用，**动之前要确认**。

### 2. 定位重编 daemon 的 JPEG 挂死 ★★★

这是唯一的硬坎。解开后能顺带修两件事：

- daemon 只哈希顶部两行的帧检测（→ 去掉 23% 重复帧）
- 也许能把 JPEG 编码再优化

已排除的方向：

- 常驻 JPEG 压缩器（改回每帧 create/destroy 仍挂死）
- 新增的流式代码（整段删掉再编仍挂死）

线索：`daemon/` 源码与已知可用的 `bak3` 二进制之间有多个函数大小差异
（`main` 4364→3856、`ev_handler` 11348→11644、`liveview_get` 672→616 等），
说明差异不止一处，需要用二分法逐文件回退定位。

### 3. 手机端接入 ★★

现在是电脑网页。Android 直接用 WebView 打开同一个页面即可，
`viewer/mjpeg_view.html` 里的 `CAM` 常量改成相机的 IP。

### 4. 超清档的客户端解码 ★

同一路 800×480 流，Python 能读 24.9 fps，Chrome 只有 14。
瓶颈在客户端解码。可尝试：

- WebCodecs `ImageDecoder`（比 `<img>` 更快）
- 或直接在客户端做 2×2 抽点，只显示 400×240

---

## 上机注意事项（血泪教训）

1. **FTP 上传必须先 kill 目标进程再覆盖** —— 否则 `Text file busy`，上传静默失败
2. **后台进程用 `setsid ... < /dev/null &`** —— `nohup` 不够，telnet 一断就死
3. **相机重启后 uptime 归零** —— 代理需要重新拉起，脚本可以用 uptime 判断
4. 编译：`zig cc -target arm-linux-musleabihf -mcpu=cortex_a9 -O2 -static`
