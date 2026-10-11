// main.ino
#include <WiFi.h>
#include <esp_camera.h>
#include <esp_http_server.h>
#include <ESP32Servo.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <driver/i2s_std.h>
#include <HTTPClient.h>

#include "camera_pins.h"
#include "web_interface.h"

// --- Pin Definitions ---
// L298N Motors
#define IN1 1
#define IN2 2
#define ENA 3
#define IN3 41
#define IN4 42
#define ENB 40

// Servos
#define HEAD_SERVO_PIN 47
#define ARM_SERVO_PIN 48

// OLED I2C
#define OLED_SDA 38
#define OLED_SCL 39
#define OLED_ADDR 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// Audio (I2S)
#define I2S_MIC_WS   14
#define I2S_MIC_SCK  21
#define I2S_MIC_SD   13
#define I2S_SPK_WS   43
#define I2S_SPK_BCLK 44
#define I2S_SPK_DIN  45

// --- Globals & IPC Objects ---
Servo headServo;
Servo armServo;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
httpd_handle_t stream_httpd = NULL;

enum RobotState { IDLE, DRIVING, LISTENING, AI_SPEAKING };
volatile RobotState currentState = IDLE;

volatile int motorLeft = 0;
volatile int motorRight = 0;
volatile int headAngle = 90;
volatile int armAngle = 90;
volatile unsigned long lastCommandTime = 0;
volatile bool pttActive = false;

// Task Handles
TaskHandle_t Core0Task;
TaskHandle_t Core1Task;

const char* ssid = "WiFiname";
const char* password = "Password";

// --- FreeRTOS Tasks ---

// CORE 0: Network, HTTP, Camera Stream, AI Audio Pipeline
void TaskCore0(void *pvParameters) {
    // 1. Initialize Camera
    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_0;
    config.ledc_timer = LEDC_TIMER_0;
    config.pin_d0 = Y2_GPIO_NUM; config.pin_d1 = Y3_GPIO_NUM; config.pin_d2 = Y4_GPIO_NUM;
    config.pin_d3 = Y5_GPIO_NUM; config.pin_d4 = Y6_GPIO_NUM; config.pin_d5 = Y7_GPIO_NUM;
    config.pin_d6 = Y8_GPIO_NUM; config.pin_d7 = Y9_GPIO_NUM; config.pin_xclk = XCLK_GPIO_NUM;
    config.pin_pclk = PCLK_GPIO_NUM; config.pin_vsync = VSYNC_GPIO_NUM; config.pin_href = HREF_GPIO_NUM;
    config.pin_sccb_sda = SIOD_GPIO_NUM; config.pin_sccb_scl = SIOC_GPIO_NUM;
    config.pin_pwdn = PWDN_GPIO_NUM; config.pin_reset = RESET_GPIO_NUM;
    config.xclk_freq_hz = 20000000;
    config.pixel_format = PIXFORMAT_JPEG;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    esp_camera_init(&config);

    // 2. Initialize Wi-Fi with Fallback
    WiFi.begin(ssid, password);
    unsigned long startAttempt = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 10000) {
        vTaskDelay(500 / portTICK_PERIOD_MS);
    }
    
    if (WiFi.status() != WL_CONNECTED) {
        WiFi.disconnect();
        WiFi.mode(WIFI_AP);
        WiFi.softAP("RoboExhibition", "12345678");
    }
    
    // 3. Start HTTP Server for UI and Stream
    startCameraServer();

    // 4. Core 0 Loop (Audio Pipeline & Watchdogs)
    while (true) {
        if (pttActive) {
            currentState = LISTENING;
            // -- Audio Pipeline Placeholder --
            // 1. Read from INMP441 via standard IDF I2S RX
            // 2. Buffer 16-bit PCM data
            // 3. Initiate HTTPS multipart/form-data POST to AI endpoint
            // 4. Switch currentState to AI_SPEAKING
            // 5. Stream decoded PCM to MAX98357A via IDF I2S TX
            // --------------------------------
            pttActive = false; 
            currentState = IDLE;
        }
        vTaskDelay(10 / portTICK_PERIOD_MS);
    }
}

// CORE 1: Hardware PWM, OLED UI, and Safety Failsafe
void TaskCore1(void *pvParameters) {
    // 1. Init OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR);
    display.clearDisplay();
    display.display();

    // 2. Init Servos
    headServo.setPeriodHertz(50);
    armServo.setPeriodHertz(50);
    headServo.attach(HEAD_SERVO_PIN, 500, 2400);
    armServo.attach(ARM_SERVO_PIN, 500, 2400);

    // 3. Init Motor Pins
    pinMode(IN1, OUTPUT); pinMode(IN2, OUTPUT); pinMode(ENA, OUTPUT);
    pinMode(IN3, OUTPUT); pinMode(IN4, OUTPUT); pinMode(ENB, OUTPUT);

    // 4. Core 1 Loop
    while (true) {
        // Motor Failsafe: Stop motors if no command in 1.5s
        if (millis() - lastCommandTime > 1500) {
            motorLeft = 0;
            motorRight = 0;
            currentState = (currentState == DRIVING) ? IDLE : currentState;
        }

        // Apply Motor PWM
        digitalWrite(IN1, motorLeft > 0 ? HIGH : LOW);
        digitalWrite(IN2, motorLeft < 0 ? HIGH : LOW);
        analogWrite(ENA, abs(motorLeft) * 255); // Requires Core v3.x analogWrite

        digitalWrite(IN3, motorRight > 0 ? HIGH : LOW);
        digitalWrite(IN4, motorRight < 0 ? HIGH : LOW);
        analogWrite(ENB, abs(motorRight) * 255);

        // Apply Servos
        headServo.write(headAngle);
        armServo.write(armAngle);

        // Update OLED Status
        updateOLED(currentState);

        vTaskDelay(20 / portTICK_PERIOD_MS); // 50Hz refresh
    }
}

// --- HTTP Handlers ---
esp_err_t index_handler(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html");
    return httpd_resp_send(req, index_html, strlen(index_html));
}

esp_err_t action_handler(httpd_req_t *req) {
    char buf[64];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        char valL[8], valR[8];
        httpd_query_key_value(buf, "l", valL, sizeof(valL));
        httpd_query_key_value(buf, "r", valR, sizeof(valR));
        motorLeft = atoi(valL);
        motorRight = atoi(valR);
        lastCommandTime = millis();
        if(motorLeft != 0 || motorRight != 0) currentState = DRIVING;
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

esp_err_t servo_handler(httpd_req_t *req) {
    char buf[64], id[8], val[8];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "id", id, sizeof(id));
        httpd_query_key_value(buf, "val", val, sizeof(val));
        if (strcmp(id, "head") == 0) headAngle = atoi(val);
        if (strcmp(id, "arm") == 0) armAngle = atoi(val);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

esp_err_t ptt_handler(httpd_req_t *req) {
    char buf[64], active[8];
    if (httpd_req_get_url_query_str(req, buf, sizeof(buf)) == ESP_OK) {
        httpd_query_key_value(buf, "active", active, sizeof(active));
        pttActive = (atoi(active) == 1);
    }
    httpd_resp_send(req, "OK", 2);
    return ESP_OK;
}

void startCameraServer() {
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    httpd_uri_t index_uri = { .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = NULL };
    httpd_uri_t action_uri = { .uri = "/action", .method = HTTP_GET, .handler = action_handler, .user_ctx = NULL };
    httpd_uri_t servo_uri = { .uri = "/servo", .method = HTTP_GET, .handler = servo_handler, .user_ctx = NULL };
    httpd_uri_t ptt_uri = { .uri = "/ptt", .method = HTTP_GET, .handler = ptt_handler, .user_ctx = NULL };
    
    // Note: MJPEG /stream handler requires multipart framing. 
    // Omitted here for brevity, standard ESP32 Camera WebServer stream_handler drops in here.

    if (httpd_start(&stream_httpd, &config) == ESP_OK) {
        httpd_register_uri_handler(stream_httpd, &index_uri);
        httpd_register_uri_handler(stream_httpd, &action_uri);
        httpd_register_uri_handler(stream_httpd, &servo_uri);
        httpd_register_uri_handler(stream_httpd, &ptt_uri);
    }
}

// --- OLED UI Function ---
void updateOLED(RobotState state) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    
    // Network Info
    display.setCursor(0, 0);
    display.print(WiFi.getMode() == WIFI_AP ? "AP: RoboExhibition" : WiFi.localIP().toString());
    
    // Status Bar & Expressions
    display.setCursor(0, 16);
    switch(state) {
        case IDLE:
            display.print("State: IDLE (-_-)"); break;
        case DRIVING:
            display.print("State: DRIVING (o_o)"); break;
        case LISTENING:
            display.print("State: LISTENING (?)"); break;
        case AI_SPEAKING:
            display.print("State: SPEAKING (^o^)"); break;
    }
    display.display();
}

void setup() {
    // Disable core watchdog during setup to allow FreeRTOS config
    disableCore0WDT();
    
    xTaskCreatePinnedToCore(TaskCore0, "Core0_NetCamAudio", 10000, NULL, 1, &Core0Task, 0);
    xTaskCreatePinnedToCore(TaskCore1, "Core1_Hardware", 10000, NULL, 1, &Core1Task, 1);
}

void loop() {
    // Empty - Code executes in FreeRTOS tasks
    vTaskDelete(NULL);
}