#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Servo.h>
#include "driver/i2s.h"

// ==========================================
// 1. PIN DEFINITIONS
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
// 2. OLED EMOTION RENDERER
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
// 3. MOTOR & SERVO PRIMITIVES
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
    
    actionStartTime = millis();
    actionDuration = 1200; // Pulse move duration in ms

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
// 4. SETUP INITIALIZATION
// ==========================================
void setup() {
    // 115200 Baud Hardware USB Serial link
    Serial.begin(115200);

    // Standard I2C: SDA on 21, SCL on 22
    Wire.begin(21, 22);
    if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
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

    // MAX98357A I2S Setup (16 kHz, 16-Bit Mono/Stereo)
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX),
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 8,
        .dma_buf_len = 128,
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

    display.clearDisplay();
    display.setCursor(10, 25);
    display.println("USB SERIAL READY");
    display.display();
    delay(1500);
    drawFace("HAPPY");
}

// ==========================================
// 5. MAIN CONTROL LOOP (SERIAL PARSING)
// ==========================================
void loop() {
    // 1. Process Incoming Serial Data from PC
    if (Serial.available()) {
        String header = Serial.readStringUntil('\n');
        header.trim();

        // Format: CMD:ACTION,EXPRESSION
        if (header.startsWith("CMD:")) {
            int commaIndex = header.indexOf(',');
            if (commaIndex > 0) {
                String action = header.substring(4, commaIndex);
                String expression = header.substring(commaIndex + 1);
                executeCommand(action, expression);
            }
        } 
        // Format: AUDIO:LENGTH_IN_BYTES
        else if (header.startsWith("AUDIO:")) {
            int audioSize = header.substring(6).toInt();
            drawFace("TALK");

            uint8_t buffer[512];
            int totalBytesRead = 0;

            while (totalBytesRead < audioSize) {
                int toRead = min((int)sizeof(buffer), audioSize - totalBytesRead);
                int bytesRead = Serial.readBytes(buffer, toRead);
                if (bytesRead > 0) {
                    size_t bytes_written;
                    i2s_write(I2S_NUM, buffer, bytesRead, &bytes_written, portMAX_DELAY);
                    totalBytesRead += bytesRead;
                }
            }
            drawFace("NORMAL");
        }
    }

    // 2. Timed Manual Movement Pulse Reset
    if (!autonomousMode) {
        if (actionDuration > 0 && (millis() - actionStartTime >= actionDuration)) {
            stopRobot();
            actionDuration = 0;
            drawFace("NORMAL");
        }
    } 
    // 3. Autonomous Obstacle Avoidance
    else {
        static unsigned long lastCheck = 0;
        if (millis() - lastCheck > 120) {
            lastCheck = millis();
            long dist = getDistance();
            if (dist < 22) {
                stopRobot();
                drawFace("SCARED");
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
