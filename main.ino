#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include "driver/i2s.h"

// ==========================================
// 1. Wi-Fi Configuration (CHANGE THESE!)
// ==========================================
const char* ssid     = "YOUR_HOTSPOT_SSID";       
const char* password = "YOUR_HOTSPOT_PASSWORD";   

WebServer server(80);

// ==========================================
// 2. Hardware Pin Definitions
// ==========================================
const int PIN_IN1 = 14;
const int PIN_IN2 = 27;
const int PIN_IN3 = 26;
const int PIN_IN4 = 25;

const int PIN_TRIG = 5;
const int PIN_ECHO = 18;

const int PIN_SERVO_LEFT  = 13;
const int PIN_SERVO_RIGHT = 12;

#define I2S_BCLK_PIN  4
#define I2S_LRC_PIN   2
#define I2S_DIN_PIN   15

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

Servo leftArm;
Servo rightArm;

// ==========================================
// 3. Dynamic State Variables
// ==========================================
volatile bool isSpeaking = false;
long currentDistanceCm = 400;
unsigned long lastSensorRead = 0;
unsigned long lastAnimUpdate = 0; // NEW: Timer to prevent OLED from blocking audio

int consecutiveDetects = 0; 
unsigned long personDetectedStartTime = 0;
bool personGreeted = false;
unsigned long greetCooldownTimer = 0;
volatile bool pendingPhoneGreeting = false;

// ==========================================
// 4. I2S Sound Synthesis (MAX98357A)
// ==========================================
void initI2S() {
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 16,    
    .dma_buf_len = 1024,    
    .use_apll = false,
    .tx_desc_auto_clear = true
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK_PIN,
    .ws_io_num = I2S_LRC_PIN,
    .data_out_num = I2S_DIN_PIN,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
}

void playChime(int freq, int durationMs) {
  const int sampleRate = 16000;
  int samples = (sampleRate * durationMs) / 1000;
  int16_t buffer[128];
  size_t bytes_written;
  int bufIdx = 0;

  for (int i = 0; i < samples; i++) {
    int16_t sample = (int16_t)(sin(2 * PI * freq * i / sampleRate) * 7500);
    buffer[bufIdx++] = sample;
    buffer[bufIdx++] = sample;
    if (bufIdx >= 128) {
      i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
      bufIdx = 0;
    }
  }
}

void playGreetingBeep() {
  playChime(659, 90);
  delay(30);
  playChime(880, 150);
}

// ==========================================
// 5. OLED Face Expressions
// ==========================================
void drawIdleFace() {
  display.clearDisplay();
  display.fillRoundRect(28, 16, 20, 26, 6, SSD1306_WHITE);
  display.fillRoundRect(80, 16, 20, 26, 6, SSD1306_WHITE);
  display.drawFastHLine(52, 52, 24, SSD1306_WHITE);
  display.display();
}

void drawSurprisedFace() {
  display.clearDisplay();
  display.fillCircle(38, 28, 14, SSD1306_WHITE);
  display.fillCircle(90, 28, 14, SSD1306_WHITE);
  display.fillCircle(38, 28, 5, SSD1306_BLACK);
  display.fillCircle(90, 28, 5, SSD1306_BLACK);
  display.fillCircle(64, 52, 8, SSD1306_WHITE);
  display.display();
}

void drawTalkingFace(int step) {
  display.clearDisplay();
  display.fillRoundRect(28, 16, 20, 26, 6, SSD1306_WHITE);
  display.fillRoundRect(80, 16, 20, 26, 6, SSD1306_WHITE);
  int mouthH = (step % 2 == 0) ? 14 : 4;
  int mouthY = (step % 2 == 0) ? 46 : 51;
  display.fillRoundRect(50, mouthY, 28, mouthH, 4, SSD1306_WHITE);
  display.display();
}

// ==========================================
// 6. Motors & Distance Sensing
// ==========================================
void setMotors(int in1, int in2, int in3, int in4) {
  digitalWrite(PIN_IN1, in1);
  digitalWrite(PIN_IN2, in2);
  digitalWrite(PIN_IN3, in3);
  digitalWrite(PIN_IN4, in4);
}

void moveForward()  { setMotors(HIGH, LOW, HIGH, LOW); }
void moveBackward() { setMotors(LOW, HIGH, LOW, HIGH); }
void turnLeft()     { setMotors(LOW, HIGH, HIGH, LOW); }
void turnRight()    { setMotors(HIGH, LOW, LOW, HIGH); }
void stopMotors()   { setMotors(LOW, LOW, LOW, LOW); }

long readUltrasonicDistance() {
  digitalWrite(PIN_TRIG, LOW);
  delayMicroseconds(2);
  digitalWrite(PIN_TRIG, HIGH);
  delayMicroseconds(10);
  digitalWrite(PIN_TRIG, LOW);

  long duration = pulseIn(PIN_ECHO, HIGH, 25000);
  if (duration == 0) return 400;
  return duration * 0.034 / 2;
}

// ==========================================
// 7. HTML/JS Embedded Webpage
// ==========================================
const char INDEX_HTML[] PROGMEM = R"HTML_CONTENT(
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0, maximum-scale=1.0, user-scalable=no">
<title>Robo Science Deck</title>
<script src="https://cdnjs.cloudflare.com/ajax/libs/jszip/3.10.1/jszip.min.js"></script>
<style>
  * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; }
  body { background: #0b1120; color: #f8fafc; display: flex; flex-direction: column; align-items: center; min-height: 100vh; padding: 14px; }
  header { width: 100%; max-width: 440px; display: flex; justify-content: space-between; align-items: center; padding: 8px 0; }
  .logo { font-size: 1.15rem; font-weight: 700; color: #38bdf8; display: flex; align-items: center; gap: 6px; }
  .menu-btn { background: none; border: none; color: #f8fafc; font-size: 1.8rem; cursor: pointer; padding: 4px 8px; }

  .lang-toggle { width: 100%; max-width: 440px; display: flex; gap: 8px; margin-bottom: 12px; }
  .lang-btn { flex: 1; padding: 8px; border-radius: 8px; border: 1px solid #334155; background: #1e293b; color: #cbd5e1; font-weight: 600; font-size: 0.85rem; cursor: pointer; }
  .lang-btn.active { background: #0284c7; color: #fff; border-color: #38bdf8; }

  .panel { width: 100%; max-width: 440px; background: #1e293b; border-radius: 14px; padding: 14px; margin-bottom: 14px; border: 1px solid #334155; }
  .panel-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px; }
  h2 { font-size: 0.8rem; text-transform: uppercase; color: #94a3b8; letter-spacing: 0.5px; }
  .counter { font-size: 0.75rem; color: #38bdf8; font-weight: 600; }

  select.cat-select { width: 100%; padding: 10px; border-radius: 8px; background: #0f172a; border: 1px solid #475569; color: #f1f5f9; font-size: 0.9rem; margin-bottom: 10px; }

  .q-list { display: flex; flex-direction: column; gap: 10px; max-height: 280px; overflow-y: auto; }
  .q-btn { background: #334155; border: 1px solid #475569; color: #f1f5f9; padding: 14px 12px; border-radius: 10px; text-align: left; cursor: pointer; display: flex; justify-content: space-between; align-items: center; }
  .q-btn:active { background: #0369a1; }
  .tag { font-size: 0.7rem; border-radius: 4px; padding: 4px 8px; font-weight:bold; }
  .tag.loaded { background: #059669; color: #ecfdf5; }
  .tag.unloaded { background: #475569; color: #cbd5e1; }

  .d-pad { display: grid; grid-template-columns: repeat(3, 1fr); gap: 10px; width: 210px; margin: 0 auto; }
  .pad-btn { background: #0284c7; border: none; color: white; border-radius: 12px; font-size: 1.5rem; padding: 16px 0; font-weight: bold; cursor: pointer; user-select: none; }
  .pad-btn:active { background: #0369a1; transform: scale(0.96); }
  .pad-btn.stop { background: #ef4444; }

  .modal { display: none; position: fixed; top: 0; left: 0; width: 100%; height: 100%; background: rgba(0,0,0,0.8); backdrop-filter: blur(4px); align-items: center; justify-content: center; z-index: 100; }
  .modal-content { background: #1e293b; width: 90%; max-width: 400px; border-radius: 16px; padding: 20px; }
  .modal-header { display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px; }
  .close-btn { background: none; border: none; color: #94a3b8; font-size: 1.6rem; cursor: pointer; }
  .file-drop { border: 2px dashed #0284c7; background: #0f172a; padding: 18px; border-radius: 12px; text-align: center; cursor: pointer; margin-bottom: 12px; display: block; }
  .file-drop input { display: none; }
  .progress-txt { font-size: 0.8rem; color: #38bdf8; margin-top: 8px; }
</style>
</head>
<body>

<header>
  <div class="logo">&#129302; SCIENCE ROBOT</div>
  <button class="menu-btn" onclick="openModal()">&#8942;</button>
</header>

<div class="lang-toggle">
  <button class="lang-btn active" id="btn-en" onclick="setLang('en')">English (EN)</button>
  <button class="lang-btn" id="btn-hi" onclick="setLang('hi')">&#2361;&#2367;&#2306;&#2342;&#2368; (HI)</button>
</div>

<div class="panel">
  <div class="panel-header">
    <h2>Speech & Responses</h2>
    <span class="counter" id="status-counter">0 Loaded</span>
  </div>

  <select class="cat-select" id="catFilter" onchange="renderList()">
    <option value="all">All Categories</option>
    <option value="greeting">Greetings & Welcome</option>
    <option value="project">Project Overview</option>
    <option value="science">General Science</option>
    <option value="physics">Physics</option>
    <option value="space">Space</option>
    <option value="robotics">Robotics</option>
    <option value="joke">Jokes</option>
  </select>

  <div class="q-list" id="qList"></div>
</div>

<div class="panel">
  <h2>Drive Control</h2>
  <div class="d-pad">
    <div></div>
    <button class="pad-btn" onpointerdown="sendMove('F')" onpointerup="sendMove('S')">&#9650;</button>
    <div></div>
    <button class="pad-btn" onpointerdown="sendMove('L')" onpointerup="sendMove('S')">&#9664;</button>
    <button class="pad-btn stop" onclick="sendMove('S')">&#9632;</button>
    <button class="pad-btn" onpointerdown="sendMove('R')" onpointerup="sendMove('S')">&#9654;</button>
    <div></div>
    <button class="pad-btn" onpointerdown="sendMove('B')" onpointerup="sendMove('S')">&#9660;</button>
    <div></div>
  </div>
</div>

<div class="modal" id="configModal">
  <div class="modal-content">
    <div class="modal-header">
      <h3 style="font-size: 1.05rem;">Upload ZIP to Phone</h3>
      <button class="close-btn" onclick="closeModal()">&times;</button>
    </div>
    <p style="font-size: 0.85rem; color: #94a3b8; margin-bottom: 12px; line-height:1.4;">
      Select the ZIP file of your audio. The phone will process it and <b>stream the audio live to the robot's physical speaker!</b>
    </p>
    <label class="file-drop">
      <span style="font-size: 1.8rem;">&#128230;</span><br>
      <span style="font-size: 0.85rem; color: #cbd5e1;">Tap to Select Audio .ZIP</span>
      <input type="file" accept=".zip" onchange="handleZip(this)">
    </label>
    <div class="progress-txt" id="zipProgress">Waiting for ZIP file...</div>
  </div>
</div>

<script>
  var currentLang = 'en';
  var audioBlobs = {};
  var audioCtx = null;

  var dialogues = [
    { id: "D001", cat: "greeting", en_file: "d001_en.wav", hi_file: "d001_hi.wav", en: "Hello! Welcome to our science exhibition.", hi: "नमस्ते! हमारी विज्ञान प्रदर्शनी में आपका स्वागत है।" },
    { id: "D002", cat: "greeting", en_file: "d002_en.wav", hi_file: "d002_hi.wav", en: "Namaste! What would you like to explore today?", hi: "नमस्ते! आज आप क्या जानना चाहेंगे?" },
    { id: "D006", cat: "greeting", en_file: "d006_en.wav", hi_file: "d006_hi.wav", en: "Who are you?", hi: "मैं विज्ञान प्रदर्शनी का रोबोट सहायक हूँ।" },
    { id: "D015", cat: "project",  en_file: "d015_en.wav", hi_file: "d015_hi.wav", en: "How was this robot built?", hi: "इस रोबोट को कैसे बनाया गया है?" },
    { id: "D016", cat: "project",  en_file: "d016_en.wav", hi_file: "d016_hi.wav", en: "What microcontroller powers you?", hi: "आपके अंदर कौन सा माइक्रोकंट्रोलर है?" },
    { id: "D027", cat: "science",  en_file: "d027_en.wav", hi_file: "d027_hi.wav", en: "What is Science?", hi: "विज्ञान क्या है?" },
    { id: "D048", cat: "physics",  en_file: "d048_en.wav", hi_file: "d048_hi.wav", en: "Explain Newton's Third Law.", hi: "न्यूटन का तीसरा नियम समझाइए।" },
    { id: "D059", cat: "space",    en_file: "d059_en.wav", hi_file: "d059_hi.wav", en: "Tell me about the Solar System.", hi: "सौर मंडल के बारे में बताइए।" },
    { id: "D070", cat: "robotics", en_file: "d070_en.wav", hi_file: "d070_hi.wav", en: "How do your servos work?", hi: "आपके सर्वो हाथ कैसे काम करते हैं?" },
    { id: "D079", cat: "joke",     en_file: "d079_en.wav", hi_file: "d079_hi.wav", en: "Tell a science joke!", hi: "एक विज्ञान का चुटकुला सुनाओ!" }
  ];

  var openModal = function() { document.getElementById('configModal').style.display = 'flex'; };
  var closeModal = function() { document.getElementById('configModal').style.display = 'none'; };

  var initAudioCtx = function() {
    if (!audioCtx) audioCtx = new (window.AudioContext || window.webkitAudioContext)();
    if (audioCtx.state === 'suspended') audioCtx.resume();
  };

  var setLang = function(lang) {
    currentLang = lang;
    document.getElementById('btn-en').className = 'lang-btn ' + (lang === 'en' ? 'active' : '');
    document.getElementById('btn-hi').className = 'lang-btn ' + (lang === 'hi' ? 'active' : '');
    renderList();
  };

  var renderList = function() {
    var list = document.getElementById('qList');
    var selectedCat = document.getElementById('catFilter').value;
    list.innerHTML = '';

    dialogues.forEach(function(item) {
      if (selectedCat !== 'all' && item.cat !== selectedCat) return;

      var filename = (currentLang === 'en' ? item.en_file : item.hi_file).toLowerCase();
      var isLoaded = !!audioBlobs[filename];
      var title = currentLang === 'en' ? item.en : item.hi;

      var btn = document.createElement('button');
      btn.className = 'q-btn';
      btn.onclick = function() { playDialogue(item); };
      
      btn.innerHTML = '<div style="flex:1; padding-right:12px;">' +
                        '<div style="font-weight:600; font-size:1rem; margin-bottom:4px; line-height:1.3;">' + title + '</div>' +
                        '<div style="font-size:0.75rem; color:#94a3b8;">Click to play on Robot Speaker</div>' +
                      '</div>' +
                      '<span class="tag ' + (isLoaded ? 'loaded' : 'unloaded') + '">' + (isLoaded ? 'Stream Ready' : 'Need ZIP') + '</span>';
      list.appendChild(btn);
    });

    document.getElementById('status-counter').innerText = Object.keys(audioBlobs).length + ' Tracks Ready';
  };

  var handleZip = function(input) {
    if (!input.files || !input.files[0]) return;
    initAudioCtx();
    var file = input.files[0];
    var progress = document.getElementById('zipProgress');
    progress.innerText = 'Extracting ZIP...';

    JSZip.loadAsync(file).then(function(zip) {
      var count = 0;
      var promises = [];
      var fileNames = Object.keys(zip.files);

      fileNames.forEach(function(filename) {
        var zipEntry = zip.files[filename];
        if (!zipEntry.dir && (filename.endsWith('.wav') || filename.endsWith('.mp3'))) {
          var p = zipEntry.async('blob').then(function(blob) {
            var cleanName = filename.split('/').pop().toLowerCase();
            audioBlobs[cleanName] = URL.createObjectURL(blob);
            count++;
            progress.innerText = 'Extracted ' + count + ' files...';
          });
          promises.push(p);
        }
      });

      Promise.all(promises).then(function() {
        progress.innerText = 'Success! Phone is ready to stream audio.';
        renderList();
        setTimeout(closeModal, 1500);
      });
    }).catch(function(err) {
      alert('ZIP Error: ' + err.message);
    });
  };

  var streamAudioToRobot = function(blobUrl) {
    initAudioCtx();
    fetch(blobUrl)
      .then(function(res) { return res.arrayBuffer(); })
      .then(function(arrayBuffer) { return audioCtx.decodeAudioData(arrayBuffer); })
      .then(function(decodedData) {
        var offlineCtx = new OfflineAudioContext(1, decodedData.duration * 16000, 16000);
        var source = offlineCtx.createBufferSource();
        source.buffer = decodedData;
        source.connect(offlineCtx.destination);
        source.start();
        return offlineCtx.startRendering();
      })
      .then(function(resampled) {
        var float32Data = resampled.getChannelData(0);
        var int16Data = new Int16Array(float32Data.length * 2);
        
        for (var i = 0; i < float32Data.length; i++) {
          var s = Math.max(-1, Math.min(1, float32Data[i])) * 0.8; 
          var val = s < 0 ? s * 0x8000 : s * 0x7FFF;
          int16Data[i*2] = val;
          int16Data[i*2+1] = val;
        }
        
        var formData = new FormData();
        formData.append("audio", new Blob([int16Data.buffer]), "audio.pcm");
        return fetch('/audio_stream', { method: 'POST', body: formData });
      })
      .catch(function(err) { console.log('Stream error:', err); });
  };

  var playDialogue = function(item) {
    var filename = (currentLang === 'en' ? item.en_file : item.hi_file).toLowerCase();
    if (audioBlobs[filename]) {
      streamAudioToRobot(audioBlobs[filename]);
    } else {
      alert("ZIP not uploaded. Using Phone TTS as fallback.");
      window.speechSynthesis.cancel();
      var text = currentLang === 'en' ? item.en : item.hi;
      var utter = new SpeechSynthesisUtterance(text);
      utter.lang = currentLang === 'en' ? 'en-US' : 'hi-IN';
      window.speechSynthesis.speak(utter);
    }
  };

  var sendMove = function(dir) { fetch('/move?dir=' + dir).catch(function() {}); };

  setInterval(function() {
    fetch('/poll_greeting')
      .then(function(res) { return res.text(); })
      .then(function(val) { if (val === 'TRIGGER') playDialogue(dialogues[0]); })
      .catch(function() {});
  }, 800);

  renderList();
</script>
</body>
</html>
)HTML_CONTENT";

// ==========================================
// 8. Server Audio Streaming & Handlers
// ==========================================
void handleAudioStream() {
  HTTPUpload& upload = server.upload();
  
  if (upload.status == UPLOAD_FILE_START) {
    isSpeaking = true;
    Serial.println("Receiving Audio Stream from Phone...");
    
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    // 1. Instantly push audio bytes to the speaker
    size_t bytes_written;
    i2s_write(I2S_NUM_0, upload.buf, upload.currentSize, &bytes_written, portMAX_DELAY);
    
    // 2. STUTTER FIX: Only redraw the face every 150ms! 
    // This prevents the OLED (which takes 80ms to draw) from starving the audio stream.
    unsigned long now = millis();
    if (now - lastAnimUpdate > 150) {
      lastAnimUpdate = now;
      int wave = (now / 250) % 2;
      leftArm.write(wave ? 135 : 55);
      rightArm.write(wave ? 55 : 135);
      int mouthStep = (now / 180) % 2;
      drawTalkingFace(mouthStep);
    }
    
  } else if (upload.status == UPLOAD_FILE_END) {
    isSpeaking = false;
    leftArm.write(90);
    rightArm.write(90);
    drawIdleFace();
    Serial.println("Audio Stream Finished.");
  }
}

void handleRoot() {
  server.send(200, "text/html", INDEX_HTML);
}

void handleMove() {
  if (server.hasArg("dir")) {
    String dir = server.arg("dir");
    if (dir == "F") {
      if (currentDistanceCm > 15) moveForward();
      else stopMotors();
    } else if (dir == "B") {
      moveBackward();
    } else if (dir == "L") {
      turnLeft();
    } else if (dir == "R") {
      turnRight();
    } else {
      stopMotors();
    }
  }
  server.send(200, "text/plain", "OK");
}

void handlePollGreeting() {
  if (pendingPhoneGreeting) {
    pendingPhoneGreeting = false;
    server.send(200, "text/plain", "TRIGGER");
  } else {
    server.send(200, "text/plain", "NONE");
  }
}

// ==========================================
// 9. Setup & Loop
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(PIN_IN1, OUTPUT);
  pinMode(PIN_IN2, OUTPUT);
  pinMode(PIN_IN3, OUTPUT);
  pinMode(PIN_IN4, OUTPUT);
  stopMotors();

  pinMode(PIN_TRIG, OUTPUT);
  pinMode(PIN_ECHO, INPUT);

  leftArm.attach(PIN_SERVO_LEFT);
  rightArm.attach(PIN_SERVO_RIGHT);
  leftArm.write(90);
  rightArm.write(90);

  // OLED ACCELERATION FIX: Overclock I2C to 400kHz to drastically reduce rendering time
  Wire.begin(21, 22);
  Wire.setClock(400000); 
  
  if (display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    drawIdleFace();
  }

  initI2S();

  WiFi.mode(WIFI_STA);       
  WiFi.disconnect(true);     
  delay(1000);

  Serial.print("Connecting to Hotspot: ");
  Serial.println(ssid);
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[ERROR] Failed to connect to hotspot! Restarting...");
    delay(2000);
    ESP.restart(); 
  }

  Serial.println("\nWi-Fi Connected!");
  Serial.print("Access Deck at: http://");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, handleRoot);
  server.on("/move", HTTP_GET, handleMove);
  server.on("/poll_greeting", HTTP_GET, handlePollGreeting);
  
  server.on("/audio_stream", HTTP_POST, []() { server.send(200, "text/plain", "OK"); }, handleAudioStream);
  
  server.begin();
}

void loop() {
  server.handleClient();

  if (!isSpeaking) {
    if (millis() - lastSensorRead > 100) {
      lastSensorRead = millis();
      currentDistanceCm = readUltrasonicDistance();

      if (currentDistanceCm <= 15 && digitalRead(PIN_IN1) == HIGH && digitalRead(PIN_IN3) == HIGH) {
        stopMotors();
      }

      if (currentDistanceCm >= 15 && currentDistanceCm <= 55) {
        consecutiveDetects++; 
        
        if (consecutiveDetects >= 5) {  
          if (!personGreeted && millis() > greetCooldownTimer) {
            personGreeted = true;
            greetCooldownTimer = millis() + 12000; 

            drawSurprisedFace();
            playGreetingBeep();

            pendingPhoneGreeting = true; 
          }
        }
      } else {
        consecutiveDetects = 0;
        
        if (millis() > greetCooldownTimer) {
          personGreeted = false;
        }
      }
    }
  }
}
