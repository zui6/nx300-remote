"""nx300_view.py —— 电脑端取景预览

从相机 daemon 拉 JPEG 实时显示，统计帧率，显示对焦点，点画面即可设置对焦点。

用法:
    python nx300_view.py [IP] [预设]

预设: 0=流畅 1=标准 2=清晰
按键: 1/2/3 切换画质, F 显示/隐藏对焦框, 空格 暂停, ESC 退出
"""

import io
import json
import sys
import time
import http.client
import threading

import tkinter as tk
from tkinter import font as tkfont
from PIL import Image, ImageTk

PREVIEW_W, PREVIEW_H = 800, 480

PRESETS = {
    0: ("流畅", 70, 3),
    1: ("标准", 70, 2),
    2: ("清晰", 85, 1),
}

# 对焦框坐标(AF 空间) -> 预览图像素坐标
# 实机标定: AF_x = 0.9092*canvas_x - 9.9 , AF_y = 0.9487*canvas_y - 36.3
def af_to_canvas_x(ax):
    return (ax + 9.9) / 0.9092


def af_to_canvas_y(ay):
    return (ay + 36.3) / 0.9487


BOX_W, BOX_H = 56, 42


class Stats:
    def __init__(self):
        self.frames = 0
        self.t0 = time.time()
        self.fps = 0.0
        self.kb = 0
        self.ms = 0
        self.dropped = 0

    def tick(self, nbytes, dt):
        self.frames += 1
        self.kb = nbytes / 1024.0
        self.ms = dt * 1000.0
        now = time.time()
        if now - self.t0 >= 1.0:
            self.fps = self.frames / (now - self.t0)
            self.frames = 0
            self.t0 = now


class Viewer:
    def __init__(self, host, preset):
        self.host = host
        self.preset = preset
        self.paused = False
        self.running = True
        self.conn = None
        self.pconn = None
        self.stats = Stats()
        self.last = None
        self.af = None
        self.af_area = -1
        self.touch_af = -1
        self.af_mode = -1
        self.show_af = True
        self.af_t = 0.0
        self.focusing = False

        self.root = tk.Tk()
        self.root.title("NX300 取景预览")
        self.root.configure(bg="#101014")
        self.root.geometry("820x620")

        self.canvas = tk.Canvas(self.root, width=PREVIEW_W, height=PREVIEW_H,
                                bg="#181820", highlightthickness=0)
        self.canvas.pack(padx=6, pady=6)
        self.canvas.bind("<Button-1>", self.on_click)

        big = tkfont.Font(family="Consolas", size=12)
        self.info = tk.Label(self.root, text="连接中…", bg="#101014", fg="#7FD1FF",
                             font=big, anchor="w", justify="left")
        self.info.pack(fill="x", padx=8, pady=(0, 4))

        self.keys = tk.Label(
            self.root,
            text="1/2/3 画质 · 点击设对焦点 · F 对焦框 · 空格暂停 · ESC 退出",
            bg="#101014", fg="#555566", font=("Consolas", 10))
        self.keys.pack(fill="x", padx=8, pady=(0, 6))

        self.root.bind("<Key>", self.on_key)
        self.root.focus_set()
        self.root.protocol("WM_DELETE_WINDOW", self.quit)

        self.thread = threading.Thread(target=self.loop, daemon=True)
        self.thread.start()
        self.root.after(200, self.refresh_ui)

    # ---------- 网络 ----------
    def connect(self):
        if self.conn is not None:
            try:
                self.conn.close()
            except Exception:
                pass
        self.conn = http.client.HTTPConnection(self.host, 80, timeout=10)

    def fetch_jpeg(self):
        if self.conn is None:
            self.connect()
        name, q, scale = PRESETS[self.preset]
        path = "/api/v1/liveview/jpeg?q=%d&scale=%d" % (q, scale)
        t0 = time.time()
        try:
            self.conn.request("GET", path)
            data = self.conn.getresponse().read()
        except Exception:
            self.connect()
            self.conn.request("GET", path)
            data = self.conn.getresponse().read()
        return data, time.time() - t0

    def fetch_af(self):
        try:
            if self.pconn is None:
                self.pconn = http.client.HTTPConnection(self.host, 80, timeout=5)
            self.pconn.request("GET", "/api/v1/params")
            d = json.loads(self.pconn.getresponse().read().decode("utf-8"))
            ax, ay = d.get("af_x"), d.get("af_y")
            self.af_area = d.get("af_area", -1)
            self.touch_af = d.get("touch_af", -1)
            self.af_mode = d.get("af_mode", -1)
            if ax is not None and ay is not None and ax >= 0 and ay >= 0:
                self.af = (ax, ay)
        except Exception:
            self.pconn = None

    def post(self, path):
        try:
            if self.conn is None:
                self.connect()
            self.conn.request("GET", path)
            self.conn.getresponse().read()
        except Exception:
            self.connect()

    # ---------- 主循环 ----------
    def loop(self):
        self.connect()
        while self.running:
            if self.paused:
                time.sleep(0.05)
                continue
            try:
                data, dt = self.fetch_jpeg()
            except Exception:
                time.sleep(0.2)
                continue
            if data and data[:2] == b"\xff\xd8":
                self.stats.tick(len(data), dt)
                try:
                    self.last = Image.open(io.BytesIO(data)).convert("RGB")
                except Exception:
                    self.stats.dropped += 1
            else:
                self.stats.dropped += 1

            if self.show_af and time.time() - self.af_t > 0.25:
                self.af_t = time.time()
                self.fetch_af()

    def refresh_ui(self):
        if self.last is not None:
            img = self.last
            self.last = None
            if img.size != (PREVIEW_W, PREVIEW_H):
                img = img.resize((PREVIEW_W, PREVIEW_H), Image.NEAREST)
            self.photo = ImageTk.PhotoImage(img)
            self.canvas.delete("all")
            self.canvas.create_image(0, 0, anchor="nw", image=self.photo)
            self.draw_af()

        s = self.stats
        name = PRESETS[self.preset][0]
        afname = {0: "选择AF", 1: "多重AF", 2: "脸部侦测", 3: "自拍AF"}.get(self.af_area, "?")
        extra = ("  " + afname) if self.af_area >= 0 else ""
        if self.paused:
            extra += "  [暂停]"
        self.info.config(
            text="%s · %.1f fps · %.1f KB/帧 · 往返 %.0f ms%s"
            % (name, s.fps, s.kb, s.ms, extra))
        self.root.after(30, self.refresh_ui)

    def draw_af(self):
        if not self.show_af or self.af is None:
            return
        # 只有在"能确定一个固定对焦框"的模式下才画：
        #   AF区=选择AF(0) 或 触摸AF=AF点(2)
        # 脸部侦测/跟踪AF 的框是动态的，画了反而误导
        static_box = (self.af_area == 0) or (self.touch_af == 2)
        if not static_box:
            return
        cx = af_to_canvas_x(self.af[0])
        cy = af_to_canvas_y(self.af[1])
        w, h = BOX_W / 2, BOX_H / 2
        c = "#FFFFFF" if not self.focusing else "#00E676"

        # 先画一条暗边做对比，再画亮边
        for color, lw in (("#000000", 3), (c, 1)):
            for sx, sy in ((-1, -1), (1, -1), (-1, 1), (1, 1)):
                x0 = cx + sx * w
                y0 = cy + sy * h
                # 水平臂
                self.canvas.create_line(x0, y0, x0 - sx * 14, y0, fill=color, width=lw)
                # 垂直臂
                self.canvas.create_line(x0, y0, x0, y0 - sy * 11, fill=color, width=lw)

        # 中心十字
        for color, lw in (("#000000", 3), (c, 1)):
            self.canvas.create_line(cx - 4, cy, cx + 4, cy, fill=color, width=lw)
            self.canvas.create_line(cx, cy - 3, cx, cy + 3, fill=color, width=lw)
    # ---------- 交互 ----------
    def on_click(self, ev):
        x = max(0, min(PREVIEW_W - 1, ev.x))
        y = max(0, min(PREVIEW_H - 1, ev.y))
        self.focusing = True
        self.root.after(1200, lambda: setattr(self, 'focusing', False))
        threading.Thread(target=self.post,
                         args=("/api/v1/input/focus?x=%d&y=%d" % (x, y),),
                         daemon=True).start()

    def on_key(self, ev):
        if ev.keysym == "Escape":
            self.quit()
        elif ev.char and ev.char in "123":
            self.preset = int(ev.char) - 1
        elif ev.char and ev.char in ("f", "F"):
            self.show_af = not self.show_af
        elif ev.keysym == "space":
            self.paused = not self.paused

    def quit(self):
        self.running = False
        time.sleep(0.05)
        self.root.destroy()

    def run(self):
        self.root.mainloop()


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "192.168.1.142"
    preset = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    Viewer(host, preset).run()


if __name__ == "__main__":
    main()
