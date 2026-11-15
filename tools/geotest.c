/*
 * geotest.c -- 探测硬件编码器期望的源图像几何
 *
 * 分配 800x480 YUV420（576000B），Y 平面填横向灰度渐变，U/V 填 128。
 * 编码 320x240，size 字段填 800*480（而不是 320*240），看输出是否正确。
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
static void *g_in = 0, *g_out = 0;

static void put(const char *s) { int n = 0; while (s[n]) n++; sc3(SYS_write, 1, (long) s, n); }
static void put_num(int v) { char b[16]; int i = 15; b[i--] = 0; if (!v) b[i--] = '0'; while (v > 0) { b[i--] = (char)('0'+v%10); v/=10; } put(&b[i+1]); }
static void put_hex(unsigned v) { char b[12]; int i; b[10]=0; for (i=9;i>=0;i--){unsigned d=v&0xf; b[i]=(char)(d<10?'0'+d:'a'+d-10); v>>=4;} put(b); }
static void die(int c) { sc1(SYS_exit, c); for (;;) {} }

static void *alloc_map(unsigned size, unsigned *phys_out)
{
    struct sma_req r; r.phys = 0; r.size = size;
    if (sc4(SYS_ioctl, g_sma, 0xc0087305, (long)&r, 0) < 0 || !r.phys) return 0;
    long va = sc6(SYS_mmap2, 0, size, 3, 1, g_sma, r.phys >> 12);
    if (va < 0 && va > -4096) return 0;
    *phys_out = r.phys;
    return (void *) va;
}

static int write_file(const char *p, const unsigned char *d, int n)
{
    long fd = sc3(SYS_open, (long)p, 0x241, 0644);
    if (fd < 0) return -1;
    int w = 0;
    while (w < n) { long r = sc3(SYS_write, fd, (long)(d+w), n-w); if (r <= 0) break; w += (int)r; }
    sc1(SYS_close, fd);
    return w;
}

void geo_main(void)
{
    const int SW = 800, SH = 480;          /* 假定的源尺寸 */
    const int PLANE = SW * SH;             /* 384000 */
    const int IN_SIZE = PLANE + PLANE / 2; /* 576000 */
    const int W = 320, H = 240;            /* 输出尺寸 */

    put("[geo] start\n");
    g_sma = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
    if (g_sma < 0) { put("open d4_sma failed\n"); die(1); }

    unsigned in_phys = 0, out_phys = 0;
    g_in = alloc_map(IN_SIZE, &in_phys);
    put("[geo] in phys=0x"); put_hex(in_phys); put("\n");
    if (!g_in) { put("in alloc failed\n"); die(2); }
    g_out = alloc_map(262144, &out_phys);
    put("[geo] out phys=0x"); put_hex(out_phys); put("\n");
    if (!g_out) { put("out alloc failed\n"); die(3); }

    unsigned char *y = (unsigned char *) g_in;
    for (int r = 0; r < SH; r++)
        for (int c = 0; c < SW; c++)
            y[r * SW + c] = (unsigned char) ((c * 255) / SW);
    unsigned char *u = y + PLANE;
    unsigned char *v = u + PLANE / 4;
    for (int i = 0; i < PLANE / 4; i++) { u[i] = 128; v[i] = 128; }
    put("[geo] filled 800x480 gradient\n");

    int rc = bma_open();
    put("[geo] bma_open rc="); put_num(rc); put("\n");

    struct yuv_data yd;
    unsigned char *q = (unsigned char *) &yd;
    for (unsigned i = 0; i < sizeof(yd); i++) q[i] = 0;
    yd.fmt = 2;
    yd.width = W;
    yd.height = H;
    yd.size = PLANE;          /* 关键：填源平面大小 800*480 */
    yd.p1 = g_in;
    yd.p2 = u;
    yd.p3 = v;
    yd.p4 = 0;

    struct mmf_data od;
    od.buf = g_out;
    od.size = 65535;

    put("[geo] encode...\n");
    mmf_jpeg_encode(&yd, &od);
    put("[geo] size="); put_num(od.size); put("\n");
    if (od.size <= 0) { put("[geo] FAILED\n"); die(6); }

    unsigned char *jpg = (unsigned char *) g_out;
    put("[geo] magic="); put_num(jpg[0]); put(","); put_num(jpg[1]); put("\n");
    write_file("/mnt/mmc/geo.jpg", jpg, od.size);
    put("[geo] done\n");
    die(0);
}
