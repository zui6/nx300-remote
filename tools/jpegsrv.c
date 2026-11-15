/*
 * jpegsrv.c -- persistent hardware JPEG encoder service
 *
 * Run:  /lib/ld-linux.so.3 /mnt/mmc/remote/jpegsrv.so
 * Protocol (binary):
 *   stdin  : 1 byte command  'E' = encode one frame, 'Q' = quit
 *   stdout : 4-byte LE length + JPEG bytes   (for 'E')
 *
 * Allocates its physical buffers ONCE at startup and reuses them,
 * because /dev/d4_sma only ever bumps its pointer (never reclaims).
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

static void put(const char *s) { int n = 0; while (s[n]) n++; sc3(SYS_write, 2, (long) s, n); }
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
static void die(int c) { sc1(SYS_exit, c); for (;;) { } }

/* --- state --- */
static long g_sma = -1;
static void *g_out_va = 0;
static unsigned g_out_size = 0;
static void *g_dummy_va = 0;

static void *map_phys(unsigned phys, unsigned size)
{
    long va = sc6(SYS_mmap2, 0, size, 3, 1, g_sma, phys >> 12);
    if (va < 0 && va > -4096) return 0;
    return (void *) va;
}

static int setup(void)
{
    g_sma = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
    if (g_sma < 0) { put("cannot open d4_sma\n"); return 0; }

    /* 输出缓冲 256KB，一次分配，永久复用 */
    struct sma_req r1;
    r1.phys = 0; r1.size = 262144;
    if (sc4(SYS_ioctl, g_sma, 0xc0087305, (long) &r1, 0) < 0 || !r1.phys) {
        put("out alloc failed\n"); return 0;
    }
    g_out_va = map_phys(r1.phys, 262144);
    if (!g_out_va) { put("out mmap failed\n"); return 0; }
    g_out_size = 262144;

    /* 4KB 哑输入（编码器实际用相机自己的取景缓冲，这里只为满足校验） */
    struct sma_req r2;
    r2.phys = 0; r2.size = 4096;
    if (sc4(SYS_ioctl, g_sma, 0xc0087305, (long) &r2, 0) >= 0 && r2.phys) {
        g_dummy_va = map_phys(r2.phys, 4096);
    }
    if (!g_dummy_va) { put("dummy alloc failed\n"); return 0; }

    int rc = bma_open();
    put("bma_open rc="); put_num(rc); put("\n");
    put("out va=0x"); put_hex((unsigned) (unsigned long) g_out_va);
    put(" dummy=0x"); put_hex((unsigned) (unsigned long) g_dummy_va); put("\n");
    return 1;
}

void srv_main(void)
{
    const int W = 320, H = 240;
    const int PLANE = 320 * 240;

    if (!setup()) die(1);
    put("ready\n");

    for (;;) {
        unsigned char cmd;
        long n = sc3(SYS_read, 0, (long) &cmd, 1);
        if (n <= 0) break;
        if (cmd == 'Q') break;
        if (cmd != 'E') continue;

        struct yuv_data yd;
        unsigned char *q = (unsigned char *) &yd;
        for (unsigned i = 0; i < sizeof(yd); i++) q[i] = 0;
        yd.fmt = 2;
        yd.width = W;
        yd.height = H;
        yd.size = PLANE;
        yd.p1 = g_dummy_va;
        yd.p2 = (void *) ((unsigned char *) g_dummy_va + PLANE / 4);
        yd.p3 = (void *) ((unsigned char *) g_dummy_va + PLANE / 2);
        yd.p4 = 0;

        struct mmf_data od;
        od.buf = g_out_va;
        od.size = 65535;

        mmf_jpeg_encode(&yd, &od);

        unsigned int len = (od.size > 0) ? (unsigned int) od.size : 0;
        unsigned char hdr[4];
        hdr[0] = (unsigned char) (len & 0xff);
        hdr[1] = (unsigned char) ((len >> 8) & 0xff);
        hdr[2] = (unsigned char) ((len >> 16) & 0xff);
        hdr[3] = (unsigned char) ((len >> 24) & 0xff);
        sc3(SYS_write, 1, (long) hdr, 4);
        if (len) sc3(SYS_write, 1, (long) g_out_va, (long) len);
    }

    if (g_out_va) sc3(SYS_munmap, (long) g_out_va, g_out_size, 0);
    if (g_sma >= 0) sc1(SYS_close, g_sma);
    die(0);
}
