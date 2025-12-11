# 🔌 Hardware Connections – Onion Storage System

Below are the wiring details for all sensors, modules, and actuators used in the system.

---

## 🧠 ESP32 Pin Mapping

### 🌡 DHT22 (Temperature & Humidity)
| DHT22 Pin | ESP32 Pin |
|-----------|-----------|
| VCC       | 3.3V      |
| DATA      | GPIO 4    |
| GND       | GND       |

---

## 🧪 MQ-135 (Ammonia Sensor)
| MQ-135 Pin | ESP32 Pin |
|------------|-----------|
| AO         | GPIO 34 (Analog) |
| VCC        | 5V |
| GND        | GND |

---

## ⏱ DS3231 RTC
| RTC Pin | ESP32 Pin |
|---------|-----------|
| SDA     | GPIO 21 |
| SCL     | GPIO 22 |
| VCC     | 3.3V or 5V |
| GND     | GND |

---

## 🖥 16×2 LCD (I2C)
| LCD Pin | ESP32 Pin |
|---------|-----------|
| SDA     | GPIO 21 |
| SCL     | GPIO 22 |
| VCC     | 5V |
| GND     | GND |

(Uses I2C address **0x27**)

---

## 📡 SIM900A GSM Module
| SIM900A Pin | ESP32 Pin |
|-------------|-----------|
| TX →        | GPIO 16 (ESP32 RX2) |
| RX ←        | GPIO 17 (ESP32 TX2) |
| GND         | GND |
| VCC         | External 5V / 2A |

⚠ **Important:**  
SIM900A must use a dedicated 2A power supply. Do NOT power it from ESP32.

---

## 🔌 Relay Outputs (Active-Low)
| Function       | ESP32 Pin | Relay Input |
|----------------|-----------|-------------|
| Exhaust Fan    | GPIO 27   | IN1 |
| Heater         | GPIO 14   | IN2 |
| Mist Humidifier| GPIO 26   | IN3 |

### Relay Power:
- VCC → 5V  
- GND → GND  
- **Active-low logic:**  
  - `digitalWrite(pin, LOW)` → ON  
  - `digitalWrite(pin, HIGH)` → OFF  

---

## 🧷 Power Notes
- ESP32 powered via USB or 5V pin  
- SIM900A must have **stable 5V 2A**  
- All modules must share a **common ground**
