/*
 * names.c —— 参数值 → 中文名称
 *
 * 名称来源：相机自带翻译包
 *   /usr/share/locale/zh_CN/LC_MESSAGES/di-camera-app-nx300.mo
 * 白平衡的枚举顺序来自 .rodata 0x2467a0 的选项指针表（顺序即枚举值）。
 * 其余枚举由实验 + 反编译确认，未确认的返回 NULL（调用方省略该字段）。
 */

#include <stddef.h>
#include "names.h"

static const char *const WB_NAMES[] = {
    "自动白平衡", "日光", "阴天", "荧光灯_W", "荧光灯_N",
    "荧光灯_D", "白炽灯", "闪光灯", "自定义", "色温"
};

static const char *const METERING_NAMES[] = {
    "多重测光", "点测光", "中央重点"
};

static const char *const AF_AREA_NAMES[] = {
    "选择AF", "多重AF", "脸部侦测AF", "跟踪AF", "连续AF", "自拍AF"
};

const char *name_wb(int v)
{
    if (v < 0 || v >= (int) (sizeof(WB_NAMES) / sizeof(WB_NAMES[0]))) return NULL;
    return WB_NAMES[v];
}

const char *name_metering(int v)
{
    if (v < 0 || v >= (int) (sizeof(METERING_NAMES) / sizeof(METERING_NAMES[0]))) return NULL;
    return METERING_NAMES[v];
}

const char *name_af_area(int v)
{
    if (v < 0 || v >= (int) (sizeof(AF_AREA_NAMES) / sizeof(AF_AREA_NAMES[0]))) return NULL;
    return AF_AREA_NAMES[v];
}

/* 驱动模式：0=单张、1=高速连拍、5=AE BKT（实验确认），其余未定 */
const char *name_drive(int v)
{
    switch (v) {
        case 0: return "单张";
        case 1: return "高速连拍";
        case 5: return "AE BKT";
        default: return NULL;
    }
}
