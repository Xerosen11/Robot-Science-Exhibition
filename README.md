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
| Component | Pin / Wire | ESP32 Connection | Power Connection |
| --- | --- | --- | --- |
| **SSD1306 OLED** | SDA | GPIO 21 | 3.3V / GND |
|  | SCL | GPIO 22 |  |
| **MAX98357A Amp** | DIN | GPIO 27 | 5V / Common GND |
|  | LRC | GPIO 26 |  |
|  | BCLK | GPIO 14 |  |
| **HC-SR04 Sensor** | TRIG | GPIO 32 | 3.3V / GND |
|  | ECHO | GPIO 33 |  |
| **Left SG90 Servo** | Signal (Orange/Yellow) | GPIO 18 | **External 5V** / Common GND |
| **Right SG90 Servo** | Signal (Orange/Yellow) | GPIO 19 | **External 5V** / Common GND |

Ensure the negative (GND) wire from your external 5V servo power supply is securely tied to a GND pin on the ESP32, or the GPIO 18 and 19 control signals will not register.
---
