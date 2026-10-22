# Leo - Friendly Desktop Assistant
*A simple desktop robot that can move around using its wheels and assist people acccoring to there need.*

>    # **Lead By:** Neha ma'am  

---

>Copyright (c) 2026 Harsh Tirkey
## 1. Hardware & Bill of Materials 

| Component | Specification / Model | Quantity | Price |
| :--- | :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 CAM DevKit (N16R8) | 1 | ₹900-₹1600 |
| **Camera** | OV2640 (24-Pin) | 1 | ₹150 – ₹200 |
| **Screen** | 0.96" I²C OLED (SSD1306) | 1 | ₹150 – ₹200 |
| **Microphone** | inmp441 i2s | 1 | ₹150 – ₹180 |
| **Amplifier** | MAX98357A | 1 | ₹120 – ₹160 |
| **Speaker** | 3W / 4Ω or 8Ω Mini Speaker | 1 | ₹40 – ₹60 |
| **Actuators** | SG90 Servos | 2 | ₹180–₹240 |
| **Motor Driver** | L298N / L293D Shield | 1 | 100–₹150 |
| **Power Supply** | Powerbank | 1 | NULL |
| **Chassis** | Wheeled chassis, two DC motors and wheels | 1 | ₹250–₹350 |

---

## 2. Students working on this project.
* Harsh Tirkey
* Nikhil Bhengra
* Nayan Nayak
* Rimil Kerketta
* Priyanshu Bhagat
* Shraddha Shrama

## 2. Circuit and Circuit Diagram
*Currently on work.*
 
|Component|Pin / Signal|ESP32 DevKit V1 Pin|Notes|
|---|---|---|---|
|I2C OLED (0.96" / 0.98")|VCC|3V3|Logic & core display power|
|\(|GND|GND|Common ground||\)|SCL|GPIO 22|Hardware default I2C clock|
|\(|SDA|GPIO 21|Hardware default I2C data||MAX98357A I2S Amp|VIN / VCC|VIN (5V)|Power from 5V/USB rail for full 3W output||\)|GND|GND|Common ground|
|\(|BCLK (Bit Clock)|GPIO 26|Standard I2S serial clock||\)|LRC / LRCLK (Word Select)|GPIO 25|Standard I2S left/right frame clock|
|\(|DIN (Data In)|GPIO 33|Audio data line||\)|GAIN|Leave Floating|Defaults to 9 dB gain (stable, minimal distortion)|
|\(|SD|Leave Floating|Mixes L+R channels into mono automatically||3W Speaker|+ and -|SPK+ / SPK-|Connect only across the amp's bridge terminals (never ground!)||HC-SR04 (RCWL-9300D)|VCC|3V3|Run at 3.3V so Echo output stays 3.3V-safe||\)|GND|GND|Common ground|
|\(|TRIG|GPIO 5|Trigger pulse output||\)|ECHO|GPIO 18|Echo return signal input|
|Reserved: Servos (SG90)|Servo 1 PWM|GPIO 13|Safe output pin for LEDC / Servo sweep|
||Servo 2 PWM|GPIO 14|Safe output pin for LEDC / Servo sweep|
---
