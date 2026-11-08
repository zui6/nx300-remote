/*
 * params.c —— 通过 process_vm_readv 直接读 di-camera-app 的曝光参数
 *
 * 地址是逆向出来的（NX300, fw 1.45，di-camera-app-n）：
 *   0x4df5dc  u16  快门分母
 *   0x4df5de  u16  快门分子
 *   0x4df5e2  u16  光圈 x10      (F8.0 -> 80)
 *   0x4df5b8  u32  Tv x16
 *   0x4df5e4  u32  Av x16
 *   0x5c3890  u32  ISO           (800, 3200 ...)
 *   0x5c3894  u32  ISO 上限
 *
 * 只读，不 attach，目标进程完全不受影响。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <sys/uio.h>
#include <sys/syscall.h>

#include "params.h"

#define ADDR_EXPO   0x4df5b0UL   /* 曝光结构，读 0x40 字节 */
#define ADDR_ISO    0x5c3880UL   /* ISO 结构，读 0x20 字节 */

static int s_cached_pid = -1;

static int pid_is_camera(int pid)
{
    char path[64], comm[32];
    FILE *f;
    int ok = 0;

    snprintf(path, sizeof(path), "/proc/%d/comm", pid);
    f = fopen(path, "r");
    if (f == NULL) return 0;
    if (fgets(comm, sizeof(comm), f) != NULL) {
        comm[strcspn(comm, "\n")] = '\0';
        if (strcmp(comm, "di-camera-app-n") == 0 ||
            strcmp(comm, "di-camera-app") == 0) ok = 1;
    }
    fclose(f);
    return ok;
}

static int find_camera_pid(void)
{
    DIR *d;
    struct dirent *e;

    /* 缓存命中就直接用，省掉每次扫 /proc */
    if (s_cached_pid > 0 && pid_is_camera(s_cached_pid)) return s_cached_pid;
    s_cached_pid = -1;

    d = opendir("/proc");
    if (d == NULL) return -1;

    while ((e = readdir(d)) != NULL) {
        char path[64], comm[32];
        FILE *f;
        int pid;

        if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
        pid = atoi(e->d_name);
        snprintf(path, sizeof(path), "/proc/%d/comm", pid);
        f = fopen(path, "r");
        if (f == NULL) continue;
        if (fgets(comm, sizeof(comm), f) != NULL) {
            comm[strcspn(comm, "\n")] = '\0';
            if (strcmp(comm, "di-camera-app-n") == 0 ||
                strcmp(comm, "di-camera-app") == 0) {
                fclose(f);
                closedir(d);
                s_cached_pid = pid;
                return pid;
            }
        }
        fclose(f);
    }
    closedir(d);
    return -1;
}

static int read_mem(int pid, unsigned long addr, void *buf, size_t len)
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

int params_read(struct cam_params *p)
{
    unsigned char expo[0x40];
    unsigned char isob[0x20];
    int pid;

    if (p == NULL) return -1;
    memset(p, 0, sizeof(*p));

    pid = find_camera_pid();
    if (pid < 0) return -1;
    p->pid = pid;

    if (read_mem(pid, ADDR_EXPO, expo, sizeof(expo)) != (int) sizeof(expo)) return -1;
    if (read_mem(pid, ADDR_ISO, isob, sizeof(isob)) != (int) sizeof(isob)) return -1;

    p->shutter_den = (unsigned short) (expo[0x2c] | (expo[0x2d] << 8));
    p->shutter_num = (unsigned short) (expo[0x2e] | (expo[0x2f] << 8));
    p->aperture10  = (unsigned short) (expo[0x32] | (expo[0x33] << 8));
    p->tv16 = expo[0x08] | (expo[0x09] << 8) | (expo[0x0a] << 16) | (expo[0x0b] << 24);
    p->av16 = expo[0x34] | (expo[0x35] << 8) | (expo[0x36] << 16) | (expo[0x37] << 24);
    p->iso  = (int) (isob[0x10] | (isob[0x11] << 8) | (isob[0x12] << 16) | (isob[0x13] << 24));
    p->iso_max = (int) (isob[0x14] | (isob[0x15] << 8) | (isob[0x16] << 16) | (isob[0x17] << 24));

    /* 合理性检查，防止结构体错位时输出垃圾 */
    if (p->iso < 50 || p->iso > 51200) p->iso = -1;
    if (p->iso_max < 50 || p->iso_max > 51200) p->iso_max = -1;
    if (p->shutter_den == 0 || p->shutter_den > 32000) p->shutter_den = 0;
    if (p->shutter_num == 0 || p->shutter_num > 64) p->shutter_num = 0;
    if (p->aperture10 < 10 || p->aperture10 > 400) p->aperture10 = 0;

    /*
     * TV x16 与快门分母应当自洽：tv16/16 ~= log2(den)。
     * 结构体一旦错位，这里一般对不上，宁可报不可用也不输出垃圾。
     */
    if (p->shutter_den > 0 && p->tv16 > 0) {
        unsigned int expect = p->tv16 / 16;
        unsigned int d = p->shutter_den, log2d = 0;
        while (d > 1) { d >>= 1; log2d++; }
        if (expect > log2d + 1 || log2d > expect + 1) {
            p->tv16 = 0;
            p->av16 = 0;
        }
    }

    return 0;
}
