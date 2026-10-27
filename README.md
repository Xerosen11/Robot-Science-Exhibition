Here is the complete, exact pinout table matching the latest code:
| Component | Component Pin | ESP32 DevKit V1 Pin | Power / Voltage Note |
|---|---|---|---|
| I2C OLED (0.96") | VCC
GND
SCL
SDA | 3V3 (or 5V/VIN)
GND
GPIO 22
GPIO 21 | Standard hardware I2C bus |
| HC-SR04 Ultrasonic | VCC
GND
Trig
Echo | VIN / 5V
GND
GPIO 5
GPIO 18 | Direct 3.3V–5V compatible |
| Left Servo (SG90) | VCC (Red)
GND (Brown/Black)
Signal (Orange/Yellow) | 5V / External Rail
GND
GPIO 13 | Dedicated PWM timer |
| Right Servo (SG90) | VCC (Red)
GND (Brown/Black)
Signal (Orange/Yellow) | 5V / External Rail
GND
GPIO 4 | Clean GPIO (no boot conflict) |
| MAX98357A I2S Amp | VIN
GND
LRC (WS)
BCLK
DIN
GAIN
SD | VIN / 5V
GND
GPIO 25
GPIO 26
GPIO 19
GND
(Leave disconnected) | Gain to GND sets volume to +6dB (avoids clipping/distortion on 3W speaker) |
Critical Power Rule
 * Common Ground: All GND pins (ESP32, Servos, MAX98357A, OLED, HC-SR04) must connect to the same common Ground rail.
 * Servo Power: Never run both servo VCC lines directly off the ESP32's tiny 3.3V pin—feed them directly from your 5V power supply rail (VIN / USB 5V).
