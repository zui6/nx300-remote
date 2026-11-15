/*
 * jpeghelper.c -- hardware JPEG encoder test (physical buffers via /dev/d4_sma)
 *
 * Run as shared object:
 *   /lib/ld-linux.so.3 /mnt/mmc/remote/jpeghelper.so
 *
 * Flow:
 *   1. ioctl(0xc0087305, {phys,size}) on /dev/d4_sma  -> phys addr
 *   2. mmap(sma_fd, phys>>12)                         -> user VA
 *   3. mmf_jpeg_encode() with PHYS addresses
 */

static long sc1(long n, long a) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r7) : "memory"); return r0; }
static long sc3(long n, long a, long b, long c) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory"); return r0; }
static long sc4(long n, long a, long b, long c, long d) { register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b; register long r2 __asm__("r2") = c; register long r3 __asm__("r3") = d; __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r7) : "memory"); return r0; }
static long sc6(long n, long a, long b, long c, long d, long e, long f) {
    register long r7 __asm__("r7") = n; register long r0 __asm__("r0") = a; register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c; register long r3 __asm__("r3") = d; register long r4 __asm__("r4") = e; register long r5 __asm__("r5") = f;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r3), "r"(r4), "r"(r5), "r"(r7) : "memory"); return r0;
}

#define SYS_exit   1
#define SYS_read   3
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_ioctl  54
#define SYS_munmap 91
#define SYS_mmap2  192

struct yuv_data {
    int fmt; int width; int height; int size;
    int pad10; int pad14; int pad18;
    void *p1; void *p2; void *p3; void *p4;
};
struct mmf_data { void *buf; int size; };
struct sma_req { unsigned phys; unsigned size; };

extern int bma_open(void);
extern void mmf_jpeg_encode(struct yuv_data *yuv, struct mmf_data *out)
    __asm__("_Z15mmf_jpeg_encodeP11tMmfYuvDataP8tMmfData");

static long g_sma = -1;
static void *g_in_va = 0;
static void *g_out_va = 0;
static unsigned g_in_size = 0;
static unsigned g_out_size = 0;

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
static void cleanup(void)
{
    if (g_in_va) { sc3(SYS_munmap, (long) g_in_va, g_in_size, 0); g_in_va = 0; }
    if (g_out_va) { sc3(SYS_munmap, (long) g_out_va, g_out_size, 0); g_out_va = 0; }
    if (g_sma >= 0) { sc1(SYS_close, g_sma); g_sma = -1; }
}
static void do_exit(int c) { cleanup(); sc1(SYS_exit, c); for (;;) { } }



static unsigned sma_alloc(unsigned size, void **va_out)
{
    if (g_sma < 0) {
        g_sma = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
        if (g_sma < 0) { put("open d4_sma failed\n"); return 0; }
    }
    struct sma_req req;
    req.phys = 0;
    req.size = size;
    long r = sc4(SYS_ioctl, g_sma, 0xc0087305, (long) &req, 0);
    if (r < 0 || req.phys == 0) { put("sma alloc failed\n"); return 0; }
    long va = sc6(SYS_mmap2, 0, size, 3, 1, g_sma, req.phys >> 12);
    if (va < 0 && va > -4096) { put("sma mmap failed\n"); return 0; }
    *va_out = (void *) va;
    return req.phys;
}

static int write_file(const char *path, const unsigned char *data, int len)
{
    long fd = sc3(SYS_open, (long) path, 0x241, 0644);
    if (fd < 0) return -1;
    int wr = 0;
    while (wr < len) {
        long n = sc3(SYS_write, fd, (long) (data + wr), len - wr);
        if (n <= 0) break;
        wr += (int) n;
    }
    sc1(SYS_close, fd);
    return wr;
}

static int read_cfg(void)
{
    long fd = sc3(SYS_open, (long) "/mnt/mmc/remote/hw.cfg", 0, 0);
    if (fd < 0) return 0;
    char b[16];
    long n = sc3(SYS_read, fd, (long) b, 15);
    sc1(SYS_close, fd);
    if (n <= 0) return 0;
    b[n] = 0;
    int i = 0, v = 0;
    while (i < (int) n && (b[i] < '0' || b[i] > '9')) i++;
    while (i < (int) n && b[i] >= '0' && b[i] <= '9') v = v * 10 + (b[i++] - '0');
    return v;
}

void helper_main(void)
{
    int cfg = read_cfg();
    const int W = (cfg >= 64) ? cfg : 320;
    const int H = W * 3 / 4;
    const int PLANE = W * H;
    const int IN_SIZE = PLANE + PLANE / 2;
    const int OUT_SIZE = 131072;   /* 实际分配/映射大小 */
    const int OUT_CAP  = 65535;    /* 容量字段是 16 位，只能填到 65535 */

    put("[hw] start\n");

    void *in_va = 0, *out_va = 0;
    unsigned in_phys = sma_alloc(IN_SIZE, &in_va);
    g_in_va = in_va; g_in_size = IN_SIZE;
    put("[hw] in  phys=0x"); put_hex(in_phys); put(" va=0x"); put_hex((unsigned) (unsigned long) in_va); put("\n");
    if (!in_phys) do_exit(2);

    unsigned out_phys = sma_alloc(OUT_SIZE, &out_va);
    g_out_va = out_va; g_out_size = OUT_SIZE;
    put("[hw] out phys=0x"); put_hex(out_phys); put(" va=0x"); put_hex((unsigned) (unsigned long) out_va); put("\n");
    if (!out_phys) do_exit(3);

    int lay = read_cfg();
    put("[hw] layout="); put_num(lay); put("\n");

    unsigned char *y = (unsigned char *) in_va;
    unsigned char *uv = y + PLANE;
    /* 整个缓冲填 128：Y=128 U=V=128 => 应该是均匀灰 */
    for (int i = 0; i < IN_SIZE; i++) ((unsigned char *) in_va)[i] = 128;
    put("[hw] all-128 filled\n");

    int brc = bma_open();
    put("[hw] bma_open rc="); put_num(brc); put("\n");

    struct yuv_data yd;
    unsigned char *q = (unsigned char *) &yd;
    for (unsigned i = 0; i < sizeof(yd); i++) q[i] = 0;
    yd.width = W;
    yd.height = H;
    yd.size = PLANE;
    /* udd 内部会把虚拟地址转成物理地址，所以这里必须传 VA */
    if (lay == 0) {
        /* 平面 Y / U / V */
        yd.fmt = 2;
        yd.p1 = (void *) in_va;
        yd.p2 = (void *) (y + PLANE);
        yd.p3 = (void *) (y + PLANE + PLANE / 4);
        yd.p4 = 0;
    } else if (lay == 1) {
        /* NV12 交错 UV */
        yd.fmt = 2;
        yd.p1 = (void *) in_va;
        yd.p2 = (void *) uv;
        yd.p3 = (void *) uv;
        yd.p4 = 0;
    } else {
        /* fmt=0：内部用 size 推 UV */
        yd.fmt = 0;
        yd.p1 = (void *) in_va;
        yd.p3 = (void *) uv;
        yd.p4 = 0;
    }

    struct mmf_data od;
    od.buf = out_va;
    od.size = OUT_CAP;

    put("[hw] encode x40...\n");
    int sz[5]; int total = 0;
    for (int k = 0; k < 40; k++) {
        od.size = OUT_CAP;
        mmf_jpeg_encode(&yd, &od);
        if (k < 5) sz[k] = od.size;
        total += od.size;
        if (k == 0 || k == 39) { put("  #"); put_num(k); put(" size="); put_num(od.size); put("\n"); }
    }
    put("[hw] total bytes="); put_num(total); put("\n");
    if (sz[0] <= 0) { put("[hw] FAILED\n"); do_exit(6); }
    od.size = sz[0];

    unsigned char *jpg = (unsigned char *) out_va;
    put("[hw] magic="); put_num(jpg[0]); put(","); put_num(jpg[1]);
    put(" last="); put_num(jpg[od.size - 1]); put("\n");

    int wr = write_file("/mnt/mmc/test.jpg", jpg, od.size);
    put("[hw] wrote "); put_num(wr); put(" bytes\n");
    do_exit(0);
}
