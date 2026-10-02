"""
fake_bike.py — 模擬車機（展場備案）

車機燒錄失敗或硬體出狀況時，用這支程式「假裝」是 ESP32：
  - APP 端：提供跟韌體一樣的 HTTP API
      port 80（失敗改 8080）：/api/status、/api/led、/api/night、/api/phoneloc
      port 81：/stream（MJPEG，輪播 uploads/ 裡的照片）
  - 系統端：每 5 秒 POST /api/data 到伺服器，並定期 POST /api/demo 讓網頁顯示「模擬資料」標籤

只用 Python 標準函式庫，不需要另外安裝套件。

鍵盤指令（在這個視窗輸入後按 Enter）：
  b = 急煞    c = 碰撞（警示燈鎖定，APP 按「解除警示」解除）
  f = 倒車通知（倒地 10 秒）    e = 緊急事故（倒地 5 分鐘）
  p = 暫停 / 繼續移動          q = 結束
"""
import glob
import http.server
import json
import math
import os
import random
import socketserver
import sys
import threading
import time
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER = os.environ.get("SAFEWAY_SERVER", "http://127.0.0.1:8000")

# 模擬路線：花蓮市區一圈（跟展示用偵測資料同一區），約 1.4 km，騎一圈約 5 分鐘
CENTER_LAT, CENTER_LON = 23.9830, 121.6015
RADIUS_LAT, RADIUS_LON = 0.0020, 0.0022
LAP_SECONDS = 300.0

lock = threading.Lock()
state = {
    "t0": time.time(),
    "paused_at": None,
    "paused_total": 0.0,
    "event": "NORMAL",
    "event_until": 0.0,
    "hazard": False,
    "night": False,
    "manual_led": None,      # None | left | right | hazard
    "phone": None,           # (lat, lon, speed, at)
}


def ride_seconds():
    with lock:
        now = state["paused_at"] or time.time()
        return now - state["t0"] - state["paused_total"]


def snapshot():
    """目前的模擬狀態（位置、速度、姿態、事件）。"""
    t = ride_seconds()
    a = 2 * math.pi * (t % LAP_SECONDS) / LAP_SECONDS
    lat = CENTER_LAT + RADIUS_LAT * math.sin(a)
    lon = CENTER_LON + RADIUS_LON * math.cos(a)
    moving = state["paused_at"] is None
    speed = (16 + 5 * math.sin(t / 23.0) + random.uniform(-0.6, 0.6)) if moving else 0.0
    roll = (6 * math.sin(t / 7.0) + random.uniform(-0.8, 0.8)) if moving else 0.0
    pitch = -1.0 + 1.5 * math.sin(t / 31.0)

    with lock:
        if time.time() > state["event_until"]:
            state["event"] = "NORMAL"
        event = state["event"]
        hazard = state["hazard"]
        night = state["night"]
        manual = state["manual_led"]
        phone = state["phone"]

    g = {"NORMAL": 1.0 + random.uniform(0, 0.05), "BRAKE": 2.7, "COLLISION": 3.4}[event]
    if phone and time.time() - phone[3] < 5:
        loc = {"src": "phone", "lat": phone[0], "lon": phone[1],
               "speed": phone[2] if phone[2] is not None else speed}
    else:
        loc = {"src": "gps", "lat": lat, "lon": lon, "speed": speed}

    if hazard or manual == "hazard" or event == "BRAKE":
        led = "HAZARD"
    elif manual in ("left", "right"):
        led = manual.upper()
    else:
        led = "NONE"
    return {
        "lat": lat, "lon": lon, "speed": speed, "roll": roll, "pitch": pitch,
        "event": event, "g": g, "hazard": hazard, "night": night, "led": led, "loc": loc,
    }


def status_json(s, ip):
    return {
        "wifi": "sta", "ip": ip, "demo": True,
        "imu": {"ok": True, "roll": round(s["roll"], 2), "pitch": round(s["pitch"], 2),
                "ax": 0.02, "ay": 0.01, "az": round(s["g"], 2), "i2cStale": 0},
        "accel": {"event": s["event"], "g": round(s["g"], 2)},
        "gps": {"chars": 120000, "fix": True, "lat": round(s["lat"], 6),
                "lon": round(s["lon"], 6), "speed": round(s["speed"], 1)},
        "time": {"now": time.strftime("%Y-%m-%d %H:%M:%S"), "source": "demo"},
        "sd": {"ok": True}, "camera": True, "night": s["night"],
        "led": s["led"], "hazard": s["hazard"],
        "loc": {"src": s["loc"]["src"], "valid": True,
                "lat": round(s["loc"]["lat"], 6), "lon": round(s["loc"]["lon"], 6),
                "speed": round(s["loc"]["speed"], 1),
                "phoneAgeMs": int((time.time() - state["phone"][3]) * 1000) if state["phone"] else -1},
    }


def trigger(event, seconds):
    with lock:
        state["event"] = event
        state["event_until"] = time.time() + seconds
        if event == "COLLISION":
            state["hazard"] = True


# ── APP 用的 HTTP API（port 80 / 8080）────────────────────────────
class ApiHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def _send(self, code, obj):
        body = json.dumps(obj).encode()
        self.send_response(code)
        self.send_header("Content-Type", "application/json")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        url = urllib.parse.urlparse(self.path)
        q = {k: v[0] for k, v in urllib.parse.parse_qs(url.query).items()}
        if url.path == "/api/status":
            ip = self.connection.getsockname()[0]
            return self._send(200, status_json(snapshot(), ip))
        if url.path == "/api/led":
            mode = q.get("mode", "")
            with lock:
                if mode == "off":
                    state["manual_led"] = None
                    state["hazard"] = False
                    state["event"] = "NORMAL"
                elif mode in ("left", "right", "hazard"):
                    state["manual_led"] = mode
                else:
                    return self._send(400, {"error": "mode=left|right|hazard|off"})
            print(f"[APP] /api/led mode={mode}")
            return self._send(200, {"ok": True, "led": mode})
        if url.path == "/api/night":
            mode = q.get("mode", "")
            if mode not in ("on", "off"):
                return self._send(400, {"error": "mode=on|off"})
            with lock:
                state["night"] = mode == "on"
            print(f"[APP] 夜間模式 {mode}")
            return self._send(200, {"ok": True, "night": mode == "on"})
        if url.path == "/api/phoneloc":
            try:
                lat, lon = float(q["lat"]), float(q["lon"])
            except (KeyError, ValueError):
                return self._send(400, {"error": "lat & lon required"})
            spd = float(q["spd"]) if "spd" in q else None
            with lock:
                state["phone"] = (lat, lon, spd, time.time())
            return self._send(200, {"ok": True, "src": "phone"})
        self._send(404, {"error": "not found"})


# ── 鏡頭串流（port 81 /stream）──────────────────────────────────
FRAMES = []


def load_frames():
    for p in sorted(glob.glob(os.path.join(HERE, "uploads", "*.jpg")))[:40]:
        with open(p, "rb") as f:
            FRAMES.append(f.read())


class StreamHandler(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        if not self.path.startswith("/stream") or not FRAMES:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace;boundary=frame")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        i = 0
        try:
            while True:
                jpg = FRAMES[(i // 10) % len(FRAMES)]   # 每張停 2 秒
                self.wfile.write(b"\r\n--frame\r\n")
                self.wfile.write(f"Content-Type: image/jpeg\r\nContent-Length: {len(jpg)}\r\n\r\n".encode())
                self.wfile.write(jpg)
                i += 1
                time.sleep(0.2)
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError):
            pass


class ThreadedServer(socketserver.ThreadingMixIn, http.server.HTTPServer):
    daemon_threads = True
    allow_reuse_address = True


def serve(handler, ports):
    for port in ports:
        try:
            srv = ThreadedServer(("0.0.0.0", port), handler)
        except OSError as e:
            print(f"  port {port} 無法使用（{e.strerror}），改試下一個")
            continue
        threading.Thread(target=srv.serve_forever, daemon=True).start()
        return port
    return None


# ── 推送給系統端伺服器 ────────────────────────────────────────────
def post(path, obj):
    req = urllib.request.Request(SERVER + path, data=json.dumps(obj).encode(),
                                 headers={"Content-Type": "application/json"}, method="POST")
    with urllib.request.urlopen(req, timeout=3) as r:
        return r.status


def pusher():
    warned = False
    while True:
        s = snapshot()
        try:
            post("/api/demo", {})
            post("/api/data", {
                "roll": s["roll"], "pitch": s["pitch"], "gx": 0.0, "gy": 0.0, "gz": 0.0,
                "accelX": 0.02, "accelY": 0.01, "accelZ": s["g"], "accelEvent": s["event"],
                "lat": s["loc"]["lat"], "lon": s["loc"]["lon"], "alt": 20.0, "speed": s["loc"]["speed"],
            })
            warned = False
        except Exception as e:
            if not warned:
                print(f"[伺服器] 連不到 {SERVER}（{e}）。請先執行 start_server.bat，會自動重試。")
                warned = True
        time.sleep(5)


def report_fallen(level):
    s = snapshot()
    try:
        post("/api/fallen", {"lat": s["lat"], "lon": s["lon"],
                             "device_ts": int(time.time()), "level": level})
        print(f"[倒車] 已送出 {'緊急事故' if level == 'emergency' else '倒車通知'}（系統端「倒車事件」查看）")
    except Exception as e:
        print(f"[倒車] 送出失敗：{e}")


def main():
    load_frames()
    print("══════ SafeWay 模擬車機（展場備案）══════")
    api_port = serve(ApiHandler, [80, 8080])
    cam_port = serve(StreamHandler, [81])
    if api_port is None:
        print("[錯誤] port 80 和 8080 都被佔用，無法啟動。")
        sys.exit(1)
    print(f"  APP API ：port {api_port}" + ("" if api_port == 80 else
          "（APP 要用「手動輸入 IP」，填 192.168.137.1:8080）"))
    print(f"  鏡頭串流：{'port 81，' + str(len(FRAMES)) + ' 張照片輪播' if cam_port else '無法啟動（port 81 被佔用）'}")
    print(f"  系統端  ：{SERVER}")
    print()
    print("手機連組員筆電熱點後，APP 按「自動搜尋裝置」即可連上。")
    print("指令：b=急煞  c=碰撞  f=倒車通知  e=緊急事故  p=暫停/繼續  q=結束")
    threading.Thread(target=pusher, daemon=True).start()

    while True:
        try:
            cmd = input("> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            break
        if cmd == "b":
            trigger("BRAKE", 3); print("[事件] 急煞")
        elif cmd == "c":
            trigger("COLLISION", 3); print("[事件] 碰撞，警示燈鎖定（APP 按「解除警示」解除）")
        elif cmd == "f":
            report_fallen("notice")
        elif cmd == "e":
            report_fallen("emergency")
        elif cmd == "p":
            with lock:
                if state["paused_at"] is None:
                    state["paused_at"] = time.time(); print("[暫停] 停止移動")
                else:
                    state["paused_total"] += time.time() - state["paused_at"]
                    state["paused_at"] = None; print("[繼續] 恢復移動")
        elif cmd == "q":
            break
        elif cmd:
            print("指令：b=急煞  c=碰撞  f=倒車通知  e=緊急事故  p=暫停/繼續  q=結束")


if __name__ == "__main__":
    main()
