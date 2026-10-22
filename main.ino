#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s.h>
#include <ESP32Servo.h>

// --- Wi-Fi Configuration ---
const char* ssid = "YOUR_HOTSPOT_NAME"; 
const char* password = "YOUR_HOTSPOT_PASSWORD";       

WebServer server(80);
WebSocketsServer webSocket(81);

// --- Pin Definitions ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

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

// --- State Machine Variables (BUG FIX) ---
String pendingEmotion = "";
String pendingText = "";
bool isActing = false;

// --- Web UI HTML ---
const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Scrappy Control</title>
  <style>
    body { font-family: Arial; text-align: center; margin-top: 20px; background-color: #222; color: #fff; }
    button, input { padding: 10px; font-size: 18px; margin: 5px; border-radius: 8px; border: none; }
    input[type="text"] { width: 80%; max-width: 300px; margin-bottom: 20px; }
  </style>
</head>
<body>
  <h1>Scrappy Companion</h1>
  <input type="text" id="chatbox" placeholder="Type what Scrappy should say...">
  <br>
  <button onclick="sendMsg('HAPPY')">Say (Happy)</button>
  <button onclick="sendMsg('ALERT')">Say (Alert)</button>
  <button onclick="sendMsg('PANIC')">Say (Panic)</button>
  <button onclick="sendMsg('REST')">Rest</button>
  <script>
    var connection = new WebSocket('ws://' + location.hostname + ':81/', ['arduino']);
    function sendMsg(emotion) {
      if(connection.readyState === WebSocket.OPEN){
        var text = document.getElementById('chatbox').value;
        if(text === "" && emotion !== "REST") text = "Hello!";
        connection.send(emotion + "|" + text);
      }
    }
  </script>
</body>
</html>
)rawliteral";

// --- WebSocket Event Handler (BUG FIX) ---
// Now we only save the data here. We DO NOT use delay() or move servos here.
void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if(type == WStype_TEXT && !isActing) {
    String data = (char*)payload;
    int delimiter = data.indexOf('|');
    if (delimiter > 0) {
      pendingEmotion = data.substring(0, delimiter);
      pendingText = data.substring(delimiter + 1);
    } else {
      pendingEmotion = data;
      pendingText = "";
    }
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
  displayScreen("NEUTRAL", "Online.");
  
  // 4. I2S Audio (MAX98357A)
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
  i2s_zero_dma_buffer(I2S_NUM_0); // Ensure silence on boot

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
}

void loop() {
  server.handleClient();
  webSocket.loop();

  // Execute pending web commands safely in the main loop
  if (pendingEmotion != "") {
    isActing = true;
    speakAndMove(pendingEmotion, pendingText);
    pendingEmotion = "";
    pendingText = "";
    isActing = false;
  }

  // Sensor Logic (Paused while acting so it doesn't interrupt itself)
  if (!isActing && (millis() - lastSensorCheck > 200)) {
    long distance = getDistance();
    if (distance > 0 && distance < 15) {
      isActing = true;
      speakAndMove("PANIC", "Whoa! Too close!"); 
      isActing = false;
    } else if (distance >= 30 && distance <= 70) {
      isActing = true;
      speakAndMove("HAPPY", "Oh, hi there!");
      isActing = false;
    }
    lastSensorCheck = millis();
  }
}

// --- Audio Beep Synthesizer (BUG FIX) ---
void playBeep(int frequency, int duration_ms) {
  int sample_rate = 16000;
  int samples = (sample_rate * duration_ms) / 1000;
  int half_period = sample_rate / frequency / 2;
  int16_t sample_val = 3000; // Moderate volume
  int16_t buffer[64];
  size_t bytes_written;
  
  // Play tone
  for(int i=0; i<samples; ) {
    for(int j=0; j<64 && i<samples; j++, i++) {
      buffer[j] = ((i / half_period) % 2 == 0) ? sample_val : -sample_val;
    }
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
  }
  
  // Flush buffer with absolute silence
  memset(buffer, 0, sizeof(buffer));
  for(int k = 0; k < 8; k++) {
    i2s_write(I2S_NUM_0, buffer, sizeof(buffer), &bytes_written, portMAX_DELAY);
  }
  
  // Hardware-level DMA clear
  i2s_zero_dma_buffer(I2S_NUM_0);
}

// --- Display, Voice & Movement ---
void speakAndMove(String emotion, String text) {
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
  else if (emotion == "REST") {
    setArms(10, 170);
  }
  
  delay(1500); // Hold the text on screen briefly
  displayScreen("NEUTRAL", "");
}

void displayScreen(String emotion, String text) {
  display.clearDisplay();
  
  int yOffset = -5; 
  if (emotion == "NEUTRAL") {
    display.fillRoundRect(20, 20+yOffset, 30, 24, 5, SSD1306_WHITE);
    display.fillRoundRect(78, 20+yOffset, 30, 24, 5, SSD1306_WHITE);
  } else if (emotion == "HAPPY") {
    display.drawCircle(35, 30+yOffset, 15, SSD1306_WHITE);
    display.fillRect(20, 30+yOffset, 30, 20, SSD1306_BLACK); 
    display.drawCircle(93, 30+yOffset, 15, SSD1306_WHITE);
    display.fillRect(78, 30+yOffset, 30, 20, SSD1306_BLACK); 
  } else if (emotion == "PANIC") {
    int jitterX = random(-2, 3); int jitterY = random(-2, 3);
    display.fillCircle(35 + jitterX, 32 + jitterY + yOffset, 12, SSD1306_WHITE);
    display.fillCircle(93 + jitterX, 32 + jitterY + yOffset, 12, SSD1306_WHITE);
  } else if (emotion == "ALERT") {
    display.fillCircle(25, 30+yOffset, 15, SSD1306_WHITE);
    display.fillCircle(83, 30+yOffset, 15, SSD1306_WHITE);
    display.fillCircle(32, 30+yOffset, 5, SSD1306_BLACK); 
    display.fillCircle(90, 30+yOffset, 5, SSD1306_BLACK);
  } else if (emotion == "THINKING") {
    display.fillRoundRect(20, 28+yOffset, 30, 8, 3, SSD1306_WHITE);
    display.fillRoundRect(78, 28+yOffset, 30, 8, 3, SSD1306_WHITE);
  }

  // Draw Text at the bottom
  if (text != "") {
    display.setCursor(0, 48);
    display.print(text);
  }
  display.display();
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
