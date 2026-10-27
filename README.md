# ESP32 Science Exhibition Robot — Pinout Configuration

### Quick Reference Pin Mapping

| Component | Component Pin | ESP32 DevKit V1 Pin | Power / Voltage Rails |
| :--- | :--- | :--- | :--- |
| **0.96" I2C OLED** | VCC | **3V3** (or 5V) | Logic High 3.3V |
| | GND | **GND** | Common Ground |
| | SCL | **GPIO 22** | Hardware I2C Clock |
| | SDA | **GPIO 21** | Hardware I2C Data |
| **HC-SR04 Ultrasonic** | VCC | **VIN / 5V** | 3.3V–5V Rail |
| | GND | **GND** | Common Ground |
| | Trig | **GPIO 5** | Trigger Output |
| | Echo | **GPIO 18** | Echo Input |
| **Left Servo (SG90)** | VCC (Red) | **5V External Rail** | Shared 5V Rail (Do NOT power via 3V3) |
| | GND (Brown/Black) | **GND** | Common Ground |
| | PWM (Orange/Yellow)| **GPIO 13** | Hardware Timer 0 |
| **Right Servo (SG90)**| VCC (Red) | **5V External Rail** | Shared 5V Rail (Do NOT power via 3V3) |
| | GND (Brown/Black) | **GND** | Common Ground |
| | PWM (Orange/Yellow)| **GPIO 4** | Hardware Timer 1 |
| **MAX98357A I2S Amp** | VIN | **VIN / 5V** | Audio Power Rail |
| | GND | **GND** | Common Ground |
| | LRC (WS) | **GPIO 25** | Word Select / Left-Right Clock |
| | BCLK | **GPIO 26** | Bit Clock |
| | DIN | **GPIO 19** | Serial Data |
| | GAIN | **GND** | Configured to +6 dB (cleanest audio output) |
| | SD | *NC* | Not Connected (Shutdown pin left floating) |

---

### Power Architecture & Decoupling Notes

* **Common Ground Rail:** Tie all grounds (`ESP32 GND`, `Servo GND`, `MAX98357A GND`, `OLED GND`, `HC-SR04 GND`) to the same negative rail.
* **Servo Inrush Current:** Keep both servo `VCC` lines on your main 5V rail; do not route them through the ESP32's onboard 3.3V regulator to avoid brownout resets.
* **Capacitors:** Place a **47µF to 100µF electrolytic capacitor** across the 5V and GND rail right next to the servo power leads to suppress voltage dips during rapid arm movement.
