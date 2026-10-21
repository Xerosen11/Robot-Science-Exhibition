
#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ArduinoJson.h>
#include "driver/i2s.h"

// ===== YOUR HOTSPOT =====
const char* WIFI_SSID = "YOUR_PHONE_HOTSPOT";
const char* WIFI_PASS = "YOUR_HOTSPOT_PASSWORD";

// ===== YOUR EXACT PINOUT =====
#define OLED_SDA 21
#define OLED_SCL 22

#define I2S_BCLK 26
#define I2S_LRC  25
#define I2S_DOUT 33

#define TRIG_PIN 5
#define ECHO_PIN 18

#define SERVO1_PIN 13
#define SERVO2_PIN 14

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(
  SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1
);
WebServer server(80);

String mood = "neutral";
String expression = "idle";
String lastMessage = "Hello! I am ready.";
int intensity = 50;

const char PAGE[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Robot AI</title>
<style>
body{font-family:Arial;background:#101827;color:white;
padding:18px;max-width:600px;margin:auto}
textarea,input,button{box-sizing:border-box;width:100%;
padding:13px;margin:7px 0;border-radius:10px;border:0}
button{background:#40c4ff;font-weight:bold}
pre{white-space:pre-wrap;background:#202d40;padding:12px;
border-radius:10px}
</style>
</head>
<body>
<h2>🤖 Gemini Robot Control</h2>
<p>Enter your Gemini API key. Keep this page on your trusted
phone hotspot; don't share the key or expose the ESP32 server publicly.</p>
<input id="key" type="password" placeholder="Gemini API key">
<textarea id="msg" rows="3" placeholder="Talk to your robot..."></textarea>
<button onclick="sendMsg()">Send to robot</button>
<button onclick="listen()">🎙️ Speak (supported browsers)</button>
<pre id="out">Robot ready.</pre>
<script>
const $=id=>document.getElementById(id);

async function sendMsg(){
 const key=$('key').value.trim();
 const msg=$('msg').value.trim();
 if(!key||!msg){$('out').textContent='Enter an API key and message.';return;}
 $('out').textContent='Thinking...';
 try{
  const r=await fetch(
   'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash:generateContent',
   {method:'POST',
    headers:{'Content-Type':'application/json','x-goog-api-key':key},
    body:JSON.stringify({contents:[{parts:[{text:
      'You are a friendly expressive robot. Reply concisely. '+
      'Return ONLY JSON: {"reply":"spoken response","emotion":"happy|sad|angry|surprised|neutral","mood":"playful|calm|excited|curious","intensity":50}. User says: '+msg
    }]}]})
  );
  const data=await r.json();
  if(!r.ok)throw Error(data.error?.message||'Gemini request failed');
  const raw=data.candidates?.[0]?.content?.parts?.map(p=>p.text||'').join('')||'';
  const clean=raw.replace(/```json|```/g,'').trim();
  let obj;
  try{obj=JSON.parse(clean)}catch{obj={reply:raw,emotion:'neutral',mood:'calm',intensity:50}}
  $('out').textContent=obj.reply||raw;
  await fetch('/mood',{method:'POST',headers:{'Content-Type':'application/json'},
   body:JSON.stringify({emotion:obj.emotion||'neutral',mood:obj.mood||'calm',
   intensity:obj.intensity||50,reply:obj.reply||raw})});
  await speak(key,obj.reply||raw);
 }catch(e){$('out').textContent='Error: '+e.message}
}

async function speak(key,text){
 $('out').textContent+='\\nGenerating voice...';
 try{
  const r=await fetch(
   'https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-preview-tts:generateContent',
   {method:'POST',
    headers:{'Content-Type':'application/json','x-goog-api-key':key},
    body:JSON.stringify({
     contents:[{parts:[{text:'Say naturally and warmly: '+text}]}],
     generationConfig:{
      responseModalities:['AUDIO'],
      speechConfig:{voiceConfig:{prebuiltVoiceConfig:{voiceName:'Kore'}}}
     }
    })
   });
  const data=await r.json();
  if(!r.ok)throw Error(data.error?.message||'TTS request failed');
  const part=data.candidates?.[0]?.content?.parts?.find(p=>p.inlineData?.data);
  if(!part)throw Error('No audio returned. Check the TTS model and API access.');
  let bytes=Uint8Array.from(atob(part.inlineData.data),c=>c.charCodeAt(0));
  // Gemini TTS commonly returns raw 24 kHz, mono, signed 16-bit PCM.
  // Strip a WAV header if the returned bytes contain one.
  if(bytes.length>44&&String.fromCharCode(...bytes.slice(0,4))==='RIFF'){
    let pos=12;
    while(pos+8<=bytes.length){
      const id=String.fromCharCode(...bytes.slice(pos,pos+4));
      const size=bytes[pos+4]|(bytes[pos+5]<<8)|(bytes[pos+6]<<16)|(bytes[pos+7]<<24);
      if(id==='data'){bytes=bytes.slice(pos+8,pos+8+size);break}
      pos+=8+size;
    }
  }
  // Send sequential chunks to ESP32. Basic prototype; gaps may occur.
  const chunkSize=4096;
  for(let i=0;i<bytes.length;i+=chunkSize){
   const chunk=bytes.slice(i,Math.min(i+chunkSize,bytes.length));
   const b64=btoa(Array.from(chunk,x=>String.fromCharCode(x)).join(''));
   const resp=await fetch('/audio',{method:'POST',
    headers:{'Content-Type':'application/json'},
    body:JSON.stringify({data:b64})});
   if(!resp.ok)throw Error('ESP32 audio transfer failed');
  }
  await fetch('/face?state=idle');
 }catch(e){$('out').textContent+='\\nVoice error: '+e.message}
}

function listen(){
 const SR=window.SpeechRecognition||window.webkitSpeechRecognition;
 if(!SR){$('out').textContent='Speech recognition is unavailable in this browser. Type instead.';return}
 const rec=new SR();rec.lang='en-IN';
 rec.onresult=e=>{$('msg').value=e.results[0][0].transcript;sendMsg()};
 rec.onerror=e=>$('out').textContent='Mic error: '+e.error;
 rec.start();
}
</script>
</body>
</html>
)HTML";

void drawFace(String state) {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  if(state == "happy") {
    display.fillCircle(38, 25, 9, SSD1306_WHITE);
    display.fillCircle(90, 25, 9, SSD1306_WHITE);
    display.fillRoundRect(42, 43, 44, 5, 2, SSD1306_WHITE);
  } else if(state == "sad") {
    display.drawLine(27, 18, 45, 25, SSD1306_WHITE);
    display.drawLine(83, 25, 101, 18, SSD1306_WHITE);
    display.drawLine(43, 48, 64, 40, SSD1306_WHITE);
    display.drawLine(64, 40, 85, 48, SSD1306_WHITE);
  } else if(state == "angry") {
    display.fillCircle(38, 27, 7, SSD1306_WHITE);
    display.fillCircle(90, 27, 7, SSD1306_WHITE);
    display.drawLine(25, 13, 48, 20, SSD1306_WHITE);
    display.drawLine(80, 20, 103, 13, SSD1306_WHITE);
    display.drawLine(47, 49, 81, 49, SSD1306_WHITE);
  } else if(state == "surprised") {
    display.drawCircle(38, 24, 10, SSD1306_WHITE);
    display.drawCircle(90, 24, 10, SSD1306_WHITE);
    display.drawCircle(64, 46, 8, SSD1306_WHITE);
  } else if(state == "talking") {
    display.fillCircle(38, 24, 8, SSD1306_WHITE);
    display.fillCircle(90, 24, 8, SSD1306_WHITE);
    display.fillRoundRect(53, 40, 22, 15, 5, SSD1306_WHITE);
  } else {
    display.drawRoundRect(27, 17, 23, 15, 5, SSD1306_WHITE);
    display.drawRoundRect(79, 17, 23, 15, 5, SSD1306_WHITE);
    display.drawLine(48, 46, 80, 46, SSD1306_WHITE);
  }

  display.setTextSize(1);
  display.setCursor(0, 56);
  display.print(mood.substring(0, 18));
  display.display();
}

void handleMood() {
  if(server.hasArg("plain")) {
    JsonDocument doc;
    if(deserializeJson(doc, server.arg("plain")) == DeserializationError::Ok) {
      expression = doc["emotion"] | "neutral";
      mood = doc["mood"] | "calm";
      intensity = doc["intensity"] | 50;
      lastMessage = doc["reply"] | "";
      if(expression != "happy" && expression != "sad" &&
         expression != "angry" && expression != "surprised")
        expression = "neutral";
      drawFace(expression);
    }
  }
  server.send(200, "text/plain", "OK");
}

void handleAudio() {
  if(!server.hasArg("plain")) {
    server.send(400, "text/plain", "Missing audio");
    return;
  }

  JsonDocument doc;
  if(deserializeJson(doc, server.arg("plain"))) {
    server.send(400, "text/plain", "Bad JSON");
    return;
  }

  String b64 = doc["data"] | "";
  size_t outLen = (b64.length() / 4) * 3;
  if(b64.endsWith("==")) outLen -= 2;
  else if(b64.endsWith("=")) outLen -= 1;

  uint8_t *pcm = (uint8_t*)malloc(outLen);
  if(!pcm) {
    server.send(500, "text/plain", "Out of memory");
    return;
  }

  size_t decoded = 0;
  int ret = mbedtls_base64_decode(pcm, outLen, &decoded,
    (const unsigned char*)b64.c_str(), b64.length());

  if(ret == 0 && decoded > 0) {
    size_t written = 0;
    i2s_write(I2S_NUM_0, pcm, decoded, &written, portMAX_DELAY);
  }

  free(pcm);
  drawFace("talking");
  server.send(200, "text/plain", "OK");
}

void setupI2S() {
  i2s_config_t config = {};
  config.mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX);
  config.sample_rate = 24000;
  config.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  config.channel_format = I2S_CHANNEL_FMT_ONLY_LEFT;
  config.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  config.intr_alloc_flags = 0;
  config.dma_buf_count = 8;
  config.dma_buf_len = 512;
  config.use_apll = false;
  config.tx_desc_auto_clear = true;
  config.fixed_mclk = 0;

  i2s_pin_config_t pins = {};
  pins.bck_io_num = I2S_BCLK;
  pins.ws_io_num = I2S_LRC;
  pins.data_out_num = I2S_DOUT;
  pins.data_in_num = I2S_PIN_NO_CHANGE;

  i2s_driver_install(I2S_NUM_0, &config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pins);
  i2s_zero_dma_buffer(I2S_NUM_0);
}

void setup() {
  Serial.begin(115200);
  Wire.begin(OLED_SDA, OLED_SCL);

  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED not found. Check address/wiring.");
  } else {
    drawFace("neutral");
  }

  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(SERVO1_PIN, OUTPUT);
  pinMode(SERVO2_PIN, OUTPUT);

  setupI2S();

  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  Serial.print("Connecting to hotspot");
  while(WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.print("Open on your phone: http://");
  Serial.println(WiFi.localIP());

  server.on("/", HTTP_GET, []() {
    server.send_P(200, "text/html", PAGE);
  });

  server.on("/mood", HTTP_POST, handleMood);
  server.on("/audio", HTTP_POST, handleAudio);
  server.on("/face", HTTP_GET, []() {
    expression = server.arg("state");
    drawFace(expression);
    server.send(200, "text/plain", "OK");
  });

  server.begin();
}

void loop() {
  server.handleClient();
}
