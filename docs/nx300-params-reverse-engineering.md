# NX300 参数读取（逆向记录）

相机：NX300，固件 1.45，Tizen 1.0，内核 3.5.0+
目标进程：`di-camera-app-n`（用户态相机 App，PID 每次开机不同，进程名固定）

## 为什么不能从寄存器读

寄存器里存的是硬件原始值（曝光行数、模拟增益码、镜头档位码），
而"ISO 800 / 1/8 / F8.0" 是 di-camera-app 用映射表算出来的**显示值**。
DRIMeIV 的寄存器无公开文档，`/dev/d4_pp_3a` 是私有 ioctl，乱读可能挂总线。

**结论：只能从进程内存里读。**

## 方法

用 `process_vm_readv`（内核 3.2+）直接读目标进程内存：

- **不需要 ptrace attach**，目标进程完全不停顿
- 只读，失败返回 EPERM/EFAULT，不会伤害目标
- 相机全程无感，实测连续 180 次无异常

对应实现见 `params.c` / `memdump.c`。

## 已验证的地址（NX300 fw 1.45）

| 地址 | 类型 | 含义 | 实测变化 |
|---|---|---|---|
| `0x5c3890` | u32 | 当前 ISO | 800 → 3200 ✓ |
| `0x5c3894` | u32 | ISO 上限（自动 ISO 封顶） | 3200 → 3200 |
| `0x4df5dc` | u16 | 快门分母 | 8 → 500 ✓ |
| `0x4df5de` | u16 | 快门分子 | 1 → 1 |
| `0x4df5e2` | u16 | 光圈 ×10 | 80 → 56（F8.0 → F5.6）✓ |
| `0x4df5b8` | u32 | Tv ×16（log2(快门秒)×16） | 48 → 144 ✓ |
| `0x4df5e4` | u32 | Av ×16（log2(f²)×16） | 96 → 80 ✓ |

推导验证：
- 1/8s → Tv = log2(8) = 3 → 3×16 = **48** ✓
- 1/500s → Tv = log2(500) ≈ 9 → 9×16 = **144** ✓
- f/8 → Av = log2(64) = 6 → 6×16 = **96** ✓
- f/5.6 → Av ≈ 5 → 5×16 = **80** ✓

## 定位方法（可复现）

1. `memdump <pid> dump.bin` 导出进程可读段（约 44MB）
2. 在相机上改**一个**参数
3. 再导出一份
4. 对比两份额：找"同一地址上值从 A 变成 B"的位置

关键点：
- 全内存只有 **1 处** 从 800 变成 3200 → 直接锁定 ISO
- 快门靠 `u16[den, num]` 有理数形式识别
- 光圈靠 80 → 56 这种"×10"编码识别

## 稳定性

- 地址在**重启后不变**（已实测：断电重启后 ptest 仍读出正确值）
- 固件升级后可能失效 → `params_read()` 带合理性校验，
  对不上就返回不可用，不会输出垃圾

## HTTP 接口

```
GET /api/v1/params
→ {"iso":3200,"iso_max":3200,"shutter_num":1,"shutter_den":500,
   "aperture10":56,"tv16":144,"av16":80}
```

- 延迟中位 34ms（主要是 TCP 建连）
- 连续 180 次：180/180 成功
- daemon 内存 512KB

## 尚未拿到

- **白平衡**：需要再改一次 WB 做对比实验
- 测光模式、驱动模式、对焦模式：同上，需逐个改+对比

## 工具

| 文件 | 用途 |
|---|---|
| `memdump.c` | 导出目标进程内存（`memdump <pid> <out> [max_mb]`） |
| `params.c` | 读取并校验曝光参数 |
| `ptest.c` | 独立验证工具，直接打印当前参数 |

## 设置结构（0x4df600 区域）

在曝光结构之后紧跟着一块"相机设置"结构，字段是连续的 u32 枚举：

| 地址 | 含义 | 观测值 |
|---|---|---|
| `0x4df650` | 白平衡模式 | 0=自动, 1=日光, 3=荧光灯 |
| `0x4df654` | 色温 | 6000 K |
| `0x4df660` | 增益（×65536） | 179200 → 2.734 |
| `0x4df668` | 增益（×65536） | 115200 → 1.758 |
| `0x4df720` | 测光模式 | 1=点测光, 2=中央重点 |

镜像位置（同一份数据的第二份拷贝，可用于交叉校验）：

| 主地址 | 镜像地址 |
|---|---|
| `0x4df650` (WB) | `0x2b9b44` |
| `0x4df720` (测光) | `0x2b9ab8` / `0x4e00ac` |

## 白平衡增益（浮点）

WB 切换会改变一组 float32 增益：

```
0x518c14   自动=1.525  日光=1.750  荧光=1.875
0x518c1c   自动=3.512  日光=3.622  荧光=3.777
0x6111a4   自动=1.750  日光=1.650  荧光=1.750
```

## 实验方法

1. `memdump <pid> out.bin 64 200000 600000`（只导出 4MB 相关段）
2. 在相机上改**一个**设置
3. 再导出一份，对比：
   - 枚举类：找"同一地址小整数变化"（<64）
   - 增益类：找"0.4~6.0 的浮点变化"
4. 改两次验证：连续两次切换都在**同一地址**变化，才算锁定

注意：改设置会触发测光/直方图重算，产生大量噪声，
所以必须用"两次切换取交集"或"限定设置块范围"来过滤。
## 静态分析（提取 di-camera-app 二进制）

`/usr/bin/di-camera-app-nx300`（3.2MB，非 PIE，ARM EABI5）可只读复制到 SD 卡分析。

关键发现：

| 项目 | 内容 |
|---|---|
| 文件头 | ELF32，`e_type=EXEC`（地址固定，与内存一致） |
| 导出符号 | 5553 个（`.dynsym`），含 `UI_Set_Attr_*`、`set_pb_exif_metering` 等真实名字 |
| 字段表 | `0x156c38` 起 203 项指针，指向 `.bss` 里 `0x4df5c0`–`0x4df900` 的设置字段 |
| 访问方式 | 代码按**索引**间接访问字段，指令里搜不到字段地址（movw/movt 命中 0 次） |
| 名字表 | **不存在**，所以字段语义拿不到，只能靠实验 |

静态分析能给的：**字段总清单 + 数量（203）+ 已知偏移**。
静态分析不能给的：**哪个字段叫什么**。

## /api/v1/fields

一次性读出全部 203 个字段（含已确认名字）：

```json
{"pid":265,"count":203,"items":[
  {"i":5,"a":5109212,"v":655370,"n":"shutter"},
  {"i":26,"a":5109328,"v":3,"n":"wb"},
  ...]}
```

实现见 `daemon/fields.c`：先读 `0x156c38` 的指针表，再逐个 `process_vm_readv`。

## 已确认字段索引

| 索引 | 地址 | 名字 | 值 |
|---|---|---|---|
| 5 | 0x4df5dc | shutter | 655370 (den/num) |
| 7 | 0x4df5e4 | av16 | 144 |
| 26 | 0x4df650 | wb | 3=荧光灯 |
| 27 | 0x4df654 | kelvin | 6000 |
| 55 | 0x4df6cc | drive | 0=单张 |
| 76 | 0x4df720 | metering | 2=中央重点 |
## AF 区域（实验确认）

```
0x4df68c (索引 40)   1 = 多区   0 = 选择AF
0x4df7a4 (索引 116)  1 = 处于"选择AF"模式（跟随开关）
```

## UI 层逆向尝试

符号表里有完整的绘制函数：

```
CAPPGUIHdmiPlaybackState::DrawEXIFAv     0xe54d0
CAPPGUIHdmiPlaybackState::DrawEXIFTv     0xe55e4
CAPPGUIHdmiPlaybackState::DrawEXIFIso    0xe56dc
CAPPGUIHdmiPlaybackState::DrawEXIFMetering 0xe5830
CAPPGUIHdmiPlaybackState::DrawEXIFWB     0xe5bc4
CAPPGUIHdmiPlaybackState::DrawEXIFEV     0xe5da0
CAPPGUIHdmiPlaybackState::DrawEXIFPW     0xe5fe0   (照片风格)
CAPPGUIHdmiPlaybackState::DrawEXIFPhotoSize 0xe5ffc
CApex::ChangeTv / ChangeAv / ConvertTvToRational ...
```

但这些是**回放界面**（读图片 EXIF），不读实时设置。
实时 OSD 走的是同一套"控制信息注册表"（`UI_Initialize_Control_Info`，
里面引用字段地址 69 次），仍然是按索引访问。

结论：UI 层没有"直接读字段"的代码，字段语义只能靠实验确认。
好消息是实验成本已降到一次设置改动 + 两次 HTTP 请求。
## AF 区域（实验确认）

```
0x4df68c (索引 40)   1 = 多区   0 = 选择AF
0x4df7a4 (索引 116)  1 = 处于"选择AF"模式
```

## 工具链现状

| 工具 | 状态 |
|---|---|
| REA + Ghidra | ❌ 在这台 Windows 上调用必超时（没有 java 进程启动） |
| REA + Rizin | ✅ 可用，能解析符号、反汇编、查引用 |
| Python + capstone | ✅ 可用（自装到 D 盘） |

Rizin 里必要的初始化：

```
e asm.bits=32      # 默认识别成 16 位，必须手动设 32
e scr.color=0
```

## 字段访问器（0x1565c8 起）

找到唯一一处读字段表的代码，是一个巨型 switch：

```
0x1565c8  movw r3, 0xf5bc / movt r3, 0x4d     -> 0x4df5bc
0x1565cc  ldr  r3, [0x156c38]                 -> 0x4df5c0
0x1565d4  ldr  r3, [0x156c3c]                 -> 0x4df5c4
...
（每个 case 结束都 b 0x156c28，跳表在 0x156200-0x1565c0）
```

即：`GetSettingPtr(index)` 通过索引返回字段地址。
所有设置读写都走它，所以字段地址不会以立即数出现在调用方代码里——
这就是为什么静态搜字段地址几乎搜不到。

要拿到"索引 → 语义"，只能看**调用方传入的常量索引**，
而调用方本身也是按索引传参的通用逻辑，所以仍然需要实验兜底。
## Ghidra 逆向（最终突破）

REA 自带的 Ghidra 在这台机器上不可用（调用超时、进程根本不启动），
改为**自己装 Ghidra 12.2 到 D 盘跑 headless**：

```
D:\tools\ghidra\ghidra_12.2_PUBLIC\support\analyzeHeadless.bat
  <proj> nx300 -import di-camera-app-nx300 \
  -postScript DumpAccessor.java <out> -scriptPath <scripts>
```

3.2MB ARM 二进制，自动分析约 **147 秒**。

### 拿到的关键结构

```
UI_Get_Value_Pointer_Capture_Menu(eUI_VALUE_ITEM)   @ 0x15623c
  case N -> DAT_00156cXX  (字段指针表)
UI_Get_Value(id)  @ 0x168618
UI_Set_Value(id, v) @ 0x168758
  -> UI_Set_DB_Value_Capture_Menu(db_id, v)   // 同时写 prefman DB
```

把「调用点常量索引」和「case → 字段地址」join 起来，
就得到 **字段语义**——这是实验法几十次都换不来的。

### 结果

- 5395 个调用点，442 个不同索引
- **118 个字段**有明确语义线索 → `docs/nx300-field-map.md`
- 已内置到 daemon 命名的：**26 个**

### 验证（与实验完全吻合）

| 参数 | 地址 | case | 实验值 | 反编译函数 |
|---|---|---|---|---|
| 白平衡 | 0x4df650 | 0x1c | 3=荧光 | set_white_balance |
| 驱动 | 0x4df6cc | 0x37 | 1=高速连拍 | UI_Check_Single_Drive |
| 测光 | 0x4df720 | 0x4d | 2=中央重点 | set_metering_spot_target |
| AF 区域 | 0x4df68c | 0x2b | 1=多区 | set_af_area |

### 新发现的参数

| 参数 | 地址 | 函数 |
|---|---|---|
| EV 补偿 | 0x4df5c8 | set_ev |
| 闪光灯 | 0x4df5f0 | set_flash |
| ISO 索引 | 0x4df648 | set_iso |
| ISO 步进 | 0x4df72c | UpdateDiskIsoStep |
| 白平衡值 | 0x4df658 | set_white_balance |
| 照片风格 | 0x4df678-684 | set_gg_picture_wizard |
| MF 辅助 | 0x4df6b8 | set_mf_foucs_aid |
## 界面名称（中文）

相机的界面文案在 **gettext 翻译包**里：

```
/usr/share/locale/zh_CN/LC_MESSAGES/di-camera-app-nx300.mo   (198KB, 2723 条)
```

只读复制到 SD 卡即可导出（不修改系统）。

解析后得到完整对照：

| 参数 | msgid | 中文名 |
|---|---|---|
| ISO | CAP_FN_ISO | ISO |
| 白平衡 | CAP_FN_WB | 白平衡 |
| 测光 | CAP_FN_METERING | 测光 |
| 拍摄模式 | CAP_FN_DRIVE | 拍摄模式 |
| AF 区 | CAP_FN_AFAREA | AF区 |
| AF 模式 | CAP_FN_AFMODE | AF模式 |
| EV | CAP_FN_EV | EV |
| 画质 | CAP_FN_QUALITY | 画质 |
| 照片尺寸 | CAP_FN_PHOTO_SIZE | 照片尺寸 |
| 图片向导 | CAP_FN_PW | 图片向导 |
| 防抖 | CAP_FN_OIS | OIS（防抖） |

### 选项名与枚举

白平衡的选项顺序来自 `.rodata` 0x2467a0 的指针表（顺序即枚举值）：

```
0 自动白平衡   1 日光      2 阴天       3 荧光灯_W   4 荧光灯_N
5 荧光灯_D     6 白炽灯    7 闪光灯     8 自定义     9 色温
```

其余（实验确认）：

```
测光    0=多重测光  1=点测光  2=中央重点
AF区    0=选择AF    1=多重AF  (2=脸部侦测AF 推测)
驱动    0=单张      1=高速连拍  5=AE BKT
```

### 接口

`GET /api/v1/params` 直接返回中文名：

```json
{"iso":200,"shutter_num":50,"shutter_den":10,"aperture10":220,
 "wb":3,"wb_name":"荧光灯_W",
 "metering":0,"metering_name":"多重测光",
 "af_area":1,"af_area_name":"多重AF",
 "drive":0,"drive_name":"单张"}
```
## 完整枚举表（反编译各 set_xxx 确认）

每个 `set_xxx(int)` 都是"按值取图标"的数组，**数组下标就是枚举值**。

### AF 区 `set_af_area`
| 值 | 名称 | 图标 |
|---|---|---|
| 0 | 选择AF | focus_selection.png |
| 1 | 多重AF | focus_multi.png |
| 2 | 脸部侦测AF | focus_fd.png |
| 3 | 自拍AF | focus_sfd.png |

（注意：跟踪AF 不在这个菜单里，属于另一个功能）

### 测光 `set_metering`
| 值 | 名称 |
|---|---|
| 0 | 多重测光 |
| 1 | 点测光 |
| 2 | 中央重点 |

### 驱动 `set_drive`
| 值 | 名称 | 图标 |
|---|---|---|
| 0 | 单张 | drive_single |
| 1 | 高速连拍 | continuous_7fps |
| 2 | 连拍 | continuous_5fps |
| 3 | 高速拍摄 | burst |
| 4 | 定时器 | timer |
| 5 | AE BKT | aeb |
| 6 | WB BKT | wbb |
| 7 | 图片向导 BKT | pwb |

### 画质 `set_quality`
| 值 | 名称 |
|---|---|
| 0 | 超精细 |
| 1 | 精细 |
| 2 | 标准 |
| 3 | RAW |
| 4 | RAW+超精细 |
| 5 | RAW+精细 |
| 6 | RAW+标准 |

### 白平衡 `set_white_balance`（图标数组，与 .rodata 0x2467a0 一致）
| 值 | 名称 | 值 | 名称 |
|---|---|---|---|
| 0 | 自动白平衡 | 5 | 荧光灯_D |
| 1 | 日光 | 6 | 白炽灯 |
| 2 | 阴天 | 7 | 闪光灯 |
| 3 | 荧光灯_W | 8 | 自定义 |
| 4 | 荧光灯_N | 9 | 色温 |

### ISO `set_iso`
字段 0x4df648（整档）：0=AUTO, 1=100, 2=200, 3=400, 4=800, 5=1600, 6=3200, 7=6400, 8=12800, 9=25600
字段 0x4df64c（1/3 档）：0=AUTO, 1=100, 2=125, 3=160, 4=200, 5=250, 6=320, 7=400 …

### 图片向导 `set_gg_picture_wizard`
字段 0x4df678：-1 = 关；否则 模式号×17（17 是 slider 步长）
0x4df67c/680/684 是三个参数（0..8，滑杆）

## 最终接口输出

```json
{"iso":200,"shutter_num":50,"shutter_den":10,"aperture10":220,
 "wb":3,"wb_name":"荧光灯_W",
 "metering":0,"metering_name":"多重测光",
 "af_area":2,"af_area_name":"脸部侦测AF",
 "drive":0,"drive_name":"单张",
 "quality":4,"quality_name":"RAW+超精细",
 "pw":-1,"pw_name":"关"}
```
### 图片向导（修正）

模式字段是 **0x4df674**（不是 0x4df678）：

```
0=关  1=标准  2=鲜明  3=人像  4=风景  5=悠远  6=怀旧
7=古典  8=清爽  9=宁静  10=用户1  11=用户2  12=用户3
```

实验：人像=3，风景=4，关=0。
`0x4df67c/680/684` 是当前模式的三个子参数（0..8），切换模式时会随之变化。
## AF 模式 / 闪光灯 / 跟踪 AF（实验+反编译确认）

### AF 模式 0x4df688 `set_af_mode`
```
0=单一 AF   1=连续 AF   2=手动对焦
```
（图标数组 01_icon_indi_af_single/continuous/manual）

### 闪光灯 0x4df718 `set_flash`
图标表在 0x224654，16 项 = 8 模式 × 2（普通/红眼变体）：

```
0=关           1=自动              2=自动消减红眼      3=强制闪光
4=强制闪光+红眼消除   5=前帘同步     6=后帘同步          7=智能闪光
```

M 档只开放 3/4/5/6（相机屏幕实测确认）。

### 触摸 AF 0x4df690（菜单 5 项，顺序即枚举值）

```
0=关  1=触摸 AF  2=AF 点  3=跟踪 AF  4=一触拍摄
```

相机菜单实测（屏幕截图确认）：关 / 触摸 AF / AF 点 / 跟踪 AF / 一触拍摄。
Ghidra 佐证：`UI_Check_Tracking_Win_Enable()` 里只有 `UI_Get_Value(0x31)==3` 才启用跟踪窗。

### 尚未定位
- 触摸 AF（开/关）
- AF 点（中心/用户自选）—— 坐标在 0x4df694/0x4df698
## 触摸对焦（点屏幕设置对焦点）

### 原版实现

原版 web UI 的 `web_root/js/mouseinput.js` 里就是全部逻辑：

```js
MouseInput.prototype.onMouseMove = function (x, y) {
    if (this.controller.isNx300()) {
        var temp = y;
        y = x;
        x = 480 - temp;      // click_x = 480 - canvas_y
    }                        // click_y = canvas_x
    // -> /api/v1/input/inject?mousemove=<x>-<y>
}
```

画布是 800x480（横屏取景），X 屏幕是 480x800（竖屏），
所以只要做一次 90° 旋转即可，相机自己会把点击位置映射到对焦框。

### 实现

daemon 新增接口：

```
GET /api/v1/input/focus?x=<0..799>&y=<0..479>
```

内部换算 `click = (480 - y, x)`，然后向注入器依次发：

```
mousemove <cx> <cy>
mousedown 1
mouseup 1
```

（中间各留 120ms / 80ms，相机 UI 需要时间处理）

### 验证（实机）

| 画布坐标 | 对焦框坐标 |
|---|---|
| (400,240) | (350,186) |
| (600,120) | (530,78) |
| (200,360) | (134,294) |
| (500,300) | (458,258) |

对焦框坐标有量化（相机自己的对焦网格），误差在几十像素内。

### 输入注入链路

```
daemon --pipe--> nx-input-injector (libxdo) --> X server --> 相机 UI
```

注入器支持：`mousemove x y` / `mousedown N` / `mouseup N` / `key ...` / `keydown` / `keyup`
## 取景优化与电脑端预览

### NEON

libjpeg 用 `-mfpu=neon -mfloat-abi=softfp` 重编（需给 `jmorecfg.h`/`jpegint.h`/`jpeglib.h`
等加 include guard，原版头文件没有），链接 `build/simdobj` 里的 NEON 实现
（排除 `jsimd_stub.o` 和 `jsimd_neon.o`，后者与 C 实现重复定义）。

效果（PC 侧 keep-alive 实测）：

| 档位 | 优化前 | 优化后 |
|---|---|---|
| 流畅 (scale3) | 21.7 fps | 20.1 fps |
| 标准 (scale2) | 11.7 fps | 13.7 fps |
| 清晰 (scale1) | 8.5 fps | 10.9 fps |

### OSD 叠加（按需）

`GET /api/v1/osd/snap` —— 抓一张 800x480 屏幕 JPEG（含按钮/参数条），耗时约 2s。
只应在客户端显式请求时调用，**不能每帧抓**（会卡死）。

预览器按 O 键抓一次并缓存，用 `ImageChops.lighter` 叠在取景图上，
这样就能看到 MENU / Fn 等按钮的位置，避免误点。

### 电脑端预览器

`viewer/nx300_view.py`：显示实时取景、帧率、往返延迟；点击画面设对焦点。

按键：1/2/3 切画质、O 显示/隐藏相机界面、空格暂停、ESC 退出
## 对焦框到底画在哪（结论：拿不到）

用显示控制器寄存器定位了两块可读缓冲：

| 地址 | 内容 | 有对焦框 |
|---|---|---|
| 0xdf480000 | X 界面缓冲（= xwd 抓到的内容） | ❌ |
| 0xdef00000 | 取景/视频缓冲 | ❌ |
| 0xfb1be000 起 5 个 | 相机 app 用的预览缓冲 | ❌ |

寄存器线索（drime4_lcd @ 0x500b0000）：

```
0x500b0920 = 0xdf480000   窗口基址
0x500b0800 = 0xdef00000   窗口基址
0x500b0928 = 0x01de0000   0x01de=478, 0x0320=800
```

结论：**对焦框由显示控制器硬件直接画，不在任何可读缓冲里**。
相机本来也不往远程流里放框（原版 web UI 叠加的只是按钮那层 UI）。

### 踩过的坑

往 /dev/mem 大范围读（8MB、5 个视频缓冲）会**直接重启相机**——
越界读到未映射区或设备寄存器会触发外部中止。两次都重启了。
**不要再用 dd/devmemdump 扫未知区域**，只能读已确认的窗口/寄存器地址。

### 现在的做法

预览器按模式决定画不画框：

- AF区=选择AF(0) 或 触摸AF=AF点(2) → 画角框（位置来自 0x4df694/698）
- 脸部侦测 / 跟踪AF → **不画**（框是动态的，画了误导）

样式：四角括号 + 中心十字，白色带暗边，对焦瞬间变绿。