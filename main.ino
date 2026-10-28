#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"

// ==========================================
// 1. WI-FI HOTSPOT CREDENTIALS
// ==========================================
const char* ssid     = "YOUR_PHONE_HOTSPOT"; 
const char* password = "YOUR_HOTSPOT_PASSWORD";   

WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

// ==========================================
// 2. PIN DEFINITIONS
// ==========================================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Ultrasonic Proximity Pins
#define TRIG_PIN 5
#define ECHO_PIN 18

// L298N Motor Driver Pins
#define MOTOR_L_F 12  // IN1
#define MOTOR_L_B 14  // IN2
#define MOTOR_R_F 27  // IN3
#define MOTOR_R_B 26  // IN4

// SG90 Servo Arms
#define SERVO_LEFT_PIN  32
#define SERVO_RIGHT_PIN 33
Servo leftArm;
Servo rightArm;

// MAX98357A I2S Audio Pins (GPIO 19 for DOUT protects GPIO 22 I2C SCL)
#define I2S_BCLK 25
#define I2S_LRC  23
#define I2S_DOUT 19
#define I2S_NUM  I2S_NUM_0

// Global Control Flags
bool autonomousMode = false;
unsigned long actionStartTime = 0;
unsigned long actionDuration = 0;

// ==========================================
// 3. SOUND SYNTHESIS (MAX98357A)
// ==========================================
void playTone(uint32_t frequency, uint32_t duration_ms) {
    size_t bytes_written;
    uint32_t sample_rate = 16000;
    uint32_t half_period = sample_rate / (2 * frequency);
    
    int16_t* sample_buffer = (int16_t*)malloc(half_period * 2 * sizeof(int16_t));
    if (!sample_buffer) return;

    for (uint32_t i = 0; i < half_period * 2; i++) {
        sample_buffer[i] = (i < half_period) ? 5000 : -5000;
    }

    unsigned long start_time = millis();
    while (millis() - start_time < duration_ms) {
        i2s_write(I2S_NUM, sample_buffer, half_period * 2 * sizeof(int16_t), &bytes_written, portMAX_DELAY);
    }
    
    int16_t silence = 0;
    i2s_write(I2S_NUM, &silence, sizeof(silence), &bytes_written, portMAX_DELAY);
    free(sample_buffer);
}

void playChirp(bool positive) {
    if (positive) {
        playTone(600, 80);
        playTone(900, 100);
        playTone(1200, 120);
    } else {
        playTone(400, 100);
        playTone(250, 150);
    }
}

// ==========================================
// 4. OLED EMOTION RENDERER
// ==========================================
void drawFace(String expression) {
    display.clearDisplay();
    if (expression == "HAPPY") {
        display.fillTriangle(34, 42, 20, 24, 48, 24, SSD1306_WHITE);
        display.fillTriangle(94, 42, 80, 24, 108, 24, SSD1306_WHITE);
        display.drawRoundRect(50, 44, 28, 12, 5, SSD1306_WHITE);
    } else if (expression == "SCARED" || expression == "SURPRISED") {
        display.fillCircle(34, 28, 14, SSD1306_WHITE);
        display.fillCircle(94, 28, 14, SSD1306_WHITE);
        display.fillCircle(34, 28, 5, SSD1306_BLACK);
        display.fillCircle(94, 28, 5, SSD1306_BLACK);
        display.drawCircle(64, 48, 7, SSD1306_WHITE);
    } else if (expression == "TALK" || expression == "LISTEN") {
        display.fillRoundRect(24, 18, 22, 24, 6, SSD1306_WHITE);
        display.fillRoundRect(82, 18, 22, 24, 6, SSD1306_WHITE);
        display.fillCircle(64, 50, 6, SSD1306_WHITE);
    } else { // NORMAL / IDLE
        display.fillRoundRect(24, 18, 22, 24, 6, SSD1306_WHITE);
        display.fillRoundRect(82, 18, 22, 24, 6, SSD1306_WHITE);
        display.drawLine(50, 48, 78, 48, SSD1306_WHITE);
    }
    display.display();
}

// ==========================================
// 5. MOTOR & SERVO PRIMITIVES
// ==========================================
void stopRobot() {
    digitalWrite(MOTOR_L_F, LOW); digitalWrite(MOTOR_L_B, LOW);
    digitalWrite(MOTOR_R_F, LOW); digitalWrite(MOTOR_R_B, LOW);
}

void moveForward() {
    digitalWrite(MOTOR_L_F, HIGH); digitalWrite(MOTOR_L_B, LOW);
    digitalWrite(MOTOR_R_F, HIGH); digitalWrite(MOTOR_R_B, LOW);
}

void moveBackward() {
    digitalWrite(MOTOR_L_F, LOW);  digitalWrite(MOTOR_L_B, HIGH);
    digitalWrite(MOTOR_R_F, LOW);  digitalWrite(MOTOR_R_B, HIGH);
}

void turnLeft() {
    digitalWrite(MOTOR_L_F, LOW);  digitalWrite(MOTOR_L_B, HIGH);
    digitalWrite(MOTOR_R_F, HIGH); digitalWrite(MOTOR_R_B, LOW);
}

void turnRight() {
    digitalWrite(MOTOR_L_F, HIGH); digitalWrite(MOTOR_L_B, LOW);
    digitalWrite(MOTOR_R_F, LOW);  digitalWrite(MOTOR_R_B, HIGH);
}

void waveArms() {
    for (int i = 0; i < 2; i++) {
        leftArm.write(150); rightArm.write(30);
        delay(200);
        leftArm.write(90);  rightArm.write(90);
        delay(200);
    }
}

long getDistance() {
    digitalWrite(TRIG_PIN, LOW); delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH); delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);
    long duration = pulseIn(ECHO_PIN, HIGH, 25000);
    if (duration == 0) return 999;
    return duration * 0.034 / 2;
}

void executeCommand(String action, String expression) {
    drawFace(expression);
    playChirp(true);
    
    actionStartTime = millis();
    actionDuration = 1200; // Drive duration for pulse movements (ms)

    if (action == "FORWARD") {
        moveForward();
    } else if (action == "BACKWARD") {
        moveBackward();
    } else if (action == "LEFT") {
        turnLeft();
    } else if (action == "RIGHT") {
        turnRight();
    } else if (action == "WAVE") {
        stopRobot();
        waveArms();
        actionDuration = 0;
    } else if (action == "AUTO") {
        autonomousMode = true;
        actionDuration = 0;
    } else if (action == "STOP") {
        stopRobot();
        autonomousMode = false;
        actionDuration = 0;
    } else {
        stopRobot();
        actionDuration = 0;
    }
}

// ==========================================
// 6. WEBSOCKET PARSER
// ==========================================
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    if (type == WStype_TEXT) {
        String msg = String((char*)payload);
        StaticJsonDocument<256> doc;
        DeserializationError error = deserializeJson(doc, msg);
        if (!error) {
            String action = doc["action"] | "NONE";
            String expression = doc["expression"] | "NORMAL";
            executeCommand(action, expression);
        }
    }
}

// ==========================================
// 7. WEBPAGE & GROQ AI ENGINE
// ==========================================
void handleRoot() {
    String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>ESP32 AI Companion</title>
  <style>
    body { font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif; background: #121212; color: #fff; text-align: center; margin: 0; padding: 20px; }
    h2 { margin-bottom: 4px; color: #f55036; }
    .card { background: #1E1E1E; border-radius: 12px; padding: 16px; margin: 14px auto; max-width: 440px; box-shadow: 0 4px 10px rgba(0,0,0,0.5); }
    input[type=password], input[type=text] { width: calc(100% - 24px); padding: 12px; margin: 8px 0; border: 1px solid #333; border-radius: 8px; background: #2A2A2A; color: #fff; font-size: 15px; }
    button { background: #f55036; border: none; color: #fff; padding: 12px 18px; font-size: 16px; border-radius: 8px; cursor: pointer; font-weight: bold; }
    button.mic-btn { background: #E94560; font-size: 18px; padding: 18px; border-radius: 50px; width: 85%; margin: 15px auto; display: block; }
    .status { color: #888; font-size: 13px; margin-top: 6px; }
    .log-box { text-align: left; background: #000; padding: 12px; border-radius: 8px; height: 120px; overflow-y: auto; font-family: monospace; font-size: 13px; color: #00ff88; margin-top: 10px; }
  </style>
</head>
<body>
  <h2>⚡ Robot AI Brain (Groq LPU)</h2>
  <div class="status" id="ws-status">Connecting to Robot...</div>

  <div class="card">
    <input type="password" id="groqKey" placeholder="Paste Groq API Key (gsk_...)">
    <button style="width: 100%;" onclick="saveKey()">Save Key Locally</button>
  </div>

  <div class="card">
    <button class="mic-btn" id="micBtn" onclick="toggleListening()">🎙️ TAP TO TALK</button>
    <div style="margin: 10px 0; color: #888;">— OR TYPE —</div>
    <div style="display: flex; gap: 8px;">
      <input type="text" id="textPrompt" placeholder="Ask anything or command robot..." onkeydown="if(event.key==='Enter') sendTextPrompt()">
      <button onclick="sendTextPrompt()">Send</button>
    </div>
    <div class="status" id="speech-status">Ready</div>
  </div>

  <div class="card">
    <div style="font-weight: bold; margin-bottom: 6px;">Live Interaction Feed</div>
    <div class="log-box" id="logBox">System initialized...</div>
  </div>

<script>
  let ws;
  let recognition;
  let isListening = false;

  function initWebSocket() {
    ws = new WebSocket('ws://' + location.hostname + ':81/');
    ws.onopen = () => { document.getElementById('ws-status').innerText = '🟢 Connected to ESP32'; };
    ws.onclose = () => { 
      document.getElementById('ws-status').innerText = '🔴 Disconnected. Reconnecting...'; 
      setTimeout(initWebSocket, 2000); 
    };
  }
  initWebSocket();

  function saveKey() {
    const k = document.getElementById('groqKey').value.trim();
    if (k) {
      localStorage.setItem('GROQ_API_KEY', k);
      alert('Groq API Key Saved!');
    }
  }

  window.onload = () => {
    const saved = localStorage.getItem('GROQ_API_KEY');
    if (saved) document.getElementById('groqKey').value = saved;
  };

  function log(msg) {
    const b = document.getElementById('logBox');
    b.innerHTML += '<br>> ' + msg;
    b.scrollTop = b.scrollHeight;
  }

  function speakText(text) {
    if ('speechSynthesis' in window) {
      window.speechSynthesis.cancel();
      const utterance = new SpeechSynthesisUtterance(text);
      utterance.pitch = 1.05;
      utterance.rate = 1.05;
      window.speechSynthesis.speak(utterance);
    }
  }

  async function queryAI(userText) {
    const apiKey = localStorage.getItem('GROQ_API_KEY');
    if (!apiKey) {
      alert('Please enter and save your Groq API key first!');
      return;
    }
    
    document.getElementById('speech-status').innerText = 'Groq Thinking...';
    log('You: ' + userText);

    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({ action: "NONE", expression: "LISTEN" }));
    }

    const systemPrompt = `You are a lively science exhibition robot. You must respond strictly in JSON matching this schema:
{
  "action": "FORWARD" | "BACKWARD" | "LEFT" | "RIGHT" | "WAVE" | "STOP" | "AUTO" | "NONE",
  "expression": "HAPPY" | "SCARED" | "SURPRISED" | "TALK" | "NORMAL",
  "speech": "Short spoken response (1-2 sentences maximum)"
}`;

    try {
      const response = await fetch("https://api.groq.com/openai/v1/chat/completions", {
        method: "POST",
        headers: {
          "Content-Type": "application/json",
          "Authorization": "Bearer " + apiKey
        },
        body: JSON.stringify({
          model: "llama-3.1-8b-instant",
          response_format: { type: "json_object" },
          messages: [
            { role: "system", content: systemPrompt },
            { role: "user", content: userText }
          ],
          temperature: 0.6,
          max_tokens: 150
        })
      });

      const data = await response.json();
      const rawText = data.choices[0].message.content;
      const parsed = JSON.parse(rawText);

      log('Robot: ' + parsed.speech);
      document.getElementById('speech-status').innerText = 'Ready';

      // Send movement & expression commands to ESP32
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action: parsed.action, expression: parsed.expression }));
      }

      // Read answer aloud on phone
      speakText(parsed.speech);

    } catch (err) {
      log('Error: ' + err.message);
      document.getElementById('speech-status').innerText = 'Request Failed';
      if (ws && ws.readyState === WebSocket.OPEN) {
        ws.send(JSON.stringify({ action: "NONE", expression: "NORMAL" }));
      }
    }
  }

  function resetMicButton() {
    isListening = false;
    const btn = document.getElementById('micBtn');
    btn.innerText = '🎙️ TAP TO TALK';
    btn.style.background = '#E94560';
  }

  function initSpeech() {
    const SpeechRecognition = window.SpeechRecognition || window.webkitSpeechRecognition;
    if (!SpeechRecognition) {
      alert('Speech recognition not supported in this browser. Please use Chrome on Android.');
      return;
    }
    
    recognition = new SpeechRecognition();
    recognition.lang = 'en-US';
    recognition.interimResults = false;
    recognition.continuous = false;

    recognition.onstart = () => {
      isListening = true;
      const btn = document.getElementById('micBtn');
      btn.innerText = '🛑 LISTENING... TAP TO STOP';
      btn.style.background = '#00ADB5';
      document.getElementById('speech-status').innerText = 'Listening...';
    };

    recognition.onresult = (e) => {
      const transcript = e.results[0][0].transcript;
      queryAI(transcript);
    };

    recognition.onerror = (e) => {
      document.getElementById('speech-status').innerText = 'Audio capture error';
      resetMicButton();
    };

    recognition.onend = () => {
      resetMicButton();
    };
  }

  function toggleListening() {
    if (!recognition) initSpeech();
    if (!recognition) return;

    if (isListening) {
      recognition.stop();
    } else {
      recognition.start();
    }
  }

  function sendTextPrompt() {
    const input = document.getElementById('textPrompt');
    if (input.value.trim() !== '') {
      queryAI(input.value.trim());
      input.value = '';
    }
  }
</script>
</body>
</html>
)rawliteral";

    server.send(200, "text/html", html);
}

// ==========================================
// 8. SETUP INITIALIZATION
// ==========================================
void setup() {
    Serial.begin(115200);

    // Standard Hardware I2C (SDA = 21, SCL = 22)
    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
        Serial.println("SSD1306 OLED init failed");
        for(;;);
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    drawFace("NORMAL");

    // Pin Configurations
    pinMode(TRIG_PIN, OUTPUT); pinMode(ECHO_PIN, INPUT);
    pinMode(MOTOR_L_F, OUTPUT); pinMode(MOTOR_L_B, OUTPUT);
    pinMode(MOTOR_R_F, OUTPUT); pinMode(MOTOR_R_B, OUTPUT);
    stopRobot();

    // Servos
    ESP32PWM::allocateTimer(0);
    ESP32PWM::allocateTimer(1);
    leftArm.attach(SERVO_LEFT_PIN, 500, 2400);
    rightArm.attach(SERVO_RIGHT_PIN, 500, 2400);
    leftArm.write(90);
    rightArm.write(90);

    // MAX98357A I2S Setup
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
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
    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_set_pin(I2S_NUM, &pin_config);

    // Wi-Fi Hotspot Connection
    WiFi.begin(ssid, password);
    display.clearDisplay();
    display.setCursor(0, 10);
    display.println("Connecting Hotspot...");
    display.display();
    
    while (WiFi.status() != WL_CONNECTED) {
        delay(400);
        Serial.print(".");
    }

    display.clearDisplay();
    display.setCursor(0, 5);
    display.println("CONNECTED!");
    display.println("\nOpen IP on Phone:");
    display.println(WiFi.localIP().toString());
    display.display();

    // Start Web & WebSocket Servers
    server.on("/", handleRoot);
    server.begin();
    webSocket.begin();
    webSocket.onEvent(webSocketEvent);

    playChirp(true);
    delay(2000);
    drawFace("HAPPY");
}

// ==========================================
// 9. MAIN CONTROL LOOP
// ==========================================
void loop() {
    server.handleClient();
    webSocket.loop();

    // Timed Manual Actions
    if (!autonomousMode) {
        if (actionDuration > 0 && (millis() - actionStartTime >= actionDuration)) {
            stopRobot();
            actionDuration = 0;
            drawFace("NORMAL");
        }
    } else {
        // Obstacle avoidance in Autonomous mode
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 120) {
            lastCheck = millis();
            long dist = getDistance();
            if (dist < 22) {
                stopRobot();
                drawFace("SCARED");
                playTone(300, 150);
                moveBackward();
                delay(350);
                turnRight();
                delay(400);
                stopRobot();
            } else {
                drawFace("NORMAL");
                moveForward();
            }
        }
    }
}
