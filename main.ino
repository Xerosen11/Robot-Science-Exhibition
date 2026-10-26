#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

// --- HOTSPOT CONFIGURATION ---
const char* ssid = "Realme 8 5G";
const char* password = "12345678";

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_L_PIN 13
#define SERVO_R_PIN 4

#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DIN  19

Servo servoL;
Servo servoR;
WebServer server(80);

enum Emotion { IDLE, LISTENING, TALKING, HAPPY, SURPRISED };
Emotion currentEmotion = IDLE;
unsigned long lastFaceUpdate = 0;
unsigned long lastDistanceCheck = 0;
unsigned long emotionResetTimer = 0;
bool personTriggered = false;
volatile bool pendingPersonEvent = false;

// --- WEB INTERFACE ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>AI Robot Controller</title>
<style>
  body { font-family: -apple-system, sans-serif; background: #0f172a; color: #fff; padding: 15px; margin: 0; }
  h2 { text-align: center; color: #38bdf8; margin: 4px 0 12px 0; }
  .card { background: #1e293b; border-radius: 12px; padding: 12px; margin-bottom: 10px; }
  input, textarea { width: 100%; box-sizing: border-box; background: #0f172a; border: 1px solid #334155; color: #fff; padding: 10px; border-radius: 8px; margin-bottom: 8px; font-size: 14px; }
  button { width: 100%; background: #0284c7; color: white; border: none; padding: 12px; border-radius: 8px; font-weight: bold; cursor: pointer; }
  button.mic { background: #16a34a; margin-bottom: 8px; }
  #log { font-size: 12px; max-height: 140px; overflow-y: auto; color: #94a3b8; font-family: monospace; }
  .err { color: #f87171 !important; }
</style>
</head>
<body>
  <h2>Exhibit AI Robot</h2>
  <div class="card">
    <label>Gemini API Key (Saved Automatically):</label>
    <input type="password" id="apiKey" placeholder="AIzaSy..." onchange="saveKey()">
    <label>Robot Personality / Scenario:</label>
    <textarea id="systemPrompt" rows="2">You are a witty, friendly science exhibition robot. Answer in 1 short sentence without asterisks.</textarea>
  </div>

  <div class="card">
    <button class="mic" id="micBtn" onclick="toggleMic()">🎤 Speak to Robot</button>
    <input type="text" id="userText" placeholder="Or type a question...">
    <button onclick="sendUserText()">Send Prompt</button>
  </div>

  <div class="card">
    <strong>Status:</strong> <span id="status" style="color:#38bdf8">Ready</span>
    <div id="log"></div>
  </div>

<script>
  let recognition, isRecording = false;
  let isBusy = false;
  const audioCtx = new (window.AudioContext || window.webkitAudioContext)({ sampleRate: 16000 });

  // Auto-restore API key from browser memory
  window.onload = () => {
    const saved = localStorage.getItem('gemini_api_key');
    if (saved) document.getElementById('apiKey').value = saved;
  };

  function saveKey() {
    const k = document.getElementById('apiKey').value.trim();
    localStorage.setItem('gemini_api_key', k);
  }

  function log(msg, isErr = false) {
    const l = document.getElementById('log');
    l.innerHTML = `<div class="${isErr ? 'err' : ''}">> ${msg}</div>` + l.innerHTML;
  }

  // Poll sensor without spamming
  setInterval(async () => {
    if (isBusy) return;
    try {
      const res = await fetch('/status');
      const data = await res.json();
      if (data.person_detected && !isBusy) {
        const key = document.getElementById('apiKey').value.trim();
        if (!key) {
          log("Enter your API key first to greet people!", true);
          return;
        }
        log("Person detected! Greeting visitor...");
        handlePrompt("A visitor just arrived at our science exhibit. Give them a quick 1-sentence welcome!");
      }
    } catch (e) {}
  }, 1500);

  if ('webkitSpeechRecognition' in window) {
    recognition = new webkitSpeechRecognition();
    recognition.continuous = false;
    recognition.interimResults = false;
    recognition.onresult = (e) => {
      const text = e.results[0][0].transcript;
      document.getElementById('userText').value = text;
      handlePrompt(text);
    };
    recognition.onend = () => {
      isRecording = false;
      document.getElementById('micBtn').innerText = "🎤 Speak to Robot";
    };
  }

  function toggleMic() {
    if (!recognition) return alert('Speech recognition requires Chrome.');
    if (!isRecording) {
      if (audioCtx.state === 'suspended') audioCtx.resume();
      recognition.start();
      isRecording = true;
      document.getElementById('micBtn').innerText = "⏹ Listening...";
      fetch('/emotion?val=LISTENING');
    } else {
      recognition.stop();
    }
  }

  function sendUserText() {
    const text = document.getElementById('userText').value.trim();
    if (text) handlePrompt(text);
  }

  async function handlePrompt(promptText) {
    if (isBusy) return;
    saveKey();
    const key = document.getElementById('apiKey').value.trim();
    const persona = document.getElementById('systemPrompt').value.trim();
    
    if (!key) {
      log("Missing Gemini API Key! Paste it above.", true);
      return;
    }

    isBusy = true;
    document.getElementById('status').innerText = 'Thinking...';
    log("Query: " + promptText);

    // Using Gemini 2.5 Flash / 1.5 Flash fallback model endpoints
    const models = ['gemini-2.5-flash', 'gemini-1.5-flash-latest', 'gemini-1.5-flash'];
    let reply = "";

    for (let model of models) {
      try {
        const resp = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${key}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            contents: [{ parts: [{ text: `${persona}\nVisitor said: ${promptText}` }] }]
          })
        });
        const data = await resp.json();
        if (data.candidates && data.candidates[0].content) {
          reply = data.candidates[0].content.parts[0].text.replace(/[*#_~]/g, '').trim();
          break;
        } else if (data.error) {
          console.warn(`Model ${model} failed:`, data.error.message);
        }
      } catch (err) {
        console.warn(`Fetch ${model} error:`, err);
      }
    }

    if (!reply) {
      log("Gemini API call failed with all model endpoints. Check API key validity or quota.", true);
      document.getElementById('status').innerText = 'Error';
      fetch('/emotion?val=IDLE'); // Release face
      isBusy = false;
      return;
    }

    log("AI: " + reply);
    await speakAndSend(reply);
  }

  async function speakAndSend(text) {
    document.getElementById('status').innerText = 'Speaking...';
    fetch('/emotion?val=TALKING');

    const url = `https://translate.google.com/translate_tts?ie=UTF-8&client=tw-ob&tl=en&q=${encodeURIComponent(text.substring(0, 180))}`;
    
    try {
      const res = await fetch(url);
      const arrayBuf = await res.arrayBuffer();
      const audioBuffer = await audioCtx.decodeAudioData(arrayBuf);
      
      const channelData = audioBuffer.getChannelData(0);
      const pcm16 = new Int16Array(channelData.length);
      for (let i = 0; i < channelData.length; i++) {
        let s = Math.max(-1, Math.min(1, channelData[i]));
        pcm16[i] = s < 0 ? s * 0x8000 : s * 0x7FFF;
      }
      
      await fetch('/audio', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: pcm16.buffer
      });

      fetch('/emotion?val=HAPPY');
      setTimeout(() => { fetch('/emotion?val=IDLE'); }, 2500);
      document.getElementById('status').innerText = 'Ready';
    } catch (e) {
      log("TTS Error: " + e.message, true);
      fetch('/emotion?val=IDLE');
    } finally {
      isBusy = false;
    }
  }
</script>
</body>
</html>
)rawliteral";

// --- I2S SETUP ---
void initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 256,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_set_clk(I2S_NUM_0, 16000, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
}

// --- OLED FACE ANIMATIONS ---
void drawFace(Emotion emo) {
  display.clearDisplay();
  int eyeRadius = 14;
  int lx = 36, rx = 92, cy = 30;

  switch (emo) {
    case IDLE:
      display.fillCircle(lx, cy, eyeRadius, SSD1306_WHITE);
      display.fillCircle(rx, cy, eyeRadius, SSD1306_WHITE);
      display.fillCircle(lx + 3, cy - 2, 4, SSD1306_BLACK);
      display.fillCircle(rx + 3, cy - 2, 4, SSD1306_BLACK);
      break;

    case LISTENING:
      display.fillCircle(lx, cy, eyeRadius + 3, SSD1306_WHITE);
      display.fillCircle(rx, cy, eyeRadius + 3, SSD1306_WHITE);
      display.fillCircle(lx, cy, 6, SSD1306_BLACK);
      display.fillCircle(rx, cy, 6, SSD1306_BLACK);
      break;

    case TALKING: {
      static bool toggle = false;
      toggle = !toggle;
      display.fillCircle(lx, cy - 4, eyeRadius - 2, SSD1306_WHITE);
      display.fillCircle(rx, cy - 4, eyeRadius - 2, SSD1306_WHITE);
      int mouthH = toggle ? 14 : 4;
      display.fillRoundRect(50, 48 - (mouthH / 2), 28, mouthH, 3, SSD1306_WHITE);
      break;
    }

    case HAPPY:
      display.fillCircle(lx, cy, eyeRadius, SSD1306_WHITE);
      display.fillCircle(rx, cy, eyeRadius, SSD1306_WHITE);
      display.fillCircle(lx, cy + 4, eyeRadius, SSD1306_BLACK);
      display.fillCircle(rx, cy + 4, eyeRadius, SSD1306_BLACK);
      display.drawRoundRect(52, 48, 24, 6, 2, SSD1306_WHITE);
      break;

    case SURPRISED:
      display.drawCircle(lx, cy, eyeRadius + 4, SSD1306_WHITE);
      display.drawCircle(rx, cy, eyeRadius + 4, SSD1306_WHITE);
      display.fillCircle(64, 48, 7, SSD1306_WHITE);
      break;
  }
  display.display();
}

long readDistanceCM() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  long duration = pulseIn(ECHO_PIN, HIGH, 20000);
  if (duration == 0) return 999;
  return duration * 0.034 / 2;
}

void handleRoot() {
  server.send_P(200, "text/html", index_html);
}

void handleStatus() {
  String json = "{\"person_detected\":";
  if (pendingPersonEvent) {
    json += "true}";
    pendingPersonEvent = false;
  } else {
    json += "false}";
  }
  server.send(200, "application/json", json);
}

void handleEmotion() {
  if (server.hasArg("val")) {
    String val = server.arg("val");
    if (val == "TALKING") {
      currentEmotion = TALKING;
      servoL.write(45);
      servoR.write(135);
    } else if (val == "HAPPY") {
      currentEmotion = HAPPY;
      servoL.write(90);
      servoR.write(90);
    } else if (val == "LISTENING") {
      currentEmotion = LISTENING;
      servoL.write(110);
      servoR.write(70);
    } else if (val == "IDLE") {
      currentEmotion = IDLE;
      servoL.write(90);
      servoR.write(90);
    }
    emotionResetTimer = millis(); // Refresh watchdog
  }
  server.send(200, "text/plain", "OK");
}

void handleAudio() {
  WiFiClient client = server.client();
  int contentLength = server.header("Content-Length").toInt();
  if (contentLength <= 0) {
    server.send(400, "text/plain", "Bad Length");
    return;
  }

  uint8_t buffer[512];
  int remaining = contentLength;
  while (remaining > 0 && client.connected()) {
    int bytesToRead = min((int)sizeof(buffer), remaining);
    int bytesRead = client.read(buffer, bytesToRead);
    if (bytesRead > 0) {
      size_t bytes_written;
      i2s_write(I2S_NUM_0, buffer, bytesRead, &bytes_written, portMAX_DELAY);
      remaining -= bytesRead;
    }
    yield();
  }
  server.send(200, "text/plain", "Done");
}

void setup() {
  Serial.begin(115200);

  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);
  ESP32PWM::allocateTimer(3);

  servoL.setPeriodHertz(50);
  servoR.setPeriodHertz(50);
  servoL.attach(SERVO_L_PIN, 500, 2400);
  servoR.attach(SERVO_R_PIN, 500, 2400);
  servoL.write(90);
  servoR.write(90);

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
  }
  display.clearDisplay();
  drawFace(IDLE);

  initI2S();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Connected!");
  display.setCursor(0, 28);
  display.println("Open in Chrome:");
  display.setCursor(0, 48);
  display.println(WiFi.localIP());
  display.display();
  delay(2500);

  server.on("/", HTTP_GET, handleRoot);
  server.on("/status", HTTP_GET, handleStatus);
  server.on("/emotion", HTTP_GET, handleEmotion);
  server.on("/audio", HTTP_POST, handleAudio);
  
  const char * headerkeys[] = {"Content-Length"};
  server.collectHeaders(headerkeys, 1);
  server.begin();
}

void loop() {
  server.handleClient();
  unsigned long now = millis();

  // Proximity Detection
  if (now - lastDistanceCheck > 600) {
    lastDistanceCheck = now;
    long dist = readDistanceCM();
    if (dist > 5 && dist < 50 && !personTriggered) {
      personTriggered = true;
      pendingPersonEvent = true;
      currentEmotion = SURPRISED;
      emotionResetTimer = now; // Start watchdog
      servoL.write(30);
      servoR.write(150);
    } else if (dist >= 50) {
      personTriggered = false;
    }
  }

  // AUTO-RESET WATCHDOG: If stuck in SURPRISED or non-idle for > 3.5s without speech, return to IDLE
  if (currentEmotion == SURPRISED && (now - emotionResetTimer > 3500)) {
    currentEmotion = IDLE;
    servoL.write(90);
    servoR.write(90);
  }

  // Expression animation
  if (now - lastFaceUpdate > 200) {
    lastFaceUpdate = now;
    drawFace(currentEmotion);
  }
}
