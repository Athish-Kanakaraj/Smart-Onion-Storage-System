# 🍁 Smart Onion Storage System

This project is a smart monitoring and control system built to improve the quality and shelf life of stored onions.  
Using an ESP32 and a set of sensors, the system keeps track of **temperature**, **humidity**, and **ammonia levels**, then automatically adjusts the environment to prevent spoilage.  
If the ammonia level rises (a sign of foul onions), the system immediately sends an **SMS alert** and updates the live data to **ThingSpeak**.

---

## 🌟 What This System Does

### ✔ Monitors the environment  
- Temperature & Humidity (DHT22)  
- Ammonia levels (MQ-135)  
- Real-time clock for accurate timestamps  
- LCD screen for live status updates  

### ✔ Takes automatic action  
- Turns ON/OFF exhaust fans, heaters, and mist sprayers  
- Maintains ideal conditions for both curing and long-term storage  
- Uses active-low relay controls for reliability  

### ✔ Alerts and logs data  
- Sends an SMS if foul onions are detected  
- Uploads sensor data to ThingSpeak every 60 seconds  
- Runs GSM communication using a non-blocking state machine so the system never freezes

---

## 🧠 Why This Project Exists
Farmers often lose a large portion of onion harvests due to improper storage conditions.  
High humidity and poor ventilation can lead to sprouting and rot.  
Spoiled onions release ammonia — something a human nose can notice, but often too late.

This system automates the entire process:

- Tracks real-time conditions  
- Detects spoilage early  
- Protects the remaining crop  
- Sends alerts instantly  

---

## 🔌 Hardware Used
- **ESP32**  
- **SIM900A GSM Module**  
- **DHT22 Temperature & Humidity Sensor**  
- **MQ-135 Gas Sensor**  
- **DS3231 Real Time Clock**  
- **16×2 I2C LCD**  
- **3× Active-Low Relay Modules** for controlling heater, fan, and mist unit  

Full connection details are in `hardware_connection.md`.

---

## 🛠 How It Works

### 1. Environment Monitoring  
The system continuously reads temperature, humidity, and ammonia levels.  
Values are displayed on the LCD and printed to the serial monitor.

### 2. Intelligent Control Logic  
- During the first 15 days (curing period): temperature is maintained between **30–35°C**  
- After curing: temperature is maintained between **25–30°C**  
- Humidity is kept between **60–80%** using mist control  

### 3. Spoilage Detection  
If ammonia levels rise above the set threshold, the system detects **foul onions** and sends an SMS alert to the registered phone number.

### 4. Cloud Logging  
All sensor data is uploaded to ThingSpeak periodically for remote monitoring.

---

## ▶ Getting Started
1. Open `ESP32_OnionStorage.ino` in the Arduino IDE.  
2. Update:
   - ThingSpeak API key  
   - Phone number for SMS  
   - APN settings for your GSM SIM  
3. Upload to ESP32.  
4. Power the SIM900A with a stable **5V 2A** supply.  
5. Watch the LCD and serial output to verify operation.
