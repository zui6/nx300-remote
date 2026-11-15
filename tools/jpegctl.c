/*
 * jpegctl.c -- 直接调用 libudd 的底层编码器，自填质量表
 *
 * 目的：验证 param[3] 指向的 4x16 量化表能否控制输出大小。
 * 全填 255 => 极端低质量，若输出字节数骤降即证明可控。
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
#define SYS_write  4
#define SYS_open   5
#define SYS_close  6
#define SYS_ioctl  54
#define SYS_mmap2  192

struct sma_req { unsigned phys; unsigned size; };

/* libudd 导出 */
extern int jpeg_open(void);
extern int jpeg_encoder(unsigned int *param);
extern int jpeg_deInit(void);
extern int jpeg_close(void);

/* 硬件编码器参数表（布局来自 cHalJpeg::Encode 反编译） */
#define P_FMT     0
#define P_ONE     1
#define P_FLAG    2
#define P_QTAB    3
#define P_W       4
#define P_H       5
#define P_OUTSZ   6
#define P_Y       7
#define P_U       8
#define P_V       9
#define P_P4      10
#define P_SIZE13  13
#define P_SIZE14  14
#define P_OUTBUF  15
#define P_16      16
#define P_CAP     17

static long g_sma = -1;
static void put(const char *s) { int n = 0; while (s[n]) n++; sc3(SYS_write, 1, (long) s, n); }
static void put_num(int v) { char b[16]; int i = 15; b[i--] = 0; if (!v) b[i--] = '0'; while (v > 0) { b[i--] = (char)('0'+v%10); v/=10; } put(&b[i+1]); }
static void die(int c) { sc1(SYS_exit, c); for (;;) {} }

static void *amap(unsigned size, unsigned *phys_out)
{
    struct sma_req r; r.phys = 0; r.size = size;
    if (sc4(SYS_ioctl, g_sma, 0xc0087305, (long)&r, 0) < 0 || !r.phys) return 0;
    long va = sc6(SYS_mmap2, 0, size, 3, 1, g_sma, r.phys >> 12);
    if (va < 0 && va > -4096) return 0;
    *phys_out = r.phys;
    return (void *) va;
}

static int wfile(const char *p, const unsigned char *d, int n)
{
    long fd = sc3(SYS_open, (long)p, 0x241, 0644);
    if (fd < 0) return -1;
    int w = 0;
    while (w < n) { long r = sc3(SYS_write, fd, (long)(d+w), n-w); if (r <= 0) break; w += (int)r; }
    sc1(SYS_close, fd);
    return w;
}

/* 4 张量化表，每张 16 个 u32 */
static unsigned int qt[4][16];
static unsigned int *qtp[4];

void ctl_main(void)
{
    const int W = 320, H = 240, PLANE = 320 * 240;
    const int IN_SIZE = 256 * 1024;

    put("[ctl] start\n");
    g_sma = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
    if (g_sma < 0) { put("no d4_sma\n"); die(1); }

    unsigned ip = 0, op = 0;
    void *in = amap(IN_SIZE, &ip);
    if (!in) { put("in alloc failed\n"); die(2); }
    void *out = amap(262144, &op);
    if (!out) { put("out alloc failed\n"); die(3); }
    put("[ctl] in=0x"); put_num((int)(unsigned long)in); put(" out=0x"); put_num((int)(unsigned long)out); put("\n");

    unsigned char *p = (unsigned char *) in;
    for (int i = 0; i < IN_SIZE; i++) p[i] = 128;

    /* 极端低质量量化表 */
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 16; j++) qt[i][j] = 255;
    for (int i = 0; i < 4; i++) qtp[i] = qt[i];

    put("[ctl] jpeg_open...\n");
    int fd = jpeg_open();
    put("[ctl] fd="); put_num(fd); put("\n");
    if (fd < 0) { put("jpeg_open failed\n"); die(4); }

    unsigned int param[24];
    for (int i = 0; i < 24; i++) param[i] = 0;
    param[P_FMT] = 2;                 /* cHalJpeg 对 fmt=2 会转成 0 */
    param[P_ONE] = 1;
    param[P_FLAG] = 0;
    param[P_QTAB] = (unsigned int)(unsigned long) qtp;
    param[P_W] = W;
    param[P_H] = H;
    param[P_OUTSZ] = 0;
    param[P_Y] = (unsigned int)(unsigned long) in;
    param[P_U] = (unsigned int)(unsigned long)(p + PLANE);
    param[P_V] = (unsigned int)(unsigned long)(p + PLANE + PLANE / 4);
    param[P_P4] = 0;
    param[P_SIZE13] = PLANE;
    param[P_SIZE14] = PLANE;
    param[P_OUTBUF] = (unsigned int)(unsigned long) out;
    param[P_16] = 20000;
    param[P_CAP] = 65535;

    put("[ctl] jpeg_encoder...\n");
    int rc = jpeg_encoder(param);
    put("[ctl] rc="); put_num(rc);
    put(" outsz="); put_num((int) param[P_OUTSZ]); put("\n");

    if (param[P_OUTSZ] > 0 && param[P_OUTSZ] < 262144) {
        wfile("/mnt/mmc/ctl.jpg", (unsigned char *) out, (int) param[P_OUTSZ]);
        put("[ctl] written\n");
    }
    jpeg_deInit();
    jpeg_close();
    put("[ctl] done\n");
    die(0);
}
