package com.nx300.remote;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.DialogInterface;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import java.io.BufferedInputStream;
import java.io.OutputStream;
import android.graphics.BitmapFactory;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.text.InputType;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.EditText;
import android.widget.GridLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;

import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * NX300 Remote —— 原生遥控 App
 *
 * 直接调用相机守护进程的 HTTP API：
 *   GET /api/v1/liveview/get        HQ 800x480 NV12 (576000 B)
 *   GET /api/v1/liveview/get_low    LQ (288000 B)
 *   GET /api/v1/input/inject?key=   单击按键
 *   GET /api/v1/input/inject?keydown= / ?keyup=   按下/抬起
 *   GET /api/v1/camera/info|status  机型与状态
 *
 * 不加载任何网页，相机端不需要 web_root。
 */
public class MainActivity extends Activity {

    private static final String PREFS = "nx300";
    private static final int COLOR_BG = 0xFF0B0D10;
    private static final int COLOR_FG = 0xFFE8EDF3;

    /* fade 查表 */
    private static final int[] RV = new int[256];
    private static final int[] GU = new int[256];
    private static final int[] GV = new int[256];
    private static final int[] BU = new int[256];
    static {
        for (int i = 0; i < 256; i++) {
            int u = i - 128, v = i - 128;
            RV[i] = Math.round(1.402f * v);
            GU[i] = Math.round(-0.344136f * u);
            GV[i] = Math.round(-0.714136f * v);
            BU[i] = Math.round(1.772f * u);
        }
    }

    private final Handler ui = new Handler(Looper.getMainLooper());
    private final ExecutorService net = Executors.newSingleThreadExecutor();
    private final ExecutorService keyPool = Executors.newSingleThreadExecutor();

    private String ip = "";
    private volatile boolean lowQuality = true;
    private volatile int quality = 1;
    private static final String[] Q_LABEL = {"\u6D41\u7545", "\u6807\u51C6", "\u6E05\u6670"};
    private volatile boolean running = false;
    private Thread worker;

    private ImageView view;
    private TextView status;
    private TextView diag;
    private TextView paramText;
    private volatile boolean paramRun = true;
    private Thread paramThread;
    private Button qualityBtn;

    private int frames = 0;
    private String model = "-";
    private int battery = -1;
    private String mode = "-";

    /* ---------------------------------------------------------------- */

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        SharedPreferences sp = getSharedPreferences(PREFS, MODE_PRIVATE);
        ip = normHost(sp.getString("ip", ""));
        lowQuality = sp.getBoolean("low", true);
        quality = sp.getInt("quality", 1);

        setContentView(buildUi());

        if (ip.length() == 0) {
            askIp();
        } else {
            connect();
        }
        ui.post(tick);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (ip.length() > 0 && !running) connect();
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopLive();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopLive();
        net.shutdownNow();
        keyPool.shutdownNow();
    }

    /* ---------------------------------------------------------------- UI */

    private View buildUi() {
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(COLOR_BG);
        root.setPadding(10, 10, 10, 10);

        status = new TextView(this);
        status.setTextColor(COLOR_FG);
        status.setTextSize(12);
        status.setSingleLine(true);
        status.setText("未连接 · 点这里设置 IP");
        status.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) { askIp(); }
        });

        qualityBtn = new Button(this);
        qualityBtn.setText(Q_LABEL[quality]);
        qualityBtn.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                quality = (quality + 1) % 3;
                getSharedPreferences(PREFS, MODE_PRIVATE).edit()
                        .putInt("quality", quality)
                        .putBoolean("low", lowQuality).apply();
                qualityBtn.setText(Q_LABEL[quality]);
                restartLive();
            }
        });

        LinearLayout top = new LinearLayout(this);
        top.setOrientation(LinearLayout.HORIZONTAL);
        top.setGravity(Gravity.CENTER_VERTICAL);
        top.addView(status, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));
        top.addView(qualityBtn, new LinearLayout.LayoutParams(dp(64), LinearLayout.LayoutParams.WRAP_CONTENT));
        root.addView(top);

        paramText = new TextView(this);
        paramText.setTextSize(13);
        paramText.setTextColor(0xFF7FD1FF);
        paramText.setPadding(0, dp(3), 0, dp(3));
        paramText.setText("");
        root.addView(paramText);

        diag = new TextView(this);
        diag.setTextSize(10);
        diag.setTextColor(0xFFFF8A80);
        diag.setVisibility(View.GONE);
        root.addView(diag);

        view = new ImageView(this);
        view.setScaleType(ImageView.ScaleType.FIT_CENTER);
        view.setBackgroundColor(Color.BLACK);
        root.addView(view, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f));

        root.addView(buildControls());
        return root;
    }

    private View buildControls() {
        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.BOTTOM);

        /* 方向键 */
        GridLayout pad = new GridLayout(this);
        pad.setColumnCount(3);
        pad.setRowCount(3);
        pad.addView(keyBtn("▲", "KP_Up"), cell(1, 0));
        pad.addView(keyBtn("◀", "KP_Left"), cell(0, 1));
        pad.addView(keyBtn("OK", "KP_Enter"), cell(1, 1));
        pad.addView(keyBtn("▶", "KP_Right"), cell(2, 1));
        pad.addView(keyBtn("▼", "KP_Down"), cell(1, 2));
        row.addView(pad, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        /* 中间功能键 */
        LinearLayout mid = new LinearLayout(this);
        mid.setOrientation(LinearLayout.VERTICAL);
        mid.setPadding(dp(8), 0, dp(8), 0);

        LinearLayout r1 = hrow();
        r1.addView(keyBtn("MENU", "Menu"));
        r1.addView(keyBtn("Fn", "XF86HomePage"));
        r1.addView(keyBtn("回放", "XF86Tools"));
        mid.addView(r1);

        LinearLayout r2 = hrow();
        r2.addView(keyBtn("EV-", "XF86Reload"));
        r2.addView(keyBtn("对焦", "XF86Launch9"));
        r2.addView(keyBtn("EV+", "XF86Reload"));
        mid.addView(r2);

        LinearLayout r3 = hrow();
        r3.addView(keyBtn("删除", "KP_Delete"));
        r3.addView(lcdBtn());
        mid.addView(r3);

        row.addView(mid, new LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f));

        /* 右侧：快门 + 录像 */
        LinearLayout right = new LinearLayout(this);
        right.setOrientation(LinearLayout.VERTICAL);
        right.setGravity(Gravity.CENTER);

        Button shutter = new Button(this);
        shutter.setText("●");
        shutter.setTextSize(28);
        shutter.setOnTouchListener(new View.OnTouchListener() {
            public boolean onTouch(View v, MotionEvent e) {
                switch (e.getActionMasked()) {
                    case MotionEvent.ACTION_DOWN:
                        v.setPressed(true);
                        sendKey("keydown=Super_L");     /* S1 半按对焦 */
                        return true;
                    case MotionEvent.ACTION_UP:
                    case MotionEvent.ACTION_CANCEL:
                        v.setPressed(false);
                        sendKey("keydown=Super_R");     /* S2 全按 */
                        sendKey("keyup=Super_R");
                        sendKey("keyup=Super_L");
                        return true;
                }
                return false;
            }
        });
        right.addView(shutter, new LinearLayout.LayoutParams(dp(72), dp(72)));

        Button rec = keyBtn("REC", "XF86WebCam");
        right.addView(rec, new LinearLayout.LayoutParams(dp(72), LinearLayout.LayoutParams.WRAP_CONTENT));

        row.addView(right, new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, LinearLayout.LayoutParams.WRAP_CONTENT));

        return row;
    }

    private LinearLayout hrow() {
        LinearLayout l = new LinearLayout(this);
        l.setOrientation(LinearLayout.HORIZONTAL);
        return l;
    }

    private GridLayout.LayoutParams cell(int col, int row) {
        GridLayout.LayoutParams p = new GridLayout.LayoutParams();
        p.columnSpec = GridLayout.spec(col, 1f);
        p.rowSpec = GridLayout.spec(row, 1f);
        p.width = dp(54);
        p.height = dp(54);
        p.setMargins(dp(2), dp(2), dp(2), dp(2));
        return p;
    }

    private Button keyBtn(final String label, final String key) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(12);
        b.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                sendKey("key=" + key);
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(48));
        lp.setMargins(dp(2), dp(2), dp(2), dp(2));
        b.setLayoutParams(lp);
        return b;
    }

    private boolean lcdOn = true;

    private Button lcdBtn() {
        final Button b = new Button(this);
        b.setText("关屏");
        b.setTextSize(12);
        b.setOnClickListener(new View.OnClickListener() {
            public void onClick(View v) {
                lcdOn = !lcdOn;
                sendApi(lcdOn ? "/api/v1/lcd/on" : "/api/v1/lcd/off");
                b.setText(lcdOn ? "关屏" : "开屏");
            }
        });
        LinearLayout.LayoutParams lp = new LinearLayout.LayoutParams(
                LinearLayout.LayoutParams.WRAP_CONTENT, dp(48));
        lp.setMargins(dp(2), dp(2), dp(2), dp(2));
        b.setLayoutParams(lp);
        return b;
    }

    private void sendApi(final String path) {
        if (ip.length() == 0) return;
        keyPool.execute(new Runnable() {
            public void run() {
                try { httpString(base() + path, 2500); } catch (Exception ignored) { }
            }
        });
    }
    private int dp(int v) {
        return (int) (v * getResources().getDisplayMetrics().density);
    }

    /* ------------------------------------------------------------- 网络 */

    private String base() { return "http://" + ip; }

    private void askIp() {
        final EditText input = new EditText(this);
        input.setInputType(InputType.TYPE_CLASS_TEXT);
        input.setHint("192.168.1.142");
        input.setText(ip);
        new AlertDialog.Builder(this)
                .setTitle("相机 IP")
                .setMessage("相机会在开机后于 LCD 弹出 IP，也可以在路由器里查 nx300.lan。")
                .setView(input)
                .setPositiveButton("连接", new DialogInterface.OnClickListener() {
                    public void onClick(DialogInterface d, int w) {
                        ip = normHost(input.getText().toString().trim()
                                .replace("http://", "").replaceAll("/+$", ""));
                        getSharedPreferences(PREFS, MODE_PRIVATE).edit().putString("ip", ip).apply();
                        connect();
                    }
                })
                .setNegativeButton("取消", null)
                .show();
    }

    /* 只填 IP 时用默认端口 80（daemon 已顶替原版） */
    private static String normHost(String v) {
        if (v == null) return "";
        v = v.trim();
        if (v.length() == 0) return v;
        /* 兼容旧版本存下来的 :8080（daemon 现在监听 80） */
        int c = v.indexOf(':');
        if (c > 0 && v.substring(c + 1).trim().equals("8080")) {
            return v.substring(0, c);
        }
        return v;
    }

    private String hostPart() {
        String v = ip.trim();
        int c = v.indexOf(':');
        return c > 0 ? v.substring(0, c) : v;
    }

    private int portPart() {
        String v = ip.trim();
        int c = v.indexOf(':');
        if (c > 0) {
            try { return Integer.parseInt(v.substring(c + 1)); } catch (Exception ignored) { }
        }
        return 80;
    }

    private String probeTcp(String host, int port) {
        Socket sock = new Socket();
        try {
            sock.connect(new InetSocketAddress(host, port), 3000);
            return "TCP " + host + ":" + port + " OK";
        } catch (Exception e) {
            return "TCP " + host + ":" + port + " FAIL: "
                    + e.getClass().getSimpleName() + " " + e.getMessage();
        } finally {
            try { sock.close(); } catch (Exception ignored) { }
        }
    }

    private void connect() {
        if (ip.length() == 0) { askIp(); return; }
        status.setText("连接 " + ip + " …");
        net.execute(new Runnable() {
            public void run() {
                String info = null;
                String why = "";
                try {
                    info = httpString(base() + "/api/v1/camera/info", 6000);
                } catch (Exception e) {
                    why = e.getClass().getSimpleName() + ": " + e.getMessage();
                }
                if (info == null) {
                    final String detail = probeTcp(hostPart(), portPart())
                            + "\nHTTP: " + (why.length() == 0 ? "(empty)" : why);
                    ui.post(new Runnable() {
                        public void run() {
                            status.setText("连不上 " + ip + "，点这里改 IP");
                            diag.setText(detail);
                            diag.setVisibility(View.VISIBLE);
                        }
                    });
                    return;
                }
                try {
                    JSONObject o = new JSONObject(info);
                    model = o.optString("model", "NX300");
                } catch (Exception ignored) { }
                ui.post(new Runnable() {
                    public void run() {
                        status.setText(model + " · " + ip);
                        startParamPoll();
        startLive();
                        polling = false;
                        pollStatus();
                    }
                });
            }
        });
    }

    private boolean polling = false;

    private void pollStatus() {
        if (polling || ip.length() == 0) return;
        polling = true;
        net.execute(new Runnable() {
            public void run() {
                try {
                    String s = httpString(base() + "/api/v1/camera/status", 3000);
                    JSONObject o = new JSONObject(s);
                    battery = o.optInt("battery_percent", -1);
                    mode = o.optString("mode", "-");
                } catch (Exception ignored) { }
                ui.postDelayed(new Runnable() {
                    public void run() {
                        if (ip.length() > 0) pollStatus();
                    }
                }, 3000);
            }
        });
    }

    private void sendKey(final String query) {
        if (ip.length() == 0) return;
        keyPool.execute(new Runnable() {
            public void run() {
                try {
                    httpString(base() + "/api/v1/input/inject?" + query, 2500);
                } catch (Exception ignored) { }
            }
        });
    }

    private String httpString(String u, int timeout) throws IOException {
        byte[] b = httpBytes(u, timeout);
        return b == null ? null : new String(b, "UTF-8");
    }

    private byte[] httpBytes(String u, int timeout) throws IOException {
        HttpURLConnection c = (HttpURLConnection) new URL(u).openConnection();
        c.setConnectTimeout(timeout);
        c.setReadTimeout(timeout);
        c.setUseCaches(false);
        try {
            InputStream in = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream(600000);
            byte[] buf = new byte[32768];
            int n;
            while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
            in.close();
            return bo.toByteArray();
        } finally {
            c.disconnect();
        }
    }

    /* ----------------------------------------------------------- 取景 */

    /* ---- 持久 TCP 连接：每帧重开连接要付 ~70ms，复用后仅 ~5ms ---- */
    private Socket liveSock;
    private InputStream liveIn;
    private OutputStream liveOut;

    private void closeLive() {
        try { if (liveSock != null) liveSock.close(); } catch (Exception ignored) { }
        liveSock = null; liveIn = null; liveOut = null;
    }

    private byte[] fetchKeepAlive(String path) throws IOException {
        if (liveSock == null || liveSock.isClosed()) {
            Socket s = new Socket();
            s.connect(new InetSocketAddress(hostPart(), portPart()), 3000);
            s.setTcpNoDelay(true);
            s.setSoTimeout(5000);
            liveSock = s;
            liveIn = new BufferedInputStream(s.getInputStream(), 65536);
            liveOut = s.getOutputStream();
        }
        String req = "GET " + path + " HTTP/1.1\r\nHost: " + ip
                + "\r\nConnection: keep-alive\r\n\r\n";
        liveOut.write(req.getBytes("US-ASCII"));
        liveOut.flush();

        StringBuilder sb = new StringBuilder(256);
        int matched = 0;
        while (matched < 4) {
            int b = liveIn.read();
            if (b < 0) throw new IOException("eof header");
            sb.append((char) b);
            if (b == '\r' && (matched == 0 || matched == 2)) matched++;
            else if (b == '\n' && (matched == 1 || matched == 3)) matched++;
            else matched = (b == '\r') ? 1 : 0;
            if (sb.length() > 4096) throw new IOException("header too long");
        }
        int cl = -1;
        for (String line : sb.toString().split("\r\n")) {
            if (line.length() > 15 && line.regionMatches(true, 0, "Content-Length:", 0, 15)) {
                cl = Integer.parseInt(line.substring(15).trim());
            }
        }
        if (cl < 0) throw new IOException("no content-length");
        byte[] data = new byte[cl];
        int off = 0;
        while (off < cl) {
            int n = liveIn.read(data, off, cl - off);
            if (n < 0) throw new IOException("eof body");
            off += n;
        }
        return data;
    }
    private void restartLive() {
        stopLive();
        startParamPoll();
        startLive();
    }

    private void startLive() {
        if (running || ip.length() == 0) return;
        running = true;
        final String path;
        {
            int q = (quality == 0) ? 70 : (quality == 1 ? 70 : 85);
            int sc = (quality == 0) ? 3 : (quality == 1 ? 2 : 1);
            path = "/api/v1/liveview/jpeg?q=" + q + "&scale=" + sc;
        }
        worker = new Thread(new Runnable() {
            public void run() {
                while (running) {
                    long t0 = System.currentTimeMillis();
                    try {

                        byte[] data = fetchKeepAlive(path);
                        final Bitmap bmp = decode(data);
                        if (bmp != null) {
                            frames++;
                            ui.post(new Runnable() {
                                public void run() { view.setImageBitmap(bmp); }
                            });
                        }
                    } catch (Exception e) {
                        closeLive();
                        try { Thread.sleep(100); } catch (InterruptedException ie) { return; }
                    }
                    long dt = System.currentTimeMillis() - t0;
                    long wait = 20 - dt;
                    if (wait > 0) {
                        try { Thread.sleep(wait); } catch (InterruptedException e) { return; }
                    }
                }
            }
        }, "liveview");
        worker.setDaemon(true);
        worker.start();
    }

    /* ------------------------------------------------------------ 参数轮询 */

    private void startParamPoll() {
        if (paramThread != null && paramThread.isAlive()) return;
        paramRun = true;
        Thread t = new Thread(new Runnable() {
            public void run() {
                while (paramRun) {
                    final String disp = formatParams(fetchText("/api/v1/params"));
                    if (disp != null) {
                        runOnUiThread(new Runnable() {
                            public void run() {
                                if (paramText != null) paramText.setText(disp);
                            }
                        });
                    }
                    try {
                        Thread.sleep(1000);
                    } catch (InterruptedException e) {
                        return;
                    }
                }
            }
        });
        t.setDaemon(true);
        paramThread = t;
        t.start();
    }

    private String fetchText(String path) {
        HttpURLConnection c = null;
        try {
            URL u = new URL("http://" + ip + path);
            c = (HttpURLConnection) u.openConnection();
            c.setConnectTimeout(3000);
            c.setReadTimeout(3000);
            InputStream in = c.getInputStream();
            ByteArrayOutputStream bo = new ByteArrayOutputStream();
            byte[] b = new byte[512];
            int n;
            while ((n = in.read(b)) > 0) bo.write(b, 0, n);
            in.close();
            return new String(bo.toByteArray(), "UTF-8");
        } catch (Exception e) {
            return null;
        } finally {
            if (c != null) c.disconnect();
        }
    }

    private String formatParams(String js) {
        if (js == null) return null;
        try {
            JSONObject o = new JSONObject(js);
            int iso = o.optInt("iso", -1);
            int num = o.optInt("shutter_num", 0);
            int den = o.optInt("shutter_den", 0);
            int ap = o.optInt("aperture10", 0);
            StringBuilder sb = new StringBuilder();
            if (iso > 0) sb.append("ISO ").append(iso);
            if (den > 0 && num > 0) {
                if (sb.length() > 0) sb.append("     ");
                if (num == 1) sb.append("1/").append(den);
                else if (den == 1) sb.append(num).append("s");
                else sb.append(num).append("/").append(den);
            }
            if (ap > 0) {
                if (sb.length() > 0) sb.append("     ");
                sb.append("F").append(ap / 10).append(".").append(ap % 10);
            }
            if (sb.length() == 0) sb.append("\u53C2\u6570\u4E0D\u53EF\u7528");
            return sb.toString();
        } catch (Exception e) {
            return null;
        }
    }
    private void stopLive() {
        running = false;
        closeLive();
        if (worker != null) {
            worker.interrupt();
            worker = null;
        }
    }

    private final Runnable tick = new Runnable() {
        public void run() {
            if (ip.length() > 0) {
                String qs = Q_LABEL[quality];
                String s = model + " · " + ip + " · " + qs + " · " + frames + "fps";
                if (battery >= 0) s += " · 电量 " + battery + "%";
                if (!"-".equals(mode)) s += " · " + mode;
                status.setText(s);
                frames = 0;
            }
            ui.postDelayed(this, 1000);
        }
    };

    /* NV12 -> Bitmap */
    private Bitmap decode(byte[] d) {
        if (d == null || d.length < 4) return null;
        if ((d[0] & 0xff) == 0xFF && (d[1] & 0xff) == 0xD8) {
            return BitmapFactory.decodeByteArray(d, 0, d.length);
        }
        int len = d.length, w, h, scale;
        if (len == 800 * 480 * 3 / 2) { w = 800; h = 480; scale = 1; }
        else if (len == 800 * 480 * 3 / 4) { w = 800; h = 480; scale = 2; }
        else if (len == 720 * 480 * 3 / 2) { w = 720; h = 480; scale = 1; }
        else if (len == 720 * 480 * 3 / 4) { w = 720; h = 480; scale = 2; }
        else if (len == 720 * 404 * 3 / 2) { w = 720; h = 404; scale = 1; }
        else if (len == 640 * 480 * 3 / 2) { w = 640; h = 480; scale = 1; }
        else return null;

        int[] px = new int[w * h];
        if (scale == 1) {
            int uvOff = w * h, o = 0;
            for (int y = 0; y < h; y++) {
                int row = y * w, uvo = uvOff + (y >> 1) * w;
                for (int x = 0; x < w; x++, o++) {
                    int Y = d[row + x] & 0xff;
                    int c = uvo + (x & ~1);
                    int U = d[c] & 0xff, V = d[c + 1] & 0xff;
                    int r = Y + RV[V], g = Y + GU[U] + GV[V], b = Y + BU[U];
                    px[o] = 0xff000000 | (clamp(b) << 16) | (clamp(g) << 8) | clamp(r);
                }
            }
        } else {
            int w2 = w >> 1, h2 = h >> 1, uv2 = w2 * h2, o = 0;
            for (int y = 0; y < h; y++) {
                int sy = y >> 1, syo = sy * w2, suvo = uv2 + (sy >> 1) * w2;
                for (int x = 0; x < w; x++, o++) {
                    int sx = x >> 1;
                    int Y = d[syo + sx] & 0xff;
                    int c = suvo + (sx & ~1);
                    int U = d[c] & 0xff, V = d[c + 1] & 0xff;
                    int r = Y + RV[V], g = Y + GU[U] + GV[V], b = Y + BU[U];
                    px[o] = 0xff000000 | (clamp(b) << 16) | (clamp(g) << 8) | clamp(r);
                }
            }
        }
        return Bitmap.createBitmap(px, w, h, Bitmap.Config.ARGB_8888);
    }

    private static int clamp(int v) {
        return v < 0 ? 0 : (v > 255 ? 255 : v);
    }
}
