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
## 显示层备忘（以后可能用得上）

### UI 层可以直接 mmap 读，不需要 xwd

| 地址 | 内容 | 尺寸/格式 |
|---|---|---|
| `0xdf480000` | **X 帧缓冲 = UI 层** | 480x800 BGRA，stride 1920 |
| `0xdef00000` | 取景/视频层 | 800x480（布局与预览缓冲不同） |

`/dev/fb0` 读出来全是 0（映射方式问题），**必须走 /dev/mem + mmap**。

地址怎么来的：`/proc/iomem` 里只有 `drime4_lcd @ 0x500b0000`，
读该寄存器块得到窗口基址：

```
0x500b0920 = 0xdf480000   ← UI 窗口（= X 帧缓冲）
0x500b0800 = 0xdef00000   ← 取景窗口
0x500b0928 = 0x01de0000   → 0x01de=478, 0x0320=800（尺寸）
```

寄存器可以用 `devmemdump` 读（mmap，安全）。每次开机地址可能变，**应该运行时从寄存器现读**。

### 性能估算（未实测完）

| 步骤 | 估算 |
|---|---|
| mmap 读 1.5MB | 2–5 ms |
| 120 块哈希（80x40/块） | 1–2 ms |
| 稳态上传（UI 不变） | 0 字节 |
| 对比：xwd 全量 | 600–2000 ms |

结论：用原版的"分块哈希 + 只传变化块"协议，服务端改成直接读 0xdf480000，
**开销从秒级降到毫秒级**。UI 基本静止，实际几乎免费。

### 但对焦框依然拿不到

UI 层里有网格线、顶部信息、底部参数条、按钮 —— 但**没有对焦框**。
对焦框是显示控制器硬件直接画的，不在任何可读缓冲里，LCD 寄存器也不跟随 AF 点变化。

### ⚠️ /dev/mem 禁区

**不要用 dd/devmemdump 扫未知区域或大范围连续读**：
越界读到未映射区或设备寄存器会触发外部中止，**相机直接重启**（踩过两次）。

已知安全的读法：
- 单个窗口基址（从寄存器读到的）
- `0x500b0000` 起的 LCD 寄存器块（0x1000 内）
## 硬件 JPEG 编码器（已打通，有重大限制）

### 调用链（全部确认）

```
libmmfhal.so.0
  mmf_jpeg_encode(tMmfYuvData*, tMmfData*)     C++ 修饰名 _Z15mmf_jpeg_encodeP11tMmfYuvDataP8tMmfData
    -> cHalJpeg::GetInstance() / cHalJpeg::Encode()
       -> jpeg_open / jpeg_init / jpeg_encoder  (实现在 libudd.so.3.1)
          -> mmap 寄存器 0x500a0000 (JPEG_TOP) / 0x500a1000 (JPEG_CTRL)
```

**前置条件**：必须先调 `bma_open()`（libudd 导出），否则报
`bma device driver is not opened`。

### 结构体（反编译 + 实测确认）

```c
struct yuv_data {              // tMmfYuvData
    int fmt;      // +0x00   0/1/2/3，经 cHalJpeg 后实际都变成 0 或 1
    int width;    // +0x04   必须是 16 的倍数
    int height;   // +0x08   必须是 16 的倍数（否则编码器直接返回 size=0）
    int size;     // +0x0c   Y 平面大小 = w*h
    int pad10, pad14, pad18;
    void *p1, *p2, *p3, *p4;  // +0x1c / +0x20 / +0x24 / +0x28
};
struct mmf_data { void *buf; int size; };   // size 按 16 位读，>65535 会截断成 0
```

**必须传虚拟地址**，udd 内部会自己转物理地址。
传物理地址会导致 DMA 写飞 → **内核崩溃重启**（实测踩到）。

### 物理内存：/dev/d4_sma

```
ioctl(fd, 0x80047308, &u32)                 查询池大小（约 398 MB: 0x18e00000）
ioctl(fd, 0xc0087305, &{u32 phys; u32 size}) 分配，phys 是出参
mmap(fd, phys >> 12, size, RW, SHARED)      映射到用户空间（offset 用物理地址页号）
ioctl(fd, 0xc0047302, &u32 va)              虚拟地址 → 物理地址（实测正确）
```

映射方式已验证：`mmap(fd, phys>>12)` 与 `/dev/mem` 映射指向同一物理页；
`mmap(fd, 0)` 是**另一块**内存，不能用。

### 实测结果

| 项目 | 结果 |
|---|---|
| 能否编码 | ✅ 输出合法 JPEG（FFD8…FFD9），Pillow 可解码 |
| 编码速度 | **约 16.8 ms/帧**（40 帧 0.671 s 墙钟）|
| 输出大小 | 320×240 约 42 KB（质量固定为很高）|
| 编码内容 | **相机实时取景画面**，不是我们传入的数据 |

### 重大限制

1. **输入指针被忽略**：把输入缓冲填成全 128（纯灰），输出仍是实时场景
   （10084 种颜色）。硬件实际编码的是相机自己的取景缓冲。
   → 好处：不用自己读 /dev/mem 喂数据；坏处：无法控制输入内容。
2. **质量不可调**：输出 42 KB @320×240，对 WiFi 传输太重。
   `cHalJpeg::Encode` 里写死的 `20000` 和内部静态配置块控制质量，未找到可调入口。
3. **SMA 分配不释放**：`munmap` 不会归还池子（地址单调递增），进程退出也不归还。
   每次运行泄漏 256 KB，池子会碎片化，之后大块分配失败。**长时间运行必须先解决释放**。
4. 尺寸受 16 对齐限制，160×120 / 240×180 / 400×300 都直接失败。

### 结论

硬件编码器能跑、速度够（~60fps 能力），但当前只能编码"相机自己的实时画面"且质量固定，
**不适合直接替代现有 JPEG 路径**（42 KB/帧 × 20 fps = 840 KB/s，NX300 的 WiFi 扛不住）。

若要真正用上，需要找到质量/输入控制入口——那要继续逆 `libudd` 的 `jpeg_set_yuv_stream`
参数结构，属于后续工作。

## 相机端已有的 H.264 流（另一条路）

NX 相机在**Wi-Fi 遥控模式**下自带 H.264 直播流：

```
http://<相机IP>:7679/livestream.avi      AVI 容器 + vidsH264，640×480
```

这是相机固件自己用硬件编码的，零 CPU 开销，延迟约 100 ms
（参考实现：github.com/Berkcan90/NX300-Preview-and-Control-Panel，
客户端修补参考：github.com/LORQUAD08/Samsung-SmartCamera-64bit-NX-Mod）。

**但只在相机切到 WiFi 遥控模式时才可用**，普通拍摄模式端口不开。
我们用自建 daemon 替换了原厂 daemon，该端口目前也没有。
### 第二轮：常驻服务与"输入无效"的结论

把助手改成常驻服务（一次分配、反复编码，`tools/jpegsrv.c`）：

```
stdin : 1 字节命令 'E'=编码一帧  'Q'=退出
stdout: 4 字节小端长度 + JPEG
```

实测 20 帧 0.685 s（34 ms/帧，含写盘），帧间 md5 不同，全部是合法 JPEG。

**但是画面是绿色噪声**，不是取景内容。做了三组对照：

| 输入内容 | 输出 |
|---|---|
| 全部填 128（应为纯灰） | 10084 种颜色的场景状图像 |
| 800×480 横向渐变（size 填 800×480） | 15862 种颜色的绿色噪声 |
| 4KB 哑缓冲 | 绿色噪声 |

**结论：编码器不读我们传入的任何内存。** 绿色 = Y 有数据、色度为 0，
说明它从某处读到 Y、从别处读到 0，源地址不受 tMmfYuvData 的 p1/p2/p3 控制。

可能是相机启动时就注册了固定源缓冲（bma/udd 内部状态），我们进程里的
`bma_open()` 只是挂到同一份状态上，改不了源。

**要真正用上，必须找到源缓冲的注册入口**——继续逆 `jpeg_set_yuv_stream`
的 `param[3]`（配置块）与 `FUNC_000e2b0c`，或找 `jpeg_open` 时传入的 base。
这属于下一步工作。

### SMA 分配器的正确用法

`/dev/d4_sma` 的 alloc 是**只增不减**的 bump 分配器：
`munmap` 不归还，进程退出也不归还。相机自己的库在用户态管理池子
（`sysmmap_free` → `sysmmap_mgr_free` → `sysmmap_client_free`，返回空闲链表），
驱动那层只在整个池子初始化时分配一次。

**所以：任何用 SMA 的程序都必须"一次分配、长期复用"，不能反复申请。**
短命进程反复申请会把池子耗光，只能重启相机恢复（实测：64 KB 能分配、
115200 就失败，就是这个原因）。

### 重启相机的正确方式

```sh
# telnet 进去
reboot
```
约 30 秒恢复，daemon 自动起来（SD 卡的 autoexec 生效）。
已多次验证，不是破坏性操作。
### 决定性实验：1 MB 全填 128（结论：输入指针被完全忽略）

把 1 MB 物理缓冲整块填成 128（Y=U=V=128，等价于中性灰），
无论编码器用什么步长、从哪个偏移读，只要它读的是这块内存，输出必然是纯色灰。

**实测结果：32465 字节，4391 种颜色，绿色噪声。**
（此前 4 KB 哑缓冲、115 KB 渐变、576 KB 800×480 渐变也全部是绿色噪声。）

对照汇总：

| 输入缓冲 | 大小 | 填充 | 输出颜色数 |
|---|---|---|---|
| 4 KB 哑 | 4 KB | 未初始化 | 绿色噪声 |
| 320×240 YUV | 115 KB | 横向渐变 + 128 色度 | 10084（绿） |
| 800×480 YUV | 576 KB | 横向渐变 + 128 色度 | 15862（绿） |
| 1 MB | 1 MB | **全部 128** | **4391（绿）** |

**结论：`mmf_jpeg_encode` 完全忽略 `tMmfYuvData` 里的 p1/p2/p3。**
它读的是某个固定的、不受调用者控制的源（很可能是相机启动时注册进
bma/udd 内部状态的缓冲）。绿色 = Y 有数据、色度为 0。

也就是说：**目前只能"让硬件编码器跑起来"，但无法让它编码我们的画面。**

### /dev/d4_sma 单次分配上限

实测（干净池）：4 MB 失败、1 MB 成功、256 KB 成功。
上限在 256 KB ~ 1 MB 之间，未精确定位。

### 后续若要继续，需要查的方向

1. `jpeg_open` / `jpeg_openRegBase` 的 base 参数——源缓冲可能在那里注册
2. `jpeg_set_yuv_stream` 里 `param[3]` 指向的配置块（`cHalJpeg::Encode` 传的是
   libmmfhal 内部静态数据）——可能含源地址/质量
3. `FUNC_000e2b0c(param_1[3])` 的作用
4. 或者放弃这条路，改用相机 WiFi 遥控模式自带的 H.264 流（7679 端口）
### 质量控制的入口找到了，但代价是卡死（重要安全结论）

`FUN_000e2b0c` 就是**量化表加载器**：

```c
// param[3] 指向一个 4 元素指针数组，每个指针指向 16 个 uint32
for (i = 0; i < 4; i++)
    for (j = 0; j < 16; j++)
        write_reg(0x41c + i*0x40 + j*4, qtab[i][j]);   // 共 64 个寄存器
```

也就是说 `cHalJpeg::Encode` 传进去的 `param[3]` 就是**质量表**，
换掉它就能改质量。但 `param[3]` 指向的是 libmmfhal 内部静态数据，改不了。

于是尝试绕过 `mmf_jpeg_encode`，直接调 libudd 导出的底层三件套：

```c
int fd = jpeg_open(void);          // 打开设备并 mmap 寄存器
int rc = jpeg_encoder(param);      // = jpeg_set_yuv_stream + jpeg_do_encoder
jpeg_deInit(); jpeg_close();
```

自己按 `cHalJpeg::Encode` 的栈布局填 `param[24]`，量化表全填 255（极端低质量）。

**结果：相机卡死，需要强制重启。** 参数表错一个字段就会把硬件挂住。

### 安全结论

| 做法 | 结果 |
|---|---|
| 调 `mmf_jpeg_encode`（高层） | ✅ 能出合法 JPEG，但**输入被忽略、质量不可调** |
| 直接调 `jpeg_encoder`（底层） | ❌ **卡死相机**，参数表无法可靠重建 |

**这条路的可行边界：只能"跑起来"，不能"用起来"。**

要真正接入取景，需要相机厂商的寄存器/参数表文档，或者完整的
`cHalJpeg::Encode` 反编译级复刻（含 4 张量化表的原始内容），
风险高、收益不确定。

### 更划算的替代方案

相机在 **Wi-Fi 遥控模式**下自带 H.264 直播流：

```
http://<相机IP>:7679/livestream.avi     640×480, AVI + H.264
```

固件自己用硬件编码，零 CPU、约 100 ms 延迟，不需要碰任何底层接口。
限制是必须先让相机切到那个模式。
## 突破：硬件/NEON H.264 编码器可用（2026-11-22）

### 之前判断错误的地方

早期结论「`savsenc_h264` 只认高通平台」是**错的**。
`libs264.so` 里的 `s264_init()` 把 `/proc/cpuinfo` 的 `Hardware` 行
和一张 **30 项**平台表做 `strstr` 比对，表里有多项三星平台：

```
[ 2] 'Samsung-DRIMeIV-ES'    ← 关键
[ 5] 'SMDK4412'   [ 6] 'SMDK4212'
[ 7] 'SLP10_C210'  [14] 'UNIVERSAL_C210'   ...
```

而相机实际的 Hardware 行是：

```
Hardware        : Samsung-DRIMeIV-NX300
```

`strstr("Samsung-DRIMeIV-NX300", "Samsung-DRIMeIV-ES")` **不匹配**，
整张表扫完没命中 → 返回 `0xfffff830`（-2000）。**只差产品后缀 `-ES` vs `-NX300`。**

### 修复方式（不写系统分区）

把 `libs264.so` 里的 `Samsung-DRIMeIV-ES`（18 字节）等长改成
`Samsung-DRIMeIV-NX`（18 字节），放在 SD 卡 `tmpbin/`，
运行时用 `LD_LIBRARY_PATH` 覆盖：

```sh
export LD_LIBRARY_PATH=/mnt/mmc/tmpbin
export GST_PLUGIN_PATH=/usr/lib/gstreamer-0.10
export GST_REGISTRY=/tmp/gst.reg
gst-launch-0.10 videotestsrc num-buffers=30 \
  ! video/x-raw-yuv,format=\(fourcc\)I420,width=640,height=480,framerate=30/1 \
  ! savsenc_h264 ! filesink location=/mnt/mmc/out.h264
```

### 验证结果

码流结构完全合法（30 帧）：

```
SPS 1  PPS 1  IDR 1  非IDR片 29
```

### 性能实测（120 帧，含 videotestsrc 开销）

| 分辨率 | 耗时 | 帧率 |
|---|---|---|
| 320×240 | 4 s | **30 fps** |
| 480×360 | 8 s | 15 fps |
| 640×480 | 13 s | 9.2 fps |

随分辨率线性变化 → 是 **CPU 上的 NEON 软件编码**（不是 MFC）。
即使如此，320×240 的 30fps 也比现有 JPEG 路径（400×240 约 14fps）快一倍。

### 编码器可调参数

```
bitrate      0 = 不控码率
quantizer    1-51，默认 14
qmin/qmax    1-51
idr-size     IDR 间隔，默认 30
slice-mode   0/1/2
```

### 意义

这条路径**不依赖 MFC 固件**、**不碰系统分区**、**不需要 WiFi 遥控模式**。
可以在我们的 daemon 里直接调用 `libs264`（或起 gst 子进程），
把取景 NV12 编成 H.264 Annex-B 推给客户端，Android 端用 MediaCodec 硬解。