/*
 * memdump.c —— 通过 process_vm_readv 读取目标进程内存（不停进程、不 ptrace attach）
 *
 * 用法: memdump <pid> <outfile> [max_mb]
 *
 * 输出格式（文本头 + 二进制）：
 *   R <start_hex> <len_hex>\n
 *   <len 字节原始数据>
 * 只导出可读、且非共享库（无路径或私有）的段，用于对比"改设置前后内存差异"。
 */

#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/syscall.h>

struct region {
    unsigned long start;
    unsigned long end;
    int rw;
    char path[128];
};

static ssize_t vm_read(int pid, void *dst, unsigned long src, size_t len)
{
#ifdef SYS_process_vm_readv
    struct iovec local, remote;
    local.iov_base = dst;
    local.iov_len = len;
    remote.iov_base = (void *) src;
    remote.iov_len = len;
    return syscall(SYS_process_vm_readv, pid, &local, 1, &remote, 1, 0);
#else
    return -1;
#endif
}

int main(int argc, char **argv)
{
    int pid, out, maxmb = 64;
    long long total = 0, cap;
    FILE *maps;
    char line[256];
    unsigned char buf[65536];
    char maps_path[64];

    if (argc < 3) {
        fprintf(stderr, "usage: %s <pid> <outfile> [max_mb]\n", argv[0]);
        return 1;
    }
    pid = atoi(argv[1]);
    if (argc >= 4) maxmb = atoi(argv[3]);
    cap = (long long) maxmb * 1024 * 1024;

    snprintf(maps_path, sizeof(maps_path), "/proc/%d/maps", pid);
    maps = fopen(maps_path, "r");
    if (maps == NULL) { perror("maps"); return 1; }

    out = open(argv[2], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (out < 0) { perror("open out"); return 1; }

    while (fgets(line, sizeof(line), maps) != NULL) {
        unsigned long start, end;
        char perms[8] = {0};
        char path[128] = {0};
        int n, ok;
        unsigned long addr;
        char hdr[64];

        /* 例: 00110000-00120000 rw-p 00000000 00:00 0  [heap] */
        if (sscanf(line, "%lx-%lx %7s %*s %*s %*s %127[^\n]", &start, &end, perms, path) < 3)
            continue;
        if (perms[0] != 'r') continue;
        if (end <= start) continue;
        if ((long long) (end - start) > cap) continue;
        if (total > cap) break;

        snprintf(hdr, sizeof(hdr), "R %lx %lx\n", start, end - start);
        write(out, hdr, strlen(hdr));

        for (addr = start; addr < end; addr += sizeof(buf)) {
            size_t want = sizeof(buf);
            if (addr + want > end) want = (size_t) (end - addr);
            n = (int) vm_read(pid, buf, addr, want);
            if (n <= 0) {
                /* 读不到就写 0，保证 offsets 对齐 */
                memset(buf, 0, want);
                n = (int) want;
            }
            write(out, buf, (size_t) n);
            total += n;
        }
        ok = 1;
        (void) ok;
    }

    fclose(maps);
    close(out);
    fprintf(stderr, "dumped %lld bytes\n", total);
    return 0;
}
