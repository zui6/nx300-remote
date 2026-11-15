"""nx300_view.py —— 电脑端取景预览

从相机 daemon 拉 JPEG 实时显示，统计帧率与耗时，点画面即可设置对焦点。

用法:
    python nx300_view.py [IP] [预设]

预设: 0=流畅 1=标准 2=清晰
按键: 1/2/3 切换画质, O 叠加相机界面, 空格 暂停, ESC 退出
"""

import io
import sys
import time
import http.client
import threading

import tkinter as tk
from tkinter import font as tkfont
from PIL import Image, ImageTk, ImageChops

PREVIEW_W, PREVIEW_H = 800, 480

PRESETS = {
    0: ("流畅", 70, 3),
    1: ("标准", 70, 2),
    2: ("清晰", 85, 1),
}


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
        self.stats = Stats()
        self.last = None
        self.osd = None
        self.osd_loading = False

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
            text="1/2/3 画质 · 点击设对焦点 · O 显示相机界面 · 空格暂停 · ESC 退出",
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
            r = self.conn.getresponse()
            data = r.read()
        except Exception:
            self.connect()
            self.conn.request("GET", path)
            r = self.conn.getresponse()
            data = r.read()
        return data, time.time() - t0

    def post(self, path):
        try:
            if self.conn is None:
                self.connect()
            self.conn.request("GET", path)
            self.conn.getresponse().read()
        except Exception:
            self.connect()

    def load_osd(self):
        if self.osd_loading:
            return
        self.osd_loading = True
        try:
            conn = http.client.HTTPConnection(self.host, 80, timeout=30)
            conn.request("GET", "/api/v1/osd/snap")
            data = conn.getresponse().read()
            conn.close()
            if data[:2] == b"\xff\xd8":
                img = Image.open(io.BytesIO(data)).convert("RGB")
                if img.size != (PREVIEW_W, PREVIEW_H):
                    img = img.resize((PREVIEW_W, PREVIEW_H), Image.LANCZOS)
                self.osd = img
        except Exception:
            pass
        self.osd_loading = False

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
            if not data or data[:2] != b"\xff\xd8":
                self.stats.dropped += 1
                continue
            self.stats.tick(len(data), dt)
            try:
                self.last = Image.open(io.BytesIO(data)).convert("RGB")
            except Exception:
                self.stats.dropped += 1

    def refresh_ui(self):
        if self.last is not None:
            img = self.last
            self.last = None
            if img.size != (PREVIEW_W, PREVIEW_H):
                img = img.resize((PREVIEW_W, PREVIEW_H), Image.NEAREST)
            if self.osd is not None:
                img = ImageChops.lighter(img, self.osd)
            self.photo = ImageTk.PhotoImage(img)
            self.canvas.delete("all")
            self.canvas.create_image(0, 0, anchor="nw", image=self.photo)
        s = self.stats
        name = PRESETS[self.preset][0]
        extra = ""
        if self.paused:
            extra += "  [暂停]"
        if self.osd is not None:
            extra += "  [界面]"
        elif self.osd_loading:
            extra += "  [抓界面…]"
        self.info.config(
            text="%s · %.1f fps · %.1f KB/帧 · 往返 %.0f ms%s"
            % (name, s.fps, s.kb, s.ms, extra))
        self.root.after(30, self.refresh_ui)

    # ---------- 交互 ----------
    def on_click(self, ev):
        x = max(0, min(PREVIEW_W - 1, ev.x))
        y = max(0, min(PREVIEW_H - 1, ev.y))
        threading.Thread(target=self.post,
                         args=("/api/v1/input/focus?x=%d&y=%d" % (x, y),),
                         daemon=True).start()

    def on_key(self, ev):
        if ev.keysym == "Escape":
            self.quit()
        elif ev.char and ev.char in "123":
            self.preset = int(ev.char) - 1
        elif ev.char and ev.char in ("o", "O"):
            if self.osd is not None:
                self.osd = None
            else:
                threading.Thread(target=self.load_osd, daemon=True).start()
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
