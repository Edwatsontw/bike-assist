#include "Network.h"
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

static AsyncWebServer server(80);
static AsyncWebSocket ws("/ws");

static const char HTML[] = R"rawliteral(
<!DOCTYPE html>
<html lang="zh-Hant">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,maximum-scale=1">
<title>BikeAssist</title>
<style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:system-ui,sans-serif;background:#0d0d14;color:#e0e0e0;padding:12px;min-height:100vh}
h1{text-align:center;font-size:1.1rem;color:#4fc3f7;letter-spacing:2px;margin-bottom:8px}
.status{display:flex;align-items:center;justify-content:center;gap:6px;font-size:0.75rem;color:#888;margin-bottom:14px}
.dot{width:8px;height:8px;border-radius:50%;background:#444;flex-shrink:0;transition:background .3s}
.dot.on{background:#4caf50;box-shadow:0 0 6px #4caf50}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:10px}
.card{background:#16161f;border-radius:12px;padding:12px;border:1px solid #1e1e2e}
.card h2{font-size:0.65rem;color:#4fc3f7;text-transform:uppercase;letter-spacing:1px;margin-bottom:10px}
.row{display:flex;justify-content:space-between;align-items:baseline;margin:5px 0}
.lbl{font-size:0.7rem;color:#666}
.val{font-size:1rem;font-weight:600;font-variant-numeric:tabular-nums}
.event-wrap{display:flex;align-items:center;justify-content:center;height:calc(100% - 24px)}
.event{padding:10px 6px;border-radius:8px;text-align:center;font-weight:700;font-size:0.9rem;width:100%;background:#1a1a2a;color:#aaa;transition:background .3s,color .3s}
.event.brake{background:#e65100;color:#fff}
.event.collision{background:#c62828;color:#fff;animation:blink .35s infinite}
@keyframes blink{0%,100%{opacity:1}50%{opacity:.2}}
.full{grid-column:1/-1}
.gps-grid{display:grid;grid-template-columns:1fr 1fr;gap:6px 16px;margin-bottom:10px}
.map-btn{display:block;text-align:center;padding:8px;border-radius:8px;background:#1e3a5f;color:#4fc3f7;text-decoration:none;font-size:0.8rem;margin-top:4px}
.map-btn.disabled{color:#444;background:#111;pointer-events:none}
</style>
</head>
<body>
<h1>&#x1F6B2; BikeAssist</h1>
<div class="status">
  <div class="dot" id="dot"></div>
  <span id="conn">連線中...</span>
</div>
<div class="grid">

  <div class="card">
    <h2>姿態</h2>
    <div class="row"><span class="lbl">Pitch</span><span class="val" id="pitch">--</span></div>
    <div class="row"><span class="lbl">Roll</span><span class="val" id="roll">--</span></div>
  </div>

  <div class="card">
    <h2>事件</h2>
    <div class="event-wrap">
      <div class="event" id="event">NORMAL</div>
    </div>
  </div>

  <div class="card">
    <h2>陀螺儀</h2>
    <div class="row"><span class="lbl">gX</span><span class="val" id="gx">--</span></div>
    <div class="row"><span class="lbl">gY</span><span class="val" id="gy">--</span></div>
    <div class="row"><span class="lbl">gZ</span><span class="val" id="gz">--</span></div>
  </div>

  <div class="card">
    <h2>加速度</h2>
    <div class="row"><span class="lbl">X</span><span class="val" id="ax">--</span></div>
    <div class="row"><span class="lbl">Y</span><span class="val" id="ay">--</span></div>
    <div class="row"><span class="lbl">Z</span><span class="val" id="az">--</span></div>
  </div>

  <div class="card full">
    <h2>GPS</h2>
    <div class="gps-grid">
      <div><div class="lbl">緯度</div><div class="val" id="lat">等待定位...</div></div>
      <div><div class="lbl">速度</div><div class="val" id="spd">--</div></div>
      <div><div class="lbl">經度</div><div class="val" id="lon">--</div></div>
      <div><div class="lbl">高度</div><div class="val" id="alt">--</div></div>
    </div>
    <a class="map-btn disabled" id="map" href="#" target="_blank">&#x1F4CD; 在 Google Maps 開啟</a>
  </div>

</div>
<script>
const $ = id => document.getElementById(id);
const set = (id, v) => $(id).textContent = v;

function connect() {
  const ws = new WebSocket('ws://' + location.hostname + '/ws');

  ws.onopen = () => {
    $('dot').className = 'dot on';
    $('conn').textContent = '已連線';
  };

  ws.onclose = () => {
    $('dot').className = 'dot';
    $('conn').textContent = '重新連線中...';
    setTimeout(connect, 2000);
  };

  ws.onmessage = e => {
    const d = JSON.parse(e.data);

    set('pitch', d.pitch.toFixed(1) + '\xb0');
    set('roll',  d.roll.toFixed(1)  + '\xb0');
    set('gx', d.gx.toFixed(1) + '\xb0/s');
    set('gy', d.gy.toFixed(1) + '\xb0/s');
    set('gz', d.gz.toFixed(1) + '\xb0/s');
    set('ax', d.accelX.toFixed(2) + ' G');
    set('ay', d.accelY.toFixed(2) + ' G');
    set('az', d.accelZ.toFixed(2) + ' G');

    const ev = $('event');
    const name = d.accelEvent;
    ev.textContent = name;
    ev.className = 'event' +
      (name === 'COLLISION' ? ' collision' :
       name === 'BRAKE'     ? ' brake' : '');

    if (d.lat !== 0) {
      set('lat', d.lat.toFixed(6));
      set('lon', d.lon.toFixed(6));
      set('alt', d.alt.toFixed(1) + ' m');
      set('spd', d.speed.toFixed(1) + ' km/h');
      const map = $('map');
      map.href = 'https://maps.google.com/?q=' + d.lat + ',' + d.lon;
      map.className = 'map-btn';
    }
  };
}

connect();
</script>
</body>
</html>
)rawliteral";

static void onWsEvent(AsyncWebSocket*, AsyncWebSocketClient*, AwsEventType type,
                      void*, uint8_t*, size_t) {
    if (type == WS_EVT_CONNECT)    Serial.println("[WS] 手機已連線");
    else if (type == WS_EVT_DISCONNECT) Serial.println("[WS] 手機已斷線");
}

void NetworkManager::begin(const char* apSSID, const char* apPassword) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(apSSID, apPassword);
    Serial.printf("[Network] AP 啟動  SSID: %s  IP: %s\n",
                  apSSID, WiFi.softAPIP().toString().c_str());

    ws.onEvent(onWsEvent);
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(200, "text/html; charset=utf-8", HTML);
    });

    server.begin();
    Serial.println("[Network] Web server 啟動，瀏覽器開啟 http://192.168.4.1");
}

void NetworkManager::update() {
    ws.cleanupClients();
}

void NetworkManager::broadcast(const SensorPayload& payload) {
    if (ws.count() == 0) return;

    char msg[384];
    snprintf(msg, sizeof(msg),
        "{\"roll\":%.2f,\"pitch\":%.2f,"
        "\"gx\":%.2f,\"gy\":%.2f,\"gz\":%.2f,"
        "\"accelX\":%.2f,\"accelY\":%.2f,\"accelZ\":%.2f,"
        "\"accelEvent\":\"%s\","
        "\"lat\":%.6f,\"lon\":%.6f,\"alt\":%.2f,\"speed\":%.2f}",
        payload.roll, payload.pitch,
        payload.gx, payload.gy, payload.gz,
        payload.accelX, payload.accelY, payload.accelZ,
        payload.accelEvent,
        payload.latitude, payload.longitude, payload.altitude, payload.speed);

    ws.textAll(msg);
}
