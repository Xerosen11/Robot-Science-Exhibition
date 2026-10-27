#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

// --- PHONE HOTSPOT CONFIGURATION ---
const char* ssid = "Realme 8 5G";
const char* password = "12345678";

// --- HARDWARE PINS: KEEP EXISTING WIRING ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_L_PIN 13
#define SERVO_R_PIN 4
#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DIN  19

#define I2S_PORT I2S_NUM_0
#define AUDIO_SAMPLE_RATE 16000

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
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
  #log { font-size: 12px; max-height: 180px; overflow-y: auto; color: #94a3b8; font-family: monospace; overflow-wrap: anywhere; }
  .err { color: #f87171 !important; }
</style>
</head>
<body>
  <h2>Exhibit AI Robot</h2>
  <div class="card">
    <label>Gemini API Key (saved in this browser):</label>
    <input type="password" id="apiKey" placeholder="Paste Gemini API key" onchange="saveKey()">
    <label>Robot Personality / Scenario:</label>
    <textarea id="systemPrompt" rows="2">You are a witty, friendly science exhibition robot. Answer in one short sentence without markdown. Reply in the same language the visitor uses.</textarea>
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
  let recognition = null;
  let isRecording = false;
  let isBusy = false;

  window.addEventListener('load', () => {
    const saved = localStorage.getItem('gemini_api_key');
    if (saved) document.getElementById('apiKey').value = saved;
  });

  function saveKey() {
    const k = document.getElementById('apiKey').value.trim();
    try { localStorage.setItem('gemini_api_key', k); } catch (e) {}
  }

  function log(msg, isErr = false) {
    const l = document.getElementById('log');
    const line = document.createElement('div');
    line.className = isErr ? 'err' : '';
    line.textContent = '> ' + msg;
    l.prepend(line);
  }

  // Keep proximity polling, but do not interrupt an active conversation.
  setInterval(async () => {
    if (isBusy) return;
    try {
      const res = await fetch('/status', { cache: 'no-store' });
      if (!res.ok) return;
      const data = await res.json();
      if (data.person_detected && !isBusy) {
        const key = document.getElementById('apiKey').value.trim();
        if (!key) {
          log('Person detected. Enter the Gemini API key to enable greeting.', true);
          return;
        }
        log('Person detected! Greeting visitor...');
        handlePrompt('A visitor just arrived at our science exhibit. Give them a quick one-sentence welcome.');
      }
    } catch (e) {}
  }, 1500);

  if ('webkitSpeechRecognition' in window || 'SpeechRecognition' in window) {
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    recognition = new SpeechRecognition();
    recognition.continuous = false;
    recognition.interimResults = false;
    recognition.onresult = (e) => {
      const text = e.results[0][0].transcript;
      document.getElementById('userText').value = text;
      handlePrompt(text);
    };
    recognition.onerror = (e) => log('Microphone recognition: ' + (e.error || 'unknown error'), true);
    recognition.onend = () => {
      isRecording = false;
      document.getElementById('micBtn').textContent = '🎤 Speak to Robot';
    };
  }

  function toggleMic() {
    if (!recognition) {
      alert('Speech recognition is not available in this browser. Try Chrome.');
      return;
    }
    if (!isRecording) {
      try {
        recognition.start();
        isRecording = true;
        document.getElementById('micBtn').textContent = '⏹ Listening...';
        fetch('/emotion?val=LISTENING').catch(() => {});
      } catch (e) {
        log('Could not start microphone: ' + e.message, true);
      }
    } else {
      recognition.stop();
    }
  }

  function sendUserText() {
    const text = document.getElementById('userText').value.trim();
    if (text) handlePrompt(text);
  }

  document.getElementById('userText').addEventListener('keydown', (e) => {
    if (e.key === 'Enter') sendUserText();
  });

  async function handlePrompt(promptText) {
    if (isBusy) return;
    const key = document.getElementById('apiKey').value.trim();
    const persona = document.getElementById('systemPrompt').value.trim();
    if (!key) {
      log('Missing Gemini API key. Paste it above.', true);
      return;
    }

    isBusy = true;
    document.getElementById('status').textContent = 'Thinking...';
    log('Query: ' + promptText);
    try {
      // Keep the original text-generation API and model fallback behavior.
      const models = ['gemini-2.5-flash', 'gemini-1.5-flash-latest', 'gemini-1.5-flash'];
      let reply = '';
      let lastError = '';

      for (const model of models) {
        try {
          const resp = await fetch(
            `https://generativelanguage.googleapis.com/v1beta/models/${model}:generateContent?key=${encodeURIComponent(key)}`,
            {
              method: 'POST',
              headers: { 'Content-Type': 'application/json' },
              body: JSON.stringify({
                contents: [{ parts: [{ text: `${persona}\nVisitor said: ${promptText}` }] }]
              })
            }
          );
          const data = await resp.json();
          if (!resp.ok) {
            lastError = data.error?.message || `HTTP ${resp.status}`;
            console.warn(model, lastError);
            continue;
          }
          const candidate = data.candidates?.[0]?.content?.parts
            ?.map(p => p.text || '')
            .join('');
          if (candidate) {
            reply = candidate.replace(/[*#_~]/g, '').trim();
            break;
          }
          lastError = data.error?.message || 'No text candidate returned';
        } catch (err) {
          lastError = err.message || String(err);
          console.warn('Gemini request failed:', err);
        }
      }

      if (!reply) throw new Error('Gemini text request failed: ' + lastError);
      log('AI: ' + reply);
      await speakAndSend(reply, key);
    } catch (err) {
      log(err.message || String(err), true);
      document.getElementById('status').textContent = 'Error';
      fetch('/emotion?val=IDLE').catch(() => {});
      isBusy = false;
    }
  }

  // Gemini TTS returns raw 16-bit signed PCM at the requested sample rate.
  // This avoids Google Translate audio and browser cross-origin audio decoding.
  async function speakAndSend(text, key) {
    document.getElementById('status').textContent = 'Generating speech...';
    fetch('/emotion?val=TALKING').catch(() => {});

    try {
      const resp = await fetch('https://generativelanguage.googleapis.com/v1beta/interactions', {
        method: 'POST',
        headers: {
          'Content-Type': 'application/json',
          'x-goog-api-key': key
        },
        body: JSON.stringify({
          model: 'gemini-3.8-flash-tts',
          input: [{
            type: 'user_input',
            content: [{
              type: 'text',
              text: text,
              annotations: [{
                type: 'speech_metadata',
                style: 'clear, warm, friendly conversational speech'
              }]
            }]
          }],
          response_format: {
            type: 'audio',
            mime_type: 'audio/l16',
            sample_rate: 16000
          },
          generation_config: {
            speech_config: [{ voice: 'Kore' }]
          }
        })
      });

      const data = await resp.json();
      if (!resp.ok) {
        throw new Error(data.error?.message || `Gemini TTS HTTP ${resp.status}`);
      }

      // REST response audio is found inside steps[].content[].data.
      function findAudio(obj) {
        if (!obj || typeof obj !== 'object') return null;
        if (obj.type === 'audio' && typeof obj.data === 'string') return obj.data;
        for (const value of Object.values(obj)) {
          const found = findAudio(value);
          if (found) return found;
        }
        return null;
      }

      const base64Audio = findAudio(data);
      if (!base64Audio) {
        throw new Error('Gemini TTS response contained no audio payload.');
      }

      const binary = atob(base64Audio);
      const pcm = new Uint8Array(binary.length);
      for (let i = 0; i < binary.length; i++) pcm[i] = binary.charCodeAt(i);

      if (pcm.byteLength < 2) throw new Error('Gemini returned empty audio.');
      if (pcm.byteLength > 1200000) throw new Error('Audio is too large for this upload; shorten the reply.');

      document.getElementById('status').textContent = 'Playing on robot speaker...';
      const playResp = await fetch('/audio', {
        method: 'POST',
        headers: { 'Content-Type': 'application/octet-stream' },
        body: pcm
      });
      if (!playResp.ok) {
        const msg = await playResp.text().catch(() => '');
        throw new Error(`ESP32 audio upload failed (HTTP ${playResp.status}) ${msg}`);
      }

      fetch('/emotion?val=HAPPY').catch(() => {});
      document.getElementById('status').textContent = 'Ready';
      setTimeout(() => fetch('/emotion?val=IDLE').catch(() => {}), 2500);
    } catch (err) {
      log('TTS error: ' + (err.message || String(err)), true);
      document.getElementById('status').textContent = 'Speech error';
      fetch('/emotion?val=IDLE').catch(() => {});
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
    .sample_rate = AUDIO_SAMPLE_RATE,
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

  esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
  if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
    Serial.printf("I2S driver install failed: %d\n", (int)err);
  }
  i2s_set_pin(I2S_PORT, &pin_config);
  i2s_set_clk(I2S_PORT, AUDIO_SAMPLE_RATE, I2S_BITS_PER_SAMPLE_16BIT, I2S_CHANNEL_MONO);
  i2s_zero_dma_buffer(I2S_PORT);
}

// --- OLED FACE ANIMATIONS ---
void drawFace(Emotion emo) {
  display.clearDisplay();
  const int eyeRadius = 14;
  const int lx = 36, rx = 92, cy = 30;

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
      const int mouthH = toggle ? 14 : 4;
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
  return (long)(duration * 0.034 / 2.0);
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
    } else if (val == "SURPRISED") {
      currentEmotion = SURPRISED;
      servoL.write(30);
      servoR.write(150);
    }
    emotionResetTimer = millis();
  }
  server.send(200, "text/plain", "OK");
}

// Arduino-ESP32 WebServer raw-body callback.
// The raw bytes are streamed to I2S as WebServer receives them.
void handleAudioRaw() {
  HTTPRaw &raw = server.raw();

  switch (raw.status) {
    case RAW_START:
      Serial.println("Audio upload started");
      i2s_zero_dma_buffer(I2S_PORT);
      break;

    case RAW_WRITE: {
      if (raw.currentSize == 0) break;
      size_t bytesWritten = 0;
      esp_err_t err = i2s_write(
        I2S_PORT,
        raw.buf,
        raw.currentSize,
        &bytesWritten,
        portMAX_DELAY
      );
      if (err != ESP_OK || bytesWritten != raw.currentSize) {
        Serial.printf(
          "I2S write warning: err=%d wrote=%u expected=%u\n",
          (int)err,
          (unsigned int)bytesWritten,
          (unsigned int)raw.currentSize
        );
      }
      break;
    }

    case RAW_END:
      Serial.printf("Audio upload complete: %u bytes\n", (unsigned int)raw.totalSize);
      break;

    case RAW_ABORTED:
      Serial.println("Audio upload aborted");
      break;
  }
}

void handleAudio() {
  HTTPRaw &raw = server.raw();
  if (raw.status == RAW_ABORTED) {
    server.send(400, "text/plain", "Audio body was aborted");
    return;
  }
  server.send(200, "text/plain", "Audio received");
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
  digitalWrite(TRIG_PIN, LOW);
  pinMode(ECHO_PIN, INPUT);

  Wire.begin(21, 22);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Failed");
  } else {
    display.clearDisplay();
    drawFace(IDLE);
  }

  initI2S();

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.printf("Connecting to hotspot '%s'", ssid);
  while (WiFi.status() != WL_CONNECTED) {
    delay(400);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("Connected. Robot IP: ");
  Serial.println(WiFi.localIP());

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
  // Fourth argument receives the raw binary POST body in chunks.
  server.on("/audio", HTTP_POST, handleAudio, handleAudioRaw);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
  const unsigned long now = millis();

  // Proximity detection and one greeting event per approach.
  if (now - lastDistanceCheck > 600) {
    lastDistanceCheck = now;
    const long dist = readDistanceCM();

    if (dist > 5 && dist < 50 && !personTriggered) {
      personTriggered = true;
      pendingPersonEvent = true;
      currentEmotion = SURPRISED;
      emotionResetTimer = now;
      servoL.write(30);
      servoR.write(150);
    } else if (dist >= 50 || dist <= 5) {
      personTriggered = false;
    }
  }

  // Return a surprised face to idle if no interaction follows.
  if (currentEmotion == SURPRISED && (now - emotionResetTimer > 3500)) {
    currentEmotion = IDLE;
    servoL.write(90);
    servoR.write(90);
  }

  // Refresh OLED face animation.
  if (now - lastFaceUpdate > 200) {
    lastFaceUpdate = now;
    drawFace(currentEmotion);
  }
}
