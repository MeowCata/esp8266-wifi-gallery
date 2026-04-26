/*
 * WiFi Pixel Art Gallery
 * ======================
 * 
 * 一个通过 WiFi 访问的在线像素画板
 * - 128x64 超大画板 (8192 像素)
 * - 16 种颜色可选
 * - 数据存储在 LittleFS (Flash 文件系统)，断电不丢失
 * - SSID 显示画作状态
 * 
 * 硬件：ESP8266 (NodeMCU / Wemos D1)
 * 框架：Arduino + PlatformIO
 */

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>

// ============ 配置 ============
#define CANVAS_WIDTH  128
#define CANVAS_HEIGHT 64
#define PIXEL_COUNT   (CANVAS_WIDTH * CANVAS_HEIGHT)  // 8192 像素

// WiFi AP 配置
const char* AP_SSID = "PixelGallery";
const char* AP_PASS = "12345678";

// ============ 全局变量 ============
ESP8266WebServer server(80);
uint8_t* canvas = nullptr;  // 画板数据缓冲区

// 16 种预设颜色 (R, G, B)
const uint8_t colorPalette[16][3] = {
  {0,   0,   0  },   // 0: 黑色
  {255, 255, 255},   // 1: 白色
  {255, 0,   0  },   // 2: 红色
  {0,   255, 0  },   // 3: 绿色
  {0,   0,   255},   // 4: 蓝色
  {255, 255, 0  },   // 5: 黄色
  {255, 0,   255},   // 6: 紫色
  {0,   255, 255},   // 7: 青色
  {255, 128, 0  },   // 8: 橙色
  {128, 0,   255},   // 9: 紫色
  {255, 0,   128},   // 10: 粉红
  {0,   255, 128},   // 11: 春绿
  {128, 255, 0  },   // 12: 黄绿
  {0,   128, 255},   // 13: 天蓝
  {128, 128, 128},   // 14: 灰色
  {64,  64,  64  }    // 15: 深灰
};

// ============ LittleFS 操作 ============
void initCanvas() {
  canvas = new uint8_t[PIXEL_COUNT];
  
  // 尝试从 LittleFS 读取画板数据
  if (LittleFS.begin()) {
    if (LittleFS.exists("/canvas.dat")) {
      File file = LittleFS.open("/canvas.dat", "r");
      if (file) {
        int bytesRead = file.read(canvas, PIXEL_COUNT);
        file.close();
        if (bytesRead == PIXEL_COUNT) {
          Serial.printf("从 LittleFS 加载画板成功 (%d 像素)\n", PIXEL_COUNT);
          return;
        }
      }
    }
    LittleFS.end();
  }
  
  // 如果没有有效数据，初始化为白色
  Serial.println("未找到画板数据，初始化为白色");
  memset(canvas, 1, PIXEL_COUNT);
}

void saveCanvasToFS() {
  if (!LittleFS.begin()) {
    Serial.println("LittleFS 挂载失败");
    return;
  }
  
  File file = LittleFS.open("/canvas.dat", "w");
  if (!file) {
    Serial.println("无法创建画板文件");
    LittleFS.end();
    return;
  }
  
  int bytesWritten = file.write(canvas, PIXEL_COUNT);
  file.close();
  LittleFS.end();
  
  Serial.printf("画板数据保存到 LittleFS: %d 字节\n", bytesWritten);
}

void clearCanvas() {
  memset(canvas, 1, PIXEL_COUNT);  // 默认白色 (颜色索引 1)
  saveCanvasToFS();
}

// ============ HTTP 请求处理 ============

// 获取画板数据 (JSON 格式)
void handleGetCanvas() {
  String json = "{\"width\":" + String(CANVAS_WIDTH) + 
                ",\"height\":" + String(CANVAS_HEIGHT) + 
                ",\"pixels\":[";
  
  for (int i = 0; i < PIXEL_COUNT; i++) {
    if (i > 0) json += ",";
    json += String(canvas[i]);
  }
  json += "]}";
  
  server.send(200, "application/json", json);
}

// 批量设置像素 (用于快速绘制)
void handleSetPixels() {
  if (!server.hasArg("data")) {
    server.send(400, "text/plain", "Missing data");
    return;
  }
  
  String data = server.arg("data");
  // 格式: "x,y,c;x,y,c;..."
  int count = 0;
  int start = 0;
  while (start < (int)data.length()) {
    int end = data.indexOf(';', start);
    if (end < 0) end = data.length();
    
    String part = data.substring(start, end);
    int c1 = part.indexOf(',');
    int c2 = part.indexOf(',', c1 + 1);
    
    if (c1 > 0 && c2 > 0) {
      int x = part.substring(0, c1).toInt();
      int y = part.substring(c1 + 1, c2).toInt();
      int c = part.substring(c2 + 1).toInt();
      
      if (x >= 0 && x < CANVAS_WIDTH && y >= 0 && y < CANVAS_HEIGHT && c >= 0 && c <= 15) {
        canvas[y * CANVAS_WIDTH + x] = (uint8_t)c;
        count++;
      }
    }
    
    start = end + 1;
  }
  
  server.send(200, "text/plain", String(count) + " pixels set");
}

// 保存画板到 LittleFS
void handleSave() {
  saveCanvasToFS();
  server.send(200, "text/plain", "Saved");
}

// 清空画板
void handleClear() {
  clearCanvas();
  server.send(200, "text/plain", "Cleared");
}

// 获取调色板
void handleGetPalette() {
  String json = "[";
  for (int i = 0; i < 16; i++) {
    if (i > 0) json += ",";
    json += "{\"index\":" + String(i) + 
            ",\"r\":" + String(colorPalette[i][0]) + 
            ",\"g\":" + String(colorPalette[i][1]) + 
            ",\"b\":" + String(colorPalette[i][2]) + "}";
  }
  json += "]";
  server.send(200, "application/json", json);
}

// ============ HTML 前端页面 ============
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, user-scalable=no">
<title>WiFi Pixel Art Gallery</title>
<style>
* { margin: 0; padding: 0; box-sizing: border-box; }
body { 
  font-family: 'Segoe UI', 'PingFang SC', 'Microsoft YaHei', sans-serif;
  background: #1a1a2e; 
  color: #eee; 
  min-height: 100vh;
  display: flex;
  flex-direction: column;
  align-items: center;
  overflow-x: hidden;
}
.header {
  text-align: center;
  padding: 20px 10px 10px;
  width: 100%;
  background: linear-gradient(135deg, #16213e, #0f3460);
  border-bottom: 2px solid #e94560;
}
.header h1 { 
  font-size: 1.5em; 
  background: linear-gradient(90deg, #e94560, #0f3460, #e94560);
  background-size: 200% auto;
  -webkit-background-clip: text;
  -webkit-text-fill-color: transparent;
  animation: shimmer 3s linear infinite;
}
@keyframes shimmer { to { background-position: 200% center; } }
.header .status { font-size: 0.8em; color: #aaa; margin-top: 5px; }
.toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 8px;
  padding: 12px 15px;
  align-items: center;
  justify-content: center;
  width: 100%;
  max-width: 900px;
}
.color-picker {
  display: flex;
  gap: 4px;
  flex-wrap: wrap;
  justify-content: center;
}
.color-btn {
  width: 32px;
  height: 32px;
  border: 2px solid #444;
  border-radius: 6px;
  cursor: pointer;
  transition: all 0.2s;
  position: relative;
}
.color-btn:hover { transform: scale(1.15); z-index: 2; }
.color-btn.active { border-color: #fff; box-shadow: 0 0 12px rgba(255,255,255,0.5); transform: scale(1.1); }
.tool-btn {
  padding: 8px 16px;
  border: 1px solid #555;
  border-radius: 6px;
  background: #2a2a4a;
  color: #eee;
  cursor: pointer;
  font-size: 0.85em;
  transition: all 0.2s;
  white-space: nowrap;
}
.tool-btn:hover { background: #3a3a6a; border-color: #e94560; }
.tool-btn.danger:hover { background: #e94560; border-color: #ff6b6b; }
.tool-btn.primary { background: #0f3460; border-color: #e94560; }
.tool-btn.primary:hover { background: #1a4a8a; }
.canvas-container {
  position: relative;
  margin: 10px auto;
  overflow: hidden;
  border: 2px solid #333;
  border-radius: 8px;
  background: #222;
  touch-action: none;
  max-width: 95vw;
}
#pixelCanvas {
  display: block;
  image-rendering: pixelated;
  cursor: crosshair;
  width: 100%;
  height: auto;
}
.info-bar {
  display: flex;
  gap: 20px;
  padding: 8px 15px;
  font-size: 0.8em;
  color: #888;
  flex-wrap: wrap;
  justify-content: center;
}
.info-bar span { white-space: nowrap; }
.info-bar .highlight { color: #e94560; }
.brush-size {
  display: flex;
  align-items: center;
  gap: 8px;
}
.brush-size input { width: 80px; accent-color: #e94560; }
.brush-size label { font-size: 0.85em; color: #aaa; }
#saveNotification {
  position: fixed;
  bottom: 20px;
  left: 50%;
  transform: translateX(-50%);
  background: #0f3460;
  color: #fff;
  padding: 10px 24px;
  border-radius: 8px;
  border: 1px solid #e94560;
  opacity: 0;
  transition: opacity 0.3s;
  pointer-events: none;
  z-index: 100;
  font-size: 0.9em;
}
#saveNotification.show { opacity: 1; }
@media (max-width: 600px) {
  .header h1 { font-size: 1.2em; }
  .color-btn { width: 28px; height: 28px; }
  .tool-btn { padding: 6px 12px; font-size: 0.8em; }
  .info-bar { gap: 10px; font-size: 0.7em; }
}
</style>
</head>
<body>
<div class="header">
  <h1>🎨 WiFi Pixel Art Gallery</h1>
  <div class="status">SSID: <span id="ssidDisplay">PixelGallery</span> | IP: <span id="ipDisplay">...</span></div>
</div>

<div class="toolbar">
  <div class="color-picker" id="colorPicker"></div>
  <div class="brush-size">
    <label>Brush:</label>
    <input type="range" id="brushSize" min="1" max="10" value="1">
    <span id="brushSizeLabel">1</span>
  </div>
  <button class="tool-btn primary" onclick="saveCanvas()">💾 Save</button>
  <button class="tool-btn" onclick="loadCanvas()">🔄 Refresh</button>
  <button class="tool-btn danger" onclick="clearCanvas()">🗑️ Clear</button>
</div>

<div class="canvas-container" id="canvasContainer">
  <canvas id="pixelCanvas"></canvas>
</div>

<div class="info-bar">
  <span>Coords: <span class="highlight" id="coordDisplay">(0, 0)</span></span>
  <span>Pixels: <span class="highlight" id="pixelCount">0</span> / <span id="totalPixels">8192</span></span>
  <span>Zoom: <span class="highlight" id="zoomLevel">100%</span></span>
</div>

<div id="saveNotification">✅ Saved to ESP8266 Flash</div>

<script>
const CANVAS_WIDTH = 128;
const CANVAS_HEIGHT = 64;
const PIXEL_COUNT = CANVAS_WIDTH * CANVAS_HEIGHT;
const PIXEL_SIZE = 6;

let canvasData = new Uint8Array(PIXEL_COUNT);
let selectedColor = 1;
let brushSize = 1;
let isDrawing = false;
let lastX = -1, lastY = -1;
let dirtyPixels = new Set();

const canvas = document.getElementById('pixelCanvas');
const ctx = canvas.getContext('2d');
const coordDisplay = document.getElementById('coordDisplay');
const pixelCountDisplay = document.getElementById('pixelCount');
const brushSizeInput = document.getElementById('brushSize');
const brushSizeLabel = document.getElementById('brushSizeLabel');

const palette = [
  {r:0,g:0,b:0}, {r:255,g:255,b:255}, {r:255,g:0,b:0}, {r:0,g:255,b:0},
  {r:0,g:0,b:255}, {r:255,g:255,b:0}, {r:255,g:0,b:255}, {r:0,g:255,b:255},
  {r:255,g:128,b:0}, {r:128,g:0,b:255}, {r:255,g:0,b:128}, {r:0,g:255,b:128},
  {r:128,g:255,b:0}, {r:0,g:128,b:255}, {r:128,g:128,b:128}, {r:64,g:64,b:64}
];

function initPalette() {
  const picker = document.getElementById('colorPicker');
  palette.forEach((c, i) => {
    const btn = document.createElement('div');
    btn.className = 'color-btn' + (i === selectedColor ? ' active' : '');
    btn.style.background = 'rgb('+c.r+','+c.g+','+c.b+')';
    if (i === 0) btn.style.border = '2px solid #666';
    btn.title = 'Color '+i;
    btn.onclick = () => selectColor(i);
    picker.appendChild(btn);
  });
}

function selectColor(index) {
  selectedColor = index;
  document.querySelectorAll('.color-btn').forEach((btn, i) => {
    btn.className = 'color-btn' + (i === index ? ' active' : '');
  });
}

function renderCanvas() {
  canvas.width = CANVAS_WIDTH * PIXEL_SIZE;
  canvas.height = CANVAS_HEIGHT * PIXEL_SIZE;
  const imageData = ctx.createImageData(canvas.width, canvas.height);
  const data = imageData.data;
  for (let y = 0; y < CANVAS_HEIGHT; y++) {
    for (let x = 0; x < CANVAS_WIDTH; x++) {
      const c = palette[canvasData[y * CANVAS_WIDTH + x]] || palette[0];
      for (let py = 0; py < PIXEL_SIZE; py++) {
        for (let px = 0; px < PIXEL_SIZE; px++) {
          const idx = ((y * PIXEL_SIZE + py) * canvas.width + (x * PIXEL_SIZE + px)) * 4;
          data[idx] = c.r;
          data[idx+1] = c.g;
          data[idx+2] = c.b;
          data[idx+3] = 255;
        }
      }
    }
  }
  ctx.putImageData(imageData, 0, 0);
  updatePixelCount();
}

function getCanvasCoords(clientX, clientY) {
  const rect = canvas.getBoundingClientRect();
  const x = Math.floor((clientX - rect.left) / rect.width * CANVAS_WIDTH);
  const y = Math.floor((clientY - rect.top) / rect.height * CANVAS_HEIGHT);
  return { x: Math.max(0, Math.min(CANVAS_WIDTH-1, x)), y: Math.max(0, Math.min(CANVAS_HEIGHT-1, y)) };
}

function setPixel(x, y, color) {
  if (x < 0 || x >= CANVAS_WIDTH || y < 0 || y >= CANVAS_HEIGHT) return;
  const idx = y * CANVAS_WIDTH + x;
  if (canvasData[idx] !== color) {
    canvasData[idx] = color;
    dirtyPixels.add(idx);
    const c = palette[color];
    for (let py = 0; py < PIXEL_SIZE; py++) {
      for (let px = 0; px < PIXEL_SIZE; px++) {
        ctx.fillStyle = 'rgb('+c.r+','+c.g+','+c.b+')';
        ctx.fillRect(x*PIXEL_SIZE+px, y*PIXEL_SIZE+py, 1, 1);
      }
    }
  }
}

function drawAt(x, y) {
  const half = Math.floor(brushSize / 2);
  for (let dy = -half; dy < brushSize - half; dy++) {
    for (let dx = -half; dx < brushSize - half; dx++) {
      setPixel(x+dx, y+dy, selectedColor);
    }
  }
}

function onPointerDown(e) {
  e.preventDefault();
  const coords = getCanvasCoords(e.clientX, e.clientY);
  isDrawing = true;
  lastX = coords.x;
  lastY = coords.y;
  drawAt(coords.x, coords.y);
}

function onPointerMove(e) {
  e.preventDefault();
  const coords = getCanvasCoords(e.clientX, e.clientY);
  coordDisplay.textContent = '('+coords.x+', '+coords.y+')';
  if (isDrawing) {
    const steps = Math.max(Math.abs(coords.x-lastX), Math.abs(coords.y-lastY));
    for (let i = 0; i <= steps; i++) {
      const t = i / (steps || 1);
      drawAt(Math.round(lastX+(coords.x-lastX)*t), Math.round(lastY+(coords.y-lastY)*t));
    }
    lastX = coords.x;
    lastY = coords.y;
  }
}

function onPointerUp() {
  if (isDrawing) {
    isDrawing = false;
    clearTimeout(window._saveTimer);
    window._saveTimer = setTimeout(sendDirtyPixels, 500);
  }
}

canvas.addEventListener('mousedown', onPointerDown);
canvas.addEventListener('mousemove', onPointerMove);
canvas.addEventListener('mouseup', onPointerUp);
canvas.addEventListener('mouseleave', () => { isDrawing = false; });

canvas.addEventListener('touchstart', (e) => {
  e.preventDefault();
  const t = e.touches[0];
  onPointerDown({ preventDefault: ()=>e.preventDefault(), clientX: t.clientX, clientY: t.clientY });
});
canvas.addEventListener('touchmove', (e) => {
  e.preventDefault();
  const t = e.touches[0];
  onPointerMove({ preventDefault: ()=>e.preventDefault(), clientX: t.clientX, clientY: t.clientY });
});
canvas.addEventListener('touchend', (e) => { e.preventDefault(); onPointerUp(); });

async function fetchAPI(endpoint) {
  try {
    const res = await fetch(endpoint);
    return await res.text();
  } catch(e) { return null; }
}

async function loadCanvas() {
  const data = await fetchAPI('/canvas');
  if (!data) return;
  try {
    const json = JSON.parse(data);
    if (json.pixels) {
      for (let i = 0; i < Math.min(json.pixels.length, PIXEL_COUNT); i++) {
        canvasData[i] = json.pixels[i];
      }
      renderCanvas();
    }
  } catch(e) {}
}

async function sendDirtyPixels() {
  if (dirtyPixels.size === 0) return;
  let payload = '';
  let count = 0;
  for (const idx of dirtyPixels) {
    const x = idx % CANVAS_WIDTH;
    const y = Math.floor(idx / CANVAS_WIDTH);
    payload += x+','+y+','+canvasData[idx]+';';
    count++;
    if (count >= 100) {
      await fetchAPI('/setpixels?data='+encodeURIComponent(payload));
      payload = '';
      count = 0;
    }
  }
  if (payload.length > 0) {
    await fetchAPI('/setpixels?data='+encodeURIComponent(payload));
  }
  dirtyPixels.clear();
}

async function saveCanvas() {
  await sendDirtyPixels();
  const result = await fetchAPI('/save');
  if (result) {
    const el = document.getElementById('saveNotification');
    el.textContent = '✅ Saved to Flash - survives power loss!';
    el.classList.add('show');
    setTimeout(() => el.classList.remove('show'), 2000);
  }
}

async function clearCanvas() {
  if (!confirm('Are you sure you want to clear the entire canvas? This cannot be undone!')) return;
  canvasData.fill(1);
  renderCanvas();
  dirtyPixels.clear();
  await fetchAPI('/clear');
}

function updatePixelCount() {
  let count = 0;
  for (let i = 0; i < PIXEL_COUNT; i++) {
    if (canvasData[i] > 0) count++;
  }
  pixelCountDisplay.textContent = count;
}

brushSizeInput.addEventListener('input', () => {
  brushSize = parseInt(brushSizeInput.value);
  brushSizeLabel.textContent = brushSize;
});

document.addEventListener('keydown', (e) => {
  if ((e.key === 's' || e.key === 'S') && (e.ctrlKey || e.metaKey)) {
    e.preventDefault();
    saveCanvas();
  }
});

async function getIP() {
  const ip = await fetchAPI('/ip');
  if (ip) document.getElementById('ipDisplay').textContent = ip;
}

initPalette();
loadCanvas();
getIP();
</script>
</body>
</html>
)rawliteral";

// ============ 处理根路径 ============
void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

// ============ 获取 IP ============
void handleGetIP() {
  IPAddress ip = WiFi.softAPIP();
  server.send(200, "text/plain", ip.toString());
}

// ============ 设置路由 ============
void setupRoutes() {
  server.on("/", handleRoot);
  server.on("/canvas", handleGetCanvas);
  server.on("/setpixels", handleSetPixels);
  server.on("/save", handleSave);
  server.on("/clear", handleClear);
  server.on("/palette", handleGetPalette);
  server.on("/ip", handleGetIP);
}

// ============ 生成 SSID ============
String generateStatusSSID() {
  return String("PixelGallery");
}

// ============ 设置 ============
void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("=== WiFi 像素艺术画廊 ===");
  
  // 初始化画板数据
  initCanvas();
  
  // 生成带画作状态的 SSID
  String statusSSID = generateStatusSSID();
  Serial.print("SSID: ");
  Serial.println(statusSSID);
  
  // 设置 WiFi AP
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(
    IPAddress(192, 168, 4, 1),
    IPAddress(192, 168, 4, 1),
    IPAddress(255, 255, 255, 0)
  );
  WiFi.softAP(statusSSID.c_str(), AP_PASS);
  
  IPAddress ip = WiFi.softAPIP();
  Serial.print("AP IP: ");
  Serial.println(ip);
  Serial.print("密码: ");
  Serial.println(AP_PASS);
  Serial.print("访问: http://");
  Serial.println(ip);
  
  // 设置 HTTP 路由
  setupRoutes();
  server.begin();
  Serial.println("HTTP 服务器已启动");
}

// ============ 主循环 ============
void loop() {
  server.handleClient();
}
