#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <ESP32Servo.h>

// --- Wi-Fi Configuration ---
const char* ssid = "Realme 8 5G"; 
const char* password = "12345678";        

WebServer server(80);
WebSocketsServer webSocket(81);

// --- Pin Definitions ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// --- I2S / Audio Amplifier Pins (MAX98357A / Speaker) ---
// Speaker / Amp Connections to ESP32:
// BCLK (Bit Clock)  -> GPIO 14
// LRC  (Word Select)  -> GPIO 26
// DIN  (Data In)      -> GPIO 27
// VIN / GND           -> 5V / GND
#define I2S_BCLK 14
#define I2S_LRC 26
#define I2S_DOUT 27

const int TRIG_PIN = 32;
const int ECHO_PIN = 33;

Servo leftArm;
Servo rightArm;
const int LEFT_SERVO_PIN = 18;
const int RIGHT_SERVO_PIN = 19;

unsigned long lastSensorCheck = 0;
unsigned long lastActivityTime = 0;
const unsigned long IDLE_TIMEOUT = 15000; // 15 seconds for screen saver
bool isScreenSaverActive = false;

// --- State Machine Variables (Non-Blocking / Interruptible) ---
String currentEmotion = "NEUTRAL";
String currentText = "Haston Online.";
bool isActing = false;

// --- Function Prototypes ---
void displayScreen(String emotion, String text);
void setArms(int leftAngle, int rightAngle);
void executeAction(String emotion, String text);
void runScreenSaver();
long getDistance();
void playBeep(int frequency, int duration_ms);
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length);

// --- Web UI HTML (Gemini API Integration + Haston Control Panel) ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Haston Companion - Gemini AI</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 20px; background-color: #1a1a1a; color: #fff; }
    button, input, textarea { padding: 10px; font-size: 16px; margin: 6px; border-radius: 8px; border: none; cursor: pointer; }
    button { background-color: #00adb5; color: white; transition: 0.2s; font-weight: bold; }
    button:active { background-color: #007e85; }
    input[type="text"], input[type="password"], textarea { width: 80%; max-width: 320px; padding: 10px; background-color: #2b2b2b; color: #fff; border: 1px solid #444; border-radius: 8px; }
    .btn-grid { max-width: 420px; margin: 0 auto; display: flex; flex-wrap: wrap; justify-content: center; }
    .chat-box { max-width: 400px; margin: 15px auto; background: #2b2b2b; padding: 15px; border-radius: 12px; text-align: left; min-height: 80px; max-height: 150px; overflow-y: auto; border: 1px solid #444; font-size: 14px; }
    .chat-msg { margin-bottom: 8px; line-height: 1.4; }
    .haston-tag { color: #00adb5; font-weight: bold; }
    .config-panel { background: #222; padding: 10px; border-radius: 10px; max-width: 350px; margin: 0 auto 15px auto; border: 1px solid #333; }
  </style>
</head>
<body>
  <h1>😎 Haston Companion</h1>
  <p style="color: #aaa; margin-top: -10px;">Powered by ESP32 & Gemini API</p>
  
  <div class="config-panel">
    <input type="password" id="apiKey" placeholder="Enter Gemini API Key...">
  </div>

  <textarea id="chatbox" rows="2" placeholder="Ask Gemini AI or command Haston..."></textarea>
  <br>
  <button style="background-color: #ff5722;" onclick="askGeminiAI()">✨ Ask Gemini AI</button>
  
  <div class="chat-box" id="chatLog">
    <div class="chat-msg"><span class="haston-tag">Haston:</span> Systems online. Hit me up or use the buttons below! 😎</div>
  </div>

  <div class="btn-grid">
    <button onclick="sendMsg('HAPPY', 'Feeling good!')">😊 Happy</button>
    <button onclick="sendMsg('ALERT', 'Watch out!')">⚠️ Alert</button>
    <button onclick="sendMsg('PANIC', 'Whoa!')">⚡ Panic</button>
    <button onclick="sendMsg('CURIOUS', 'Hmm...')">🤔 Curious</button>
    <button onclick="sendMsg('CONFUSED', 'Wait what?')">💫 Confused</button>
    <button onclick="sendMsg('PARTY', 'Let us jam!')">🎉 Party</button>
    <button onclick="sendMsg('SAD', 'So sad...')">💧 Sad</button>
    <button onclick="sendMsg('SLEEPY', 'Zzz...')">😴 Sleepy</button>
    <button onclick="sendMsg('REST', 'Resting.')">🛑 Rest</button>
  </div>

  <script>
    var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);
    
    function sendMsg(emotion, text) {
      if(connection.readyState === WebSocket.OPEN){
        connection.send(emotion + "|" + text);
      }
    }

    async function askGeminiAI() {
      var key = document.getElementById('apiKey').value.trim();
      var inputField = document.getElementById('chatbox');
      var query = inputField.value.trim();
      if(query === "") return;

      appendChat("You", query);
      inputField.value = "";

      if(!key) {
        appendChat("Haston", "Drop your Gemini API key in the box above first, dawg!");
        sendMsg("CONFUSED", "No API Key!");
        return;
      }

      appendChat("Haston", "Connecting to Gemini matrix... 🤖✨");
      sendMsg("CURIOUS", "Thinking...");

      try {
        const response = await fetch(`https://generativelanguage.googleapis.com/v1beta/models/gemini-1.5-flash:generateContent?key=${key}`, {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({
            contents: [{
              parts: [{ text: "Respond as Haston, a super chill, friendly, and cool open-source robot companion. Keep the response under 25 words so it fits on a tiny OLED screen: " + query }]
            }]
          })
        });

        const data = await response.json();
        if(data.candidates && data.candidates[0].content.parts[0].text) {
          let answer = data.candidates[0].content.parts[0].text.trim();
          updateLastChat("Haston: " + answer);
          let shortText = answer.length > 28 ? answer.substring(0, 25) + "..." : answer;
          sendMsg("HAPPY", shortText);
        } else {
          throw new Error("Invalid response format");
        }
      } catch (error) {
        let errReply = "API glitch! Check your key or network.";
        updateLastChat("Haston: " + errReply);
        sendMsg("CONFUSED", "API Error!");
      }
    }

    function appendChat(sender, message) {
      var log = document.getElementById('chatLog');
      log.innerHTML += `<div class="chat-msg"><span class="haston-tag">${sender}:</span> ${message}</div>`;
      log.scrollTop = log.scrollHeight;
    }

    function updateLastChat(message) {
      var log = document.getElementById('chatLog');
      var msgs = log.getElementsByClassName('chat-msg');
      if(msgs.length > 0) {
        msgs[msgs.length - 1].innerHTML = `<span class="haston-tag">Haston:</span> ${message.replace("Haston: ", "")}`;
      }
      log.scrollTop = log.scrollHeight;
    }
  </script>
</body>
</html>
)rawliteral";

// --- WebSocket Event Handler (Interruptible / Instant Execution) ---
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if(type == WStype_TEXT) {
    String data = (char*)payload;
    int delimiter = data.indexOf('|');
    if (delimiter > 0) {
      currentEmotion = data.substring(0, delimiter);
      currentText = data.substring(delimiter + 1);
    } else {
      currentEmotion = data;
      currentText = "";
    }
    isActing = true;
    lastActivityTime = millis();
    isScreenSaverActive = false;
  }
}

void setup() {
  Serial.begin(115200);

  // 1. Wi-Fi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting");
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nIP: " + WiFi.localIP().toString());

  // 2. Web & WebSockets
  server.on("/", []() { server.send(200, "text/html", index_html); });
  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // 3. OLED
  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  displayScreen("NEUTRAL", "Haston Online.");
  
  // 4. I2S Audio (MAX98357A Amp & Speaker)
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
    .sample_rate = 16000,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format = I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_BCLK,
    .ws_io_num = I2S_LRC,
    .data_out_num = I2S_DOUT,
    .data_in_num = I2S_PIN_NO_CHANGE
  };
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  i2s_zero_dma_buffer(I2S_NUM_0);

  // 5. Servos & Sensor
  ESP32PWM::allocateTimer(0);
  ESP32PWM::allocateTimer(1);
  leftArm.setPeriodHertz(50);
  rightArm.setPeriodHertz(50);
  leftArm.attach(LEFT_SERVO_PIN, 500, 2400);
  rightArm.attach(RIGHT_SERVO_PIN, 500, 2400);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  
  setArms(10, 170); // Idle
  lastActivityTime = millis();
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Instant / Interruptible Action Execution
  if (isActing) {
    executeAction(currentEmotion, currentText);
    isActing = false;
    lastActivityTime = millis();
    isScreenSaverActive = false;
  }

  // Screen Saver Trigger
  if (!isActing && !isScreenSaverActive && (millis() - lastActivityTime > IDLE_TIMEOUT)) {
    isScreenSaverActive = true;
  }

  if (isScreenSaverActive && !isActing) {
    runScreenSaver();
  }

  // Sensor Logic
  if (!isActing && !isScreenSaverActive && (millis() - lastSensorCheck > 200)) {
    long distance = getDistance();
    if (distance > 0 && distance < 15) {
      currentEmotion = "PANIC";
      currentText = "Whoa! Too close!";
      isActing = true;
    } else if (distance >= 30 && distance <= 70) {
      currentEmotion = "HAPPY";
      currentText = "Oh, hi there!";
      isActing = true;
    }
    lastSensorCheck = millis();
  }
}

// --- Audio Beep Synthesizer ---
void playBeep(int frequency, int duration_ms) {
  int sample_rate = 16000;
  int samples = (sample_rate * duration_ms) / 1000;
  int half_period = sample_rate / frequency / 2;
  int16_t sample_val = 3000; 
  int16_t buffer[64];
  size_t bytes_written;
  
  for(int i=0; i<samples; ) {
    for(int j=0; j<64 && i<samples; j++, i++) {
      buffer[j] = ((i / half_period) % 2 == 0) ? sample_val : -sample_val;
    }
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
  }
  
  memset(buffer, 0, sizeof(buffer));
  for(int k = 0; k < 8; k++) {
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// --- Immediate Action Executor ---
void executeAction(String emotion, String text) {
  displayScreen(emotion, text);
  
  if (emotion == "HAPPY") {
    leftArm.write(150); 
    playBeep(880, 100); delay(50); playBeep(1100, 150); 
    rightArm.write(20); delay(200);
    rightArm.write(60); delay(200);
    rightArm.write(20); delay(200);
    setArms(10, 170);
  } 
  else if (emotion == "ALERT") {
    setArms(45, 170);
    playBeep(600, 200);
  } 
  else if (emotion == "PANIC") {
    for(int i=0; i<2; i++) {
      setArms(150, 30);
      playBeep(1200, 100);
      setArms(10, 170);
      playBeep(400, 100);
    }
  } 
  else if (emotion == "CURIOUS") {
    setArms(80, 110);
    playBeep(750, 120); delay(80); playBeep(950, 150);
  }
  else if (emotion == "CONFUSED") {
    setArms(120, 60);
    playBeep(500, 150); delay(100); playBeep(350, 200);
  }
  else if (emotion == "PARTY") {
    for(int i=0; i<3; i++) {
      setArms(150, 30); playBeep(900, 80);
      setArms(30, 150); playBeep(1300, 80);
    }
    setArms(10, 170);
  }
  else if (emotion == "SAD") {
    setArms(90, 90); 
    playBeep(400, 200); delay(100); playBeep(300, 300);
  }
  else if (emotion == "SLEEPY") {
    setArms(10, 170);
    playBeep(300, 300); delay(150); playBeep(200, 400);
  }
  else if (emotion == "REST") {
    setArms(10, 170);
  }
  
  // Hold expression for 12 seconds unless interrupted by new WebSocket command
  unsigned long startWait = millis();
  while (millis() - startWait < 12000) {
    server.handleClient();
    webSocket.loop();
    if (isActing) break; // Cuts off immediately if a new command arrives
    delay(50);
  }
  
  if (!isActing) {
    displayScreen("NEUTRAL", "");
  }
}

// --- Fixed OLED Graphic Expressions Handler ---
void displayScreen(String emotion, String text) {
  display.clearDisplay();
  int yOffset = -2; 
  
  if (emotion == "NEUTRAL") {
    display.fillRoundRect(22, 18+yOffset, 26, 22, 4, SSD1306_WHITE);
    display.fillRoundRect(80, 18+yOffset, 26, 22, 4, SSD1306_WHITE);
  } 
  else if (emotion == "HAPPY") {
    display.drawCircle(35, 28+yOffset, 14, SSD1306_WHITE);
    display.fillRect(20, 28+yOffset, 30, 16, SSD1306_BLACK); 
    display.drawCircle(93, 28+yOffset, 14, SSD1306_WHITE);
    display.fillRect(78, 28+yOffset, 30, 16, SSD1306_BLACK); 
  } 
  else if (emotion == "PANIC") {
    int jitterX = random(-2, 3); int jitterY = random(-2, 3);
    display.fillCircle(35 + jitterX, 28 + jitterY + yOffset, 11, SSD1306_WHITE);
    display.fillCircle(93 + jitterX, 28 + jitterY + yOffset, 11, SSD1306_WHITE);
  } 
  else if (emotion == "ALERT") {
    display.fillCircle(28, 28+yOffset, 13, SSD1306_WHITE);
    display.fillCircle(100, 28+yOffset, 13, SSD1306_WHITE);
    display.fillCircle(32, 28+yOffset, 4, SSD1306_BLACK); 
    display.fillCircle(96, 28+yOffset, 4, SSD1306_BLACK);
  } 
  else if (emotion == "THINKING" || emotion == "CURIOUS") {
    display.fillRoundRect(22, 24+yOffset, 26, 10, 3, SSD1306_WHITE);
    display.fillRoundRect(80, 24+yOffset, 26, 10, 3, SSD1306_WHITE);
    display.fillCircle(35, 22+yOffset, 3, SSD1306_BLACK);
  }
  else if (emotion == "CONFUSED") {
    display.fillRect(22, 28+yOffset, 26, 5, SSD1306_WHITE);
    display.fillCircle(93, 28+yOffset, 9, SSD1306_WHITE);
  }
  else if (emotion == "PARTY") {
    display.drawTriangle(35, 14+yOffset, 22, 38+yOffset, 48, 38+yOffset, SSD1306_WHITE);
    display.drawTriangle(93, 14+yOffset, 80, 38+yOffset, 106, 38+yOffset, SSD1306_WHITE);
  }
  else if (emotion == "SAD") {
    display.drawCircle(35, 32+yOffset, 11, SSD1306_WHITE);
    display.fillRect(20, 18+yOffset, 30, 10, SSD1306_BLACK); 
    display.drawCircle(93, 32+yOffset, 11, SSD1306_WHITE);
    display.fillRect(78, 18+yOffset, 30, 10, SSD1306_BLACK);
  }
  else if (emotion == "SLEEPY") {
    display.fillRect(22, 28+yOffset, 26, 3, SSD1306_WHITE);
    display.fillRect(80, 28+yOffset, 26, 3, SSD1306_WHITE);
  }

  if (text != "") {
    display.setCursor(0, 50);
    display.print(text);
  }
  display.display();
}

// --- Screen Saver Expression Cycle ---
void runScreenSaver() {
  int choice = random(0, 3);
  if (choice == 0) {
    displayScreen("SLEEPY", "");
  } else if (choice == 1) {
    displayScreen("SAD", "");
  } else {
    displayScreen("CONFUSED", "");
  }
  
  unsigned long startSS = millis();
  while (millis() - startSS < 3000) {
    server.handleClient();
    webSocket.loop();
    if (isActing) break;
    delay(50);
  }
}

void setArms(int leftAngle, int rightAngle) {
  leftArm.write(leftAngle);
  rightArm.write(rightAngle);
}

long getDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  return (pulseIn(ECHO_PIN, HIGH, 30000) * 0.034 / 2);
}
