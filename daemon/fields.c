/*
 * fields.c —— 读取 di-camera-app 的整张设置字段表
 *
 * 静态分析发现：0x156c38 处有一张 203 项的指针表，
 * 每项指向一个设置字段（都在 0x4df5c0 - 0x4df900 的 .bss 里）。
 * 代码通过索引间接访问这些字段，所以字段地址不会出现在指令里。
 *
 * 这里直接读表本身，再逐个读字段值——不需要硬编码 203 个地址。
 */

#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>

#include "fields.h"
#include "params.h"

#define ADDR_TABLE  0x156c38UL   /* 字段指针表 */
#define MAX_FIELDS  256
#define FIELD_LO    0x4df000UL
#define FIELD_HI    0x4e0000UL

static int vm_read(int pid, unsigned long addr, void *buf, size_t len)
{
#ifdef SYS_process_vm_readv
    struct iovec local, remote;
    local.iov_base = buf;
    local.iov_len = len;
    remote.iov_base = (void *) addr;
    remote.iov_len = len;
    return (int) syscall(SYS_process_vm_readv, pid, &local, 1, &remote, 1, 0);
#else
    (void) pid; (void) addr; (void) buf; (void) len;
    return -1;
#endif
}

/* 目前确认过语义的字段（值来自实验） */
const char *field_name(unsigned long addr)
{
    switch (addr) {
        /* 曝光相关 */
        case 0x4df5c0: return "ae_lock";
        case 0x4df5c8: return "ev";
        case 0x4df5cc: return "av_tv_sel";
        case 0x4df5dc: return "shutter";
        case 0x4df5e0: return "fno";
        case 0x4df5e4: return "av16";
        case 0x4df5e8: return "shutter_info";
        case 0x4df5ec: return "lock3a";
        case 0x4df5f0: return "flash";
        /* AF */
        case 0x4df608: return "af_area_ref";
        case 0x4df68c: return "af_area";
        case 0x4df6b8: return "mf_aid";
        /* ISO */
        case 0x4df648: return "iso_idx";
        case 0x4df64c: return "iso_idx_max";
        case 0x4df72c: return "iso_step";
        /* 白平衡 */
        case 0x4df650: return "wb";
        case 0x4df654: return "kelvin";
        case 0x4df658: return "wb_value";
        case 0x4df61c: return "wb_custom";
        /* 照片风格 */
        case 0x4df678: return "pw1";
        case 0x4df67c: return "pw2";
        case 0x4df680: return "pw3";
        case 0x4df684: return "pw4";
        /* 测光 / 驱动 */
        case 0x4df6c0: return "meter_spot";
        case 0x4df720: return "metering";
        case 0x4df6cc: return "drive";
        default: return NULL;
    }
}
int fields_read_all(struct field_dump *out)
{
    unsigned int table[MAX_FIELDS];
    int pid, n, i;
    int count = 0;

    if (out == NULL) return -1;
    out->count = 0;
    out->pid = -1;

    pid = params_find_pid();
    if (pid < 0) return -1;
    out->pid = pid;

    if (vm_read(pid, ADDR_TABLE, table, sizeof(table)) != (int) sizeof(table))
        return -1;

    for (i = 0; i < MAX_FIELDS; i++) {
        unsigned long addr = table[i];
        unsigned int value;

        if (addr < FIELD_LO || addr >= FIELD_HI) break;      /* 表尾 */
        if (vm_read(pid, addr, &value, sizeof(value)) != (int) sizeof(value))
            continue;

        out->items[count].addr = addr;
        out->items[count].value = value;
        out->items[count].name = field_name(addr);
        count++;
    }

    out->count = count;
    return count > 0 ? 0 : -1;
}
