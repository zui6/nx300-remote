/*
 * names.c —— 参数值 → 中文名称
 *
 * 名称来源：
 *   /usr/share/locale/zh_CN/LC_MESSAGES/di-camera-app-nx300.mo（相机自带翻译包）
 * 枚举顺序来源：各 set_xxx() 函数里按值取图标的数组（Ghidra 反编译确认）
 */

#include <stddef.h>
#include "names.h"

#define COUNT(a) ((int) (sizeof(a) / sizeof((a)[0])))

/* AF 区：set_af_area 的图标顺序 (selection/multi/fd/sfd) */
static const char *const AF_AREA_NAMES[] = {
    "选择AF", "多重AF", "脸部侦测AF", "自拍AF"
};

/* 测光：set_metering (multi/spot/center) */
static const char *const METERING_NAMES[] = {
    "多重测光", "点测光", "中央重点"
};

/* 驱动：set_drive (single/7fps/5fps/burst/timer/aeb/wbb/pwb) */
static const char *const DRIVE_NAMES[] = {
    "单张", "高速连拍", "连拍", "高速拍摄",
    "定时器", "AE BKT", "WB BKT", "图片向导 BKT"
};

/* 画质：set_quality (sf/f/normal/raw/raw_sf/raw_f/raw_normal) */
static const char *const QUALITY_NAMES[] = {
    "超精细", "精细", "标准",
    "RAW", "RAW+超精细", "RAW+精细", "RAW+标准"
};

/* 白平衡：.rodata 0x2467a0 指针表顺序 */
static const char *const WB_NAMES[] = {
    "自动白平衡", "日光", "阴天", "荧光灯_W", "荧光灯_N",
    "荧光灯_D", "白炽灯", "闪光灯", "自定义", "色温"
};

/* 图片向导：0x4df674 直接是模式索引（实验：3=人像、4=风景） */
static const char *const PW_NAMES[] = {
    "关", "标准", "鲜明", "人像", "风景", "悠远", "怀旧",
    "古典", "清爽", "宁静", "用户设置1", "用户设置2", "用户设置3"
};

const char *name_af_area(int v)
{
    if (v < 0 || v >= COUNT(AF_AREA_NAMES)) return NULL;
    return AF_AREA_NAMES[v];
}

const char *name_metering(int v)
{
    if (v < 0 || v >= COUNT(METERING_NAMES)) return NULL;
    return METERING_NAMES[v];
}

const char *name_drive(int v)
{
    if (v < 0 || v >= COUNT(DRIVE_NAMES)) return NULL;
    return DRIVE_NAMES[v];
}

const char *name_quality(int v)
{
    if (v < 0 || v >= COUNT(QUALITY_NAMES)) return NULL;
    return QUALITY_NAMES[v];
}

const char *name_wb(int v)
{
    if (v < 0 || v >= COUNT(WB_NAMES)) return NULL;
    return WB_NAMES[v];
}

const char *name_picture_wizard(int v)
{
    if (v < 0 || v >= COUNT(PW_NAMES)) return NULL;
    return PW_NAMES[v];
}
