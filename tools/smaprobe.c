/*
 * smaprobe.c —— /dev/d4_sma 物理内存分配探测
 *
 * 模式从 /mnt/mmc/remote/probe.cfg 读（ld.so 直接跑 .so 时没有可靠 argv）。
 *
 *   mode 0  只查池大小，不分配
 *   mode 1  分配 4096，打印物理地址，尝试两种 mmap 方式
 *   mode 2  分配 + 写入回环校验 + munmap
 */

static long sc1(long n, long a) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory"); return r0; }
static long sc3(long n, long a, long b, long c) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory"); return r0; }
static long sc4(long n, long a, long b, long c, long d) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c; register long r3 __asm__("r3") = d; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory"); return r0; }
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c; register long r3 __asm__("r3") = d; register long r4 __asm__("r4") = e; register long r5 __asm__("r5") = f;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory"); return r0;
}

#define SYS_exit  1
#define SYS_read  3
#define SYS_write 4
#define SYS_open  5
#define SYS_close 6
#define SYS_ioctl 54
#define SYS_munmap 91
#define SYS_mmap2 192

struct sma_req { unsigned phys; unsigned size; };

static void put(const char *s) { int n = 0; while (s[n]) n++; sc3(SYS_write, 1, (long) s, n); }
static void put_num(int v) {
    char b[16]; int i = 15; b[i--] = 0;
    if (v == 0) b[i--] = '0';
    while (v > 0) { b[i--] = (char) ('0' + v % 10); v /= 10; }
    put(&b[i + 1]);
}
static void put_hex(unsigned v) {
    char b[12]; int i; b[10] = 0;
    for (i = 9; i >= 0; i--) { unsigned d = v & 0xf; b[i] = (char) (d < 10 ? '0' + d : 'a' + d - 10); v >>= 4; }
    put(b);
}
static void do_exit(int c) { sc1(SYS_exit, c); for (;;) { } }

/* 从配置文件读模式号 */
static int read_mode(void)
{
    long fd = sc3(SYS_open, (long) "/mnt/mmc/remote/probe.cfg", 0, 0);
    if (fd < 0) return -1;
    char buf[16];
    long n = sc3(SYS_read, fd, (long) buf, 15);
    sc1(SYS_close, fd);
    if (n <= 0) return -1;
    buf[n] = 0;
    int i = 0, v = 0;
    while (i < n && (buf[i] < '0' || buf[i] > '9')) i++;
    while (i < n && buf[i] >= '0' && buf[i] <= '9') v = v * 10 + (buf[i++] - '0');
    return v;
}

void probe_main(void)
{
    int mode = read_mode();
    put("[sma] mode=");
    if (mode < 0) { put("(no cfg)\n"); do_exit(1); }
    put_num(mode); put("\n");

    long fd = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
    if (fd < 0) { put("[sma] open failed\n"); do_exit(1); }
    put("[sma] fd ok\n");

    unsigned pool = 0;
    long r = sc4(SYS_ioctl, fd, 0x80047308, (long) &pool, 0);
    put("[sma] pool query ioctl ret="); put_num((int) r);
    put(" size=0x"); put_hex(pool); put("\n");

    if (mode == 0) { put("[sma] done (no alloc)\n"); do_exit(0); }

    struct sma_req req;
    req.phys = 0;
    req.size = (mode >= 1024) ? (unsigned) mode : 4096;
    r = sc4(SYS_ioctl, fd, 0xc0087305, (long) &req, 0);
    put("[sma] alloc ret="); put_num((int) r);
    put(" phys=0x"); put_hex(req.phys);
    put(" size="); put_num((int) req.size); put("\n");
    if (r < 0 || req.phys == 0) { put("[sma] alloc failed\n"); do_exit(2); }

    /* 方式 A：在 SMA 自身 fd 上 mmap，offset = 物理地址 */
    long va_a = sc6(SYS_mmap2, 0, req.size, 3, 1, fd, req.phys >> 12);
    put("[sma] mmap(fd, phys)="); put_hex((unsigned) va_a); put("\n");

    /* 方式 B：offset = 0 */
    long va_b = sc6(SYS_mmap2, 0, req.size, 3, 1, fd, 0);
    put("[sma] mmap(fd, 0)="); put_hex((unsigned) va_b); put("\n");

    /* 方式 C：/dev/mem */
    long memfd = sc3(SYS_open, (long) "/dev/mem", 2, 0);
    long va_c = -1;
    if (memfd >= 0) {
        va_c = sc6(SYS_mmap2, 0, req.size, 3, 1, memfd, req.phys >> 12);
        sc1(SYS_close, memfd);
    }
    put("[sma] mmap(/dev/mem, phys)="); put_hex((unsigned) va_c); put("\n");

    if (mode == 1) { put("[sma] done (no write)\n"); do_exit(0); }

    /* 模式 4：验证 virt->phys 转换 */
    if (mode == 4) {
        unsigned v = (unsigned) (unsigned long) va_a;
        put("[sma] virt_to_phys(in=0x"); put_hex(v); put(") ...\n");
        long r2 = sc4(SYS_ioctl, fd, 0xc0047302, (long) &v, 0);
        put("[sma] ret="); put_num((int) r2);
        put(" out=0x"); put_hex(v);
        put("  expect=0x"); put_hex(req.phys); put("\n");
        do_exit(0);
    }

    /* 模式 2：交叉验证哪种映射真的指向 phys */
    if (va_c < 0 && va_c > -4096) { put("[sma] no /dev/mem mapping, abort\n"); do_exit(3); }
    unsigned char *mem = (unsigned char *) va_c;   /* 基准：确定指向 phys */

    mem[0] = 0x11;
    put("[sma] 基准 /dev/mem 可写\n");

    if (!(va_a < 0 && va_a > -4096)) {
        ((unsigned char *) va_a)[0] = 0xAA;
        put("[sma] 写 mmap(fd,phys)[0]=0xAA, 基准读到 0x"); put_hex(mem[0]); put(mem[0] == 0xAA ? "  -> 同一块\n" : "  -> 不同块\n");
    }
    if (!(va_b < 0 && va_b > -4096)) {
        ((unsigned char *) va_b)[0] = 0xBB;
        put("[sma] 写 mmap(fd,0)[0]=0xBB, 基准读到 0x"); put_hex(mem[0]); put(mem[0] == 0xBB ? "  -> 同一块\n" : "  -> 不同块\n");
    }

    /* 反向：通过基准写，看哪个映射能看到 */
    mem[0] = 0xCC;
    if (!(va_a < 0 && va_a > -4096))
        { put("[sma] 基准写 0xCC, mmap(fd,phys) 读到 0x"); put_hex(((unsigned char *) va_a)[0]); put("\n"); }
    if (!(va_b < 0 && va_b > -4096))
        { put("[sma] 基准写 0xCC, mmap(fd,0)   读到 0x"); put_hex(((unsigned char *) va_b)[0]); put("\n"); }
    do_exit(0);
}
