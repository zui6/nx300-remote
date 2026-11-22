# MJPEG 取景器（2026-11-22）

## 成果

| 档位 | 分辨率 | 帧率 | 码率 |
|---|---|---|---|
| 流畅 | 266×160 | **22–25 fps** | ~100 KB/s |
| 高清 | 400×240 | **25 fps** | ~147 KB/s |

对比改造前（daemon 每帧一次 HTTP 请求）：

| 档位 | 改造前 | 现在 | 提升 |
|---|---|---|---|
| 266×160 | 19.8 fps | 24.9 fps | +26 % |
| 400×240 | 14.2 fps | 23.4 fps | **+65 %** |

## 架构

```
浏览器 ──一条长连接(multipart/x-mixed-replace)──> mjpeg_proxy(:8081)
                                                     │
                                                     └─每帧一条短连接(loopback)→ daemon(:80)
                                                        GET /api/v1/liveview/jpeg?q=&scale=
```

**daemon 一个字节都没改。** 这是关键：任何从当前源码树重编的 daemon，JPEG 请求都会挂死
（连不含新功能的对照版也挂），所以只做外挂代理。

## 为什么是「外长内短」

- **浏览器侧长连接**：瓶颈就在这里。WiFi 每次往返约 30 ms，每帧一次请求
  等于 30 ms 的固定开销，20 fps 的 50 ms 里一大半浪费在握手上。
- **daemon 侧短连接**：loopback 建连约 0.2 ms。早先试过 keep-alive 复用，
  两个客户端同时拉时会在单线程 daemon 上互相饿死，得不偿失。

## 画质档位

单进程，从 URL 参数读：

```
http://192.168.1.142:8081/?scale=3     # 266x160
http://192.168.1.142:8081/?scale=2     # 400x240
http://192.168.1.142:8081/?scale=1&q=60  # 全尺寸，慎用
```

## 踩过的坑（都记下来免得再犯）

| 现象 | 原因 | 修法 |
|---|---|---|
| 客户端连上就崩 | `send()` 向已关闭 socket 写入触发 **SIGPIPE**，默认杀死进程 | `signal(SIGPIPE, SIG_IGN)` |
| 全部请求 `code=-4` | 首次 `recv` 把 1023 字节缓冲填满（头+body），我加的「缓冲区满」判断误判成失败 | 改用「是否找到 `\r\n\r\n`」标志 |
| 刷新页面后新连接一直排队 | 旧连接残留，代理只服务一个 client | `select` 非阻塞检查 accept，新连接优先 |
| Chrome 下帧率腰斩 | Chrome 会发**空闲预连接**，被「新连接优先」逻辑误判，把正在用的流踢掉，反复抖动 | 新连接必须**真的发出请求**才替换（120 ms 超时判定） |
| 两个 proxy 时有一个卡死 | 共用单线程 daemon，keep-alive 连接互相饿死 | 只跑一个 proxy，画质走 URL 参数 |

## 内存

- 代理进程：约 1.5 MB（含 128 KB 帧缓冲 + 64 KB 发送缓冲）
- 相机空闲内存常态 5–9 MB

## 目前状态

- 代理是**手动启动**的（`setsid ./mjpeg_proxy 8081 3 70 25`），**相机重启后不会自动起**
- 没有改 `autoexec.sh`（那是和 LUT agent 共用的，不动）
- 前台页面：`viewer/mjpeg_view.html`，双击打开即可

## 下一步（可选）

1. 若确认稳定，把代理加进 autoexec 的启动段（需与 LUT agent 协调）
2. 支持在页面上直接调 JPEG 质量（现在质量是 URL 参数）
3. 之前分析过的 `nx300-remote-app/index.html`（canvas 渲染 NV12 的那个界面）
   也可以接这个流，但它是按帧请求的，需要改成流式读取

## 帧率天花板（2026-11-22 追加）

实测：代理**不限速**时推送 38.7 fps，但其中**不同画面只有 27.8 fps**（22% 重复）。

原因在 daemon 的帧检测：

```c
for (j = 0; j < *frame_width * 2; j++)   /* 只看前 1600 字节 = 最上面两行 */
    hash += p[j];
```

只哈希顶部两行 → 画面下方在动、顶部没动时会被判为「没有新帧」，返回旧缓冲。
所以 27.8 是**检测漏判的下界**，真实刷新率≥此值。

同一个 bug 早先在 `feed.c` 里修过（改成整帧跨采样后 13 fps → 20 fps）。
要真正突破 28 fps，必须改 daemon 里这段检测并重编 —— 而重编出来的 daemon
JPEG 请求会挂死（见下），这是目前唯一的硬坎。

## 未解问题：重编的 daemon JPEG 挂死

- 已知可用的二进制：`nx-remote-controller-daemon.bak3`（2,456,868 B）
- 用当前 `daemon/` 源码重编（`build-neon.ps1`）得到的二进制，启动正常、
  `/api/v1/params` 返回 200，但 `/api/v1/liveview/jpeg` **无响应直到超时**
- 做过对照：把新增的流式代码整段去掉再编，**依然挂死** → 不是新增功能引起，
  而是源码树相对 bak3 的某个累计差异
- 已排除：常驻 JPEG 压缩器（改回每帧 create/destroy 后仍挂死）

**因此当前方案刻意不改 daemon，只做外挂代理。**

## 开机自启（2026-11-22 追加）

`camera/autoexec.sh` 里新增第 3 段，在 `exit 0` 之前：

```sh
if [ -x /mnt/mmc/mjpeg_proxy ] && [ ! -e /mnt/mmc/mjpeg_proxy.DISABLED ]; then
    (
        sleep 4
        cd /mnt/mmc
        exec ./mjpeg_proxy 8081 3 70 0 80
    ) >/tmp/mjpeg_proxy.log 2>&1 &
fi
```

设计考虑：

- **只追加，不动前两段**（nx-remote-controller 和 nx300m-mod 的启动逻辑原样保留）
- `-x` 判断 + `DISABLED` 标志文件，想临时关掉只需 `touch /mnt/mmc/mjpeg_proxy.DISABLED`
- 整个块放进后台子 shell，代理起不来也不影响开机
- `sleep 4` 等 daemon 完成 mmap

验证（重启后）：

```
uptime=97s
daemon=370  proxy=594
mjpeg_proxy: :8081 -> daemon:80 scale=3 q=70 fps=0
:8081  166 帧 = 33.1 fps, 140.1 KB/s
params=200  jpeg=200 4287B      # 原接口未受影响
```

### 踩坑：FAT 上 `mv` 换文件可能不落盘

第一次是用 `mv -f autoexec.sh.new autoexec.sh` 安装的，命令内 `sync` 过，
回读也是新内容，但**重启后文件又变回旧版**（时间戳还是原来的）。

改成 `cat autoexec.new.sh > autoexec.sh` 再 `sync` 两次，才真正持久化。

在 FAT 分区上改开机脚本，**务必回读 + 重启确认**，别信 `ls` 的即时结果。

回滚：`cp -f /mnt/mmc/autoexec.sh.bak-mjpeg /mnt/mmc/autoexec.sh`
