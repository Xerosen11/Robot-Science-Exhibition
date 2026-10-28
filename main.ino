/*
  ESP32 DevKit V1 — Science Exhibition Robot
  Preserved features:
  - Phone-hotspot Wi-Fi + web controller
  - Browser speech recognition and typed prompts
  - Gemini text generation
  - Gemini TTS -> PCM upload -> MAX98357A I2S speaker
  - HC-SR04 visitor detection
  - SSD1306 animated face
  - Two SG90 servos
  Libraries: ESP32Servo, Adafruit GFX, Adafruit SSD1306
  Board package: Arduino-ESP32 2.x (uses legacy I2S driver API)

  IMPORTANT:
  1) Set your hotspot SSID/password below.
  2) Enter your Gemini API key in the robot webpage.
  3) HC-SR04 ECHO is 5 V on many modules. Use a resistor divider to bring
     ECHO down to 3.3 V before connecting it to ESP32 GPIO 18.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"

// ---- CHANGE THESE TO YOUR PHONE HOTSPOT DETAILS ----
const char* WIFI_SSID = "YOUR_PHONE_HOTSPOT_NAME";
const char* WIFI_PASSWORD = "YOUR_PHONE_HOTSPOT_PASSWORD";

// ---- EXISTING HARDWARE PIN ASSIGNMENTS ----
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_ADDR 0x3C
#define TRIG_PIN 5
#define ECHO_PIN 18
#define SERVO_L_PIN 13
#define SERVO_R_PIN 4
#define I2S_BCLK 26
#define I2S_LRC 25
#define I2S_DIN 19
#define I2S_PORT I2S_NUM_0

// Gemini TTS raw audio configured below as signed 16-bit little-endian mono PCM.
#define AUDIO_SAMPLE_RATE 24000
#define AUDIO_MAX_BYTES 1200000UL

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
bool oledReady = false;
bool wifiReady = false;

// ---- PHONE WEB APP ----
const char index_html[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Exhibit AI Robot</title>
<style>
:root{color-scheme:dark}body{font-family:system-ui,sans-serif;background:#0f172a;color:#fff;padding:15px;margin:0;max-width:760px;margin-inline:auto}
h2{text-align:center;color:#38bdf8;margin:4px 0 12px}.card{background:#1e293b;border-radius:12px;padding:12px;margin-bottom:10px}
input,textarea{width:100%;box-sizing:border-box;background:#0f172a;border:1px solid #475569;color:#fff;padding:10px;border-radius:8px;margin:6px 0 10px;font-size:16px}
button{width:100%;background:#0284c7;color:white;border:0;padding:12px;border-radius:8px;font-weight:bold;cursor:pointer;margin:3px 0}
button.mic{background:#15803d}#log{font-size:12px;max-height:200px;overflow:auto;color:#cbd5e1;font-family:monospace;overflow-wrap:anywhere}.err{color:#f87171}
small{color:#cbd5e1}
</style>
</head>
<body>
<h2>Exhibit AI Robot</h2>
<div class="card">
<label for="apiKey">Gemini API key (saved only in this browser)</label>
<input type="password" id="apiKey" autocomplete="off" placeholder="Paste Gemini API key">
<button type="button" onclick="saveKey()">Save API key</button>
<label for="systemPrompt">Robot personality / scenario</label>
<textarea id="systemPrompt" rows="3">You are a witty, friendly science exhibition robot. Answer in one short sentence without markdown. Reply in the same language the visitor uses.</textarea>
<small>Use a key you control. Do not share it or publish it in screenshots.</small>
</div>
<div class="card">
<button class="mic" id="micBtn" type="button" onclick="toggleMic()">🎤 Speak to Robot</button>
<label for="userText">Type a question</label>
<input type="text" id="userText" placeholder="Ask the robot..." autocomplete="off">
<button type="button" onclick="sendUserText()">Send prompt</button>
</div>
<div class="card"><strong>Status:</strong> <span id="status" style="color:#38bdf8">Ready</span><div id="log" aria-live="polite"></div></div>
<script>
let recognition=null, isRecording=false, isBusy=false, lastGreetingAt=0;
const $=id=>document.getElementById(id);
window.addEventListener('load',()=>{
  try { const saved=localStorage.getItem('gemini_api_key'); if(saved) $('apiKey').value=saved; } catch(e){}
});
function saveKey(){
  const key=$('apiKey').value.trim();
  try { localStorage.setItem('gemini_api_key',key); log('API key saved in this browser.'); }
  catch(e){ log('Could not save key; it can still be used until this page closes.',true); }
}
function log(msg,isErr=false){
  const line=document.createElement('div'); line.className=isErr?'err':'';
  line.textContent='> '+String(msg); $('log').prepend(line);
}
async function getJSON(url,options={}){
  const response=await fetch(url,options);
  const text=await response.text();
  let data={};
  try{data=text?JSON.parse(text):{};}catch(e){data={raw:text};}
  if(!response.ok) throw new Error(data.error?.message||data.message||`HTTP ${response.status}`);
  return data;
}
// Keep checking the ESP32 for one-shot proximity events; never interrupt a prompt.
setInterval(async()=>{
  if(isBusy) return;
  try{
    const data=await getJSON('/status',{cache:'no-store'});
    if(data.person_detected && !isBusy){
      const now=Date.now();
      if(now-lastGreetingAt<5000) return;
      lastGreetingAt=now;
      if(!$('apiKey').value.trim()){log('Person detected. Enter your Gemini API key to enable greeting.',true);return;}
      log('Person detected! Greeting visitor...');
      handlePrompt('A visitor just arrived at our science exhibit. Give them a quick one-sentence welcome.');
    }
  }catch(e){ /* Wi-Fi may briefly reconnect; keep polling. */ }
},1200);

if('webkitSpeechRecognition' in window || 'SpeechRecognition' in window){
  const SpeechRecognition=window.SpeechRecognition||window.webkitSpeechRecognition;
  recognition=new SpeechRecognition();
  recognition.continuous=false; recognition.interimResults=false;
  recognition.onresult=e=>{
    const text=e.results?.[0]?.[0]?.transcript||'';
    $('userText').value=text;
    if(text.trim()) handlePrompt(text.trim());
  };
  recognition.onerror=e=>log('Microphone recognition: '+(e.error||'unknown error'),true);
  recognition.onend=()=>{isRecording=false;$('micBtn').textContent='🎤 Speak to Robot';};
}
function toggleMic(){
  if(!recognition){alert('Speech recognition is unavailable in this browser. Try Chrome on the phone.');return;}
  if(!isRecording){
    try{recognition.start();isRecording=true;$('micBtn').textContent='⏹ Listening...';fetch('/emotion?val=LISTENING').catch(()=>{});}
    catch(e){log('Could not start microphone: '+e.message,true);}
  }else recognition.stop();
}
function sendUserText(){const text=$('userText').value.trim();if(text)handlePrompt(text);}
$('userText').addEventListener('keydown',e=>{if(e.key==='Enter')sendUserText();});

async function handlePrompt(promptText){
  if(isBusy) return;
  const key=$('apiKey').value.trim(), persona=$('systemPrompt').value.trim();
  if(!key){log('Missing Gemini API key. Paste it above.',true);return;}
  isBusy=true; $('status').textContent='Thinking...'; log('Query: '+promptText);
  try{
    // Use a current stable text model; no obsolete 1.5 fallback IDs.
    const response=await getJSON(
      'https://generativelanguage.googleapis.com/v1beta/models/gemini-3.8-flash:generateContent?key='+encodeURIComponent(key),
      {method:'POST',headers:{'Content-Type':'application/json'},
       body:JSON.stringify({
         system_instruction:{parts:[{text:persona||'You are a friendly science exhibition robot. Reply briefly in the visitor’s language.'}]},
         contents:[{role:'user',parts:[{text:promptText}]}],
         generationConfig:{temperature:0.7,maxOutputTokens:180}
       })}
    );
    const reply=(response.candidates?.[0]?.content?.parts||[])
      .map(p=>p.text||'').join('').replace(/[*#_~]/g,'').trim();
    if(!reply) throw new Error(response.promptFeedback?.blockReason
      ? 'Gemini blocked the prompt: '+response.promptFeedback.blockReason
      : 'Gemini returned no text. Check API access and model availability.');
    log('AI: '+reply);
    await speakAndSend(reply,key);
  }catch(err){
    log('Gemini text error: '+(err.message||String(err)),true);
    $('status').textContent='Error';
    fetch('/emotion?val=IDLE').catch(()=>{});
    isBusy=false;
  }
}

function findAudio(obj){
  if(!obj||typeof obj!=='object') return null;
  if(obj.type==='audio'&&typeof obj.data==='string') return obj.data;
  if(Array.isArray(obj)){for(const item of obj){const found=findAudio(item);if(found)return found;}}
  else{for(const value of Object.values(obj)){const found=findAudio(value);if(found)return found;}}
  return null;
}
function base64ToBytes(b64){
  const binary=atob(b64), out=new Uint8Array(binary.length);
  for(let i=0;i<binary.length;i++)out[i]=binary.charCodeAt(i);
  return out;
}
async function speakAndSend(text,key){
  $('status').textContent='Generating speech...';
  fetch('/emotion?val=TALKING').catch(()=>{});
  try{
    // TTS REST schema follows Google's current Interactions API documentation.
    // Request raw 24 kHz, mono, signed 16-bit little-endian PCM for I2S.
    const data=await getJSON('https://generativelanguage.googleapis.com/v1beta/interactions',{
      method:'POST',
      headers:{'Content-Type':'application/json','x-goog-api-key':key},
      body:JSON.stringify({
        model:'gemini-3.8-flash-tts',
        input:[{type:'user_input',content:[{type:'text',text:text,annotations:[{
          type:'speech_metadata',style:'clear, warm, friendly conversational speech'
        }]}]}],
        response_format:{type:'audio',mime_type:'audio/l16',sample_rate:24000},
        generation_config:{speech_config:[{voice:'Kore'}]}
      })
    });
    const b64=findAudio(data);
    if(!b64) throw new Error('Gemini TTS returned no audio payload. Check TTS model access and API response.');
    const pcm=base64ToBytes(b64);
    if(pcm.byteLength<2) throw new Error('Gemini returned empty audio.');
    if(pcm.byteLength>AUDIO_MAX_BYTES) throw new Error('Audio is too large; shorten the robot reply.');
    if(pcm.byteLength%2!==0) throw new Error('PCM audio has an incomplete 16-bit sample.');
    $('status').textContent='Playing on robot speaker...';
    const response=await fetch('/audio',{
      method:'POST',headers:{'Content-Type':'application/octet-stream'},body:pcm
    });
    if(!response.ok) throw new Error(`ESP32 audio upload failed (HTTP ${response.status}): `+(await response.text().catch(()=>'')));
    fetch('/emotion?val=HAPPY').catch(()=>{});
    $('status').textContent='Ready';
    setTimeout(()=>fetch('/emotion?val=IDLE').catch(()=>{}),2500);
  }catch(err){
    log('TTS / speaker error: '+(err.message||String(err)),true);
    $('status').textContent='Speech error';
    fetch('/emotion?val=IDLE').catch(()=>{});
  }finally{isBusy=false;}
}
</script>
</body>
</html>
)rawliteral";

// ---- I2S OUTPUT ----
bool initI2S(){
  i2s_config_t config={
    .mode=(i2s_mode_t)(I2S_MODE_MASTER|I2S_MODE_TX),
    .sample_rate=AUDIO_SAMPLE_RATE,
    .bits_per_sample=I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format=I2S_CHANNEL_FMT_ONLY_LEFT,
    .communication_format=I2S_COMM_FORMAT_STAND_I2S,
    .intr_alloc_flags=ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count=8,
    .dma_buf_len=512,
    .use_apll=false,
    .tx_desc_auto_clear=true,
    .fixed_mclk=0
  };
  i2s_pin_config_t pins={
    .bck_io_num=I2S_BCLK,
    .ws_io_num=I2S_LRC,
    .data_out_num=I2S_DIN,
    .data_in_num=I2S_PIN_NO_CHANGE
  };
  esp_err_t err=i2s_driver_install(I2S_PORT,&config,0,NULL);
  if(err!=ESP_OK && err!=ESP_ERR_INVALID_STATE){
    Serial.printf("I2S driver install failed: %d\n",(int)err); return false;
  }
  err=i2s_set_pin(I2S_PORT,&pins);
  if(err!=ESP_OK){Serial.printf("I2S pin setup failed: %d\n",(int)err);return false;}
  err=i2s_set_clk(I2S_PORT,AUDIO_SAMPLE_RATE,I2S_BITS_PER_SAMPLE_16BIT,I2S_CHANNEL_MONO);
  if(err!=ESP_OK){Serial.printf("I2S clock setup failed: %d\n",(int)err);return false;}
  i2s_zero_dma_buffer(I2S_PORT);
  return true;
}

// ---- OLED FACE ----
void drawFace(Emotion emo){
  if(!oledReady)return;
  display.clearDisplay();
  const int eyeRadius=14,lx=36,rx=92,cy=30;
  switch(emo){
    case IDLE:
      display.fillCircle(lx,cy,eyeRadius,SSD1306_WHITE);
      display.fillCircle(rx,cy,eyeRadius,SSD1306_WHITE);
      display.fillCircle(lx+3,cy-2,4,SSD1306_BLACK);
      display.fillCircle(rx+3,cy-2,4,SSD1306_BLACK);break;
    case LISTENING:
      display.fillCircle(lx,cy,eyeRadius+3,SSD1306_WHITE);
      display.fillCircle(rx,cy,eyeRadius+3,SSD1306_WHITE);
      display.fillCircle(lx,cy,6,SSD1306_BLACK);
      display.fillCircle(rx,cy,6,SSD1306_BLACK);break;
    case TALKING:{
      static bool toggle=false;toggle=!toggle;
      display.fillCircle(lx,cy-4,eyeRadius-2,SSD1306_WHITE);
      display.fillCircle(rx,cy-4,eyeRadius-2,SSD1306_WHITE);
      int mouthH=toggle?14:4;
      display.fillRoundRect(50,48-mouthH/2,28,mouthH,3,SSD1306_WHITE);break;}
    case HAPPY:
      display.fillCircle(lx,cy,eyeRadius,SSD1306_WHITE);
      display.fillCircle(rx,cy,eyeRadius,SSD1306_WHITE);
      display.fillCircle(lx,cy+4,eyeRadius,SSD1306_BLACK);
      display.fillCircle(rx,cy+4,eyeRadius,SSD1306_BLACK);
      display.drawRoundRect(52,48,24,6,2,SSD1306_WHITE);break;
    case SURPRISED:
      display.drawCircle(lx,cy,eyeRadius+4,SSD1306_WHITE);
      display.drawCircle(rx,cy,eyeRadius+4,SSD1306_WHITE);
      display.fillCircle(64,48,7,SSD1306_WHITE);break;
  }
  display.display();
}

long readDistanceCM(){
  digitalWrite(TRIG_PIN,LOW);delayMicroseconds(2);
  digitalWrite(TRIG_PIN,HIGH);delayMicroseconds(10);digitalWrite(TRIG_PIN,LOW);
  unsigned long duration=pulseIn(ECHO_PIN,HIGH,20000);
  if(duration==0)return 999;
  return (long)(duration*0.0343/2.0);
}

// ---- HTTP HANDLERS ----
void handleRoot(){server.send_P(200,"text/html; charset=utf-8",index_html);}

void handleStatus(){
  bool detected=pendingPersonEvent;
  pendingPersonEvent=false;
  server.send(200,"application/json",detected?"{\"person_detected\":true}":"{\"person_detected\":false}");
}

void handleEmotion(){
  if(server.hasArg("val")){
    String val=server.arg("val");
    if(val=="TALKING"){currentEmotion=TALKING;servoL.write(45);servoR.write(135);}
    else if(val=="HAPPY"){currentEmotion=HAPPY;servoL.write(90);servoR.write(90);}
    else if(val=="LISTENING"){currentEmotion=LISTENING;servoL.write(110);servoR.write(70);}
    else if(val=="IDLE"){currentEmotion=IDLE;servoL.write(90);servoR.write(90);}
    else if(val=="SURPRISED"){currentEmotion=SURPRISED;servoL.write(30);servoR.write(150);}
    emotionResetTimer=millis();
  }
  server.send(200,"text/plain","OK");
}

// Arduino-ESP32 WebServer raw-body callback: write incoming PCM chunks directly to I2S.
void handleAudioRaw(){
  HTTPRaw &raw=server.raw();
  switch(raw.status){
    case RAW_START:
      Serial.println("Audio upload started");
      i2s_zero_dma_buffer(I2S_PORT);
      break;
    case RAW_WRITE:{
      if(raw.currentSize==0)break;
      size_t bytesWritten=0;
      esp_err_t err=i2s_write(I2S_PORT,raw.buf,raw.currentSize,&bytesWritten,portMAX_DELAY);
      if(err!=ESP_OK||bytesWritten!=raw.currentSize){
        Serial.printf("I2S write warning: err=%d wrote=%u expected=%u\n",
          (int)err,(unsigned int)bytesWritten,(unsigned int)raw.currentSize);
      }
      break;
    }
    case RAW_END:
      Serial.printf("Audio upload complete: %u bytes\n",(unsigned int)raw.totalSize);
      break;
    case RAW_ABORTED:
      Serial.println("Audio upload aborted");
      break;
  }
}
void handleAudio(){
  HTTPRaw &raw=server.raw();
  if(raw.status==RAW_ABORTED){server.send(400,"text/plain","Audio body was aborted");return;}
  server.send(200,"text/plain","Audio received");
}

void setup(){
  Serial.begin(115200);
  delay(200);
  ESP32PWM::allocateTimer(0);ESP32PWM::allocateTimer(1);
  ESP32PWM::allocateTimer(2);ESP32PWM::allocateTimer(3);
  servoL.setPeriodHertz(50);servoR.setPeriodHertz(50);
  servoL.attach(SERVO_L_PIN,500,2400);servoR.attach(SERVO_R_PIN,500,2400);
  servoL.write(90);servoR.write(90);

  pinMode(TRIG_PIN,OUTPUT);digitalWrite(TRIG_PIN,LOW);
  pinMode(ECHO_PIN,INPUT);
  Wire.begin(21,22);
  oledReady=display.begin(SSD1306_SWITCHCAPVCC,OLED_ADDR);
  if(!oledReady)Serial.println("OLED init failed; continuing without display.");
  else{display.clearDisplay();drawFace(IDLE);}

  if(!initI2S())Serial.println("I2S init failed; check MAX98357A wiring.");
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD);
  Serial.printf("Connecting to hotspot '%s'",WIFI_SSID);
  unsigned long wifiStart=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-wifiStart<30000){
    delay(400);Serial.print(".");
  }
  Serial.println();
  if(WiFi.status()==WL_CONNECTED){
    wifiReady=true;
    Serial.print("Connected. Robot IP: ");Serial.println(WiFi.localIP());
    if(oledReady){
      display.clearDisplay();display.setTextSize(1);display.setTextColor(SSD1306_WHITE);
      display.setCursor(0,8);display.println("Connected!");
      display.setCursor(0,24);display.println("Open in browser:");
      display.setCursor(0,42);display.println(WiFi.localIP());display.display();
    }
  }else{
    Serial.println("Wi-Fi connection timed out. Check hotspot SSID/password, then reset.");
    if(oledReady){display.clearDisplay();display.setCursor(0,10);display.setTextColor(SSD1306_WHITE);
      display.setTextSize(1);display.println("Wi-Fi failed");display.println("Check hotspot");display.display();}
  }

  server.on("/",HTTP_GET,handleRoot);
  server.on("/status",HTTP_GET,handleStatus);
  server.on("/emotion",HTTP_GET,handleEmotion);
  server.on("/audio",HTTP_POST,handleAudio,handleAudioRaw);
  server.begin();
  Serial.println("HTTP server started; use the IP shown on the OLED/Serial Monitor.");
}

void loop(){
  server.handleClient();
  const unsigned long now=millis();
  if(now-lastDistanceCheck>600){
    lastDistanceCheck=now;
    long dist=readDistanceCM();
    if(dist>5&&dist<50&&!personTriggered){
      personTriggered=true;
      pendingPersonEvent=true;
      currentEmotion=SURPRISED;
      emotionResetTimer=now;
      servoL.write(30);servoR.write(150);
      Serial.printf("Visitor detected at %ld cm\n",dist);
    }else if(dist>=50||dist<=5){
      personTriggered=false;
    }
  }
  if(currentEmotion==SURPRISED&&now-emotionResetTimer>3500){
    currentEmotion=IDLE;servoL.write(90);servoR.write(90);
  }
  if(now-lastFaceUpdate>200){
    lastFaceUpdate=now;drawFace(currentEmotion);
  }
  // Reconnect after the phone hotspot briefly drops.
  static unsigned long lastReconnectAttempt=0;
  if(WiFi.status()!=WL_CONNECTED&&now-lastReconnectAttempt>5000){
    lastReconnectAttempt=now;WiFi.reconnect();
  }
}
