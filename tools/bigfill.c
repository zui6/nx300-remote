/*
 * bigfill.c -- 判定硬件编码器到底读不读我们的缓冲
 *
 * 分配 4MB，整块填 128（Y=U=V=128 => 中性灰）。
 * 无论编码器用什么步长/偏移，只要它读的是这块内存，输出必然是均匀灰。
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

struct yuv_data { int fmt, width, height, size, pad10, pad14, pad18; void *p1, *p2, *p3, *p4; };
struct mmf_data { void *buf; int size; };
struct sma_req { unsigned phys; unsigned size; };

extern int bma_open(void);
extern void mmf_jpeg_encode(struct yuv_data *yuv, struct mmf_data *out)
    __asm__("_Z15mmf_jpeg_encodeP11tMmfYuvDataP8tMmfData");

static long g_sma = -1;
static void put(const char *s) { int n = 0; while (s[n]) n++; sc3(SYS_write, 1, (long) s, n); }
static void put_num(int v) { char b[16]; int i = 15; b[i--] = 0; if (!v) b[i--] = '0'; while (v > 0) { b[i--] = (char)('0'+v%10); v/=10; } put(&b[i+1]); }
static void put_hex(unsigned v) { char b[12]; int i; b[10]=0; for (i=9;i>=0;i--){unsigned d=v&0xf; b[i]=(char)(d<10?'0'+d:'a'+d-10); v>>=4;} put(b); }
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

void fill_main(void)
{
    const int BIG = 1024 * 1024;
    const int W = 320, H = 240, PLANE = 320 * 240;

    put("[fill] start\n");
    g_sma = sc3(SYS_open, (long) "/dev/d4_sma", 2, 0);
    if (g_sma < 0) { put("no d4_sma\n"); die(1); }

    unsigned ip = 0, op = 0;
    void *in = amap(BIG, &ip);
    put("[fill] in phys=0x"); put_hex(ip); put("\n");
    if (!in) { put("in alloc failed\n"); die(2); }
    void *out = amap(262144, &op);
    put("[fill] out phys=0x"); put_hex(op); put("\n");
    if (!out) { put("out alloc failed\n"); die(3); }

    unsigned char *p = (unsigned char *) in;
    for (int i = 0; i < BIG; i++) p[i] = 128;
    put("[fill] 1MB all 128\n");

    int rc = bma_open();
    put("[fill] bma="); put_num(rc); put("\n");

    struct yuv_data yd;
    unsigned char *q = (unsigned char *) &yd;
    for (unsigned i = 0; i < sizeof(yd); i++) q[i] = 0;
    yd.fmt = 2;
    yd.width = W;
    yd.height = H;
    yd.size = PLANE;
    yd.p1 = in;
    yd.p2 = (void *) (p + PLANE);
    yd.p3 = (void *) (p + PLANE + PLANE / 4);
    yd.p4 = 0;

    struct mmf_data od;
    od.buf = out;
    od.size = 65535;

    put("[fill] encode...\n");
    mmf_jpeg_encode(&yd, &od);
    put("[fill] size="); put_num(od.size); put("\n");
    if (od.size <= 0) { put("[fill] FAILED\n"); die(6); }

    wfile("/mnt/mmc/fill.jpg", (unsigned char *) out, od.size);
    put("[fill] done\n");
    die(0);
}
