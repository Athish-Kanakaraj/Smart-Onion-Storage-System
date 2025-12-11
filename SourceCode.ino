#include <Wire.h>
#include <RTClib.h>
#include <LiquidCrystal_I2C.h>
#include <DHT.h>

// ----------------- USER SETTINGS -----------------
#define THINGSPEAK_API_KEY "S6QMMC3CBYIIO3ZQ"
#define USER_PHONE_NUMBER "+918778049093"

// SIM900A GPRS settings
#define APN "airtelgprs.com"
#define GPRS_USER ""
#define GPRS_PASS ""

// Manual RTC time set
const int START_YEAR  = 2025;
const int START_MONTH = 9;
const int START_DAY   = 20;
const int START_HOUR  = 10;
const int START_MIN   = 0;
const int START_SEC   = 0;

// ----------------- PIN CONFIG -----------------
#define DHT_PIN 4
#define DHT_TYPE DHT22

#define MQ1_PIN 34   // Rack 1 MQ135

#define EXHAUST_PIN 27
#define HEATER_PIN 14
#define MIST_PIN 26

#define GSM_RX 16 // SIM900 TX -> ESP32 RX2
#define GSM_TX 17 // SIM900 RX -> ESP32 TX2
#define GSM_BAUD 9600

// LCD 16x2 I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);
RTC_DS3231 rtc;
DHT dht(DHT_PIN, DHT_TYPE);

// ----------------- TIMING -----------------
unsigned long lastSendTS = 0;
const unsigned long SEND_INTERVAL = 60000UL; // ThingSpeak every 60s

unsigned long lastSMS = 0;
const unsigned long SMS_INTERVAL = 60000UL; // SMS throttle

unsigned long lastLCD = 0;
const unsigned long LCD_INTERVAL = 1000UL; // Update LCD every 1s

// ----------------- MQ135 PPM -----------------
const float MQ_ADC_MAX = 4095.0;
const float MQ_PPM_MIN = 10.0;
const float MQ_PPM_MAX = 300.0;

// ----------------- GSM State Machines -----------------
enum SmsState {
  SMS_IDLE,
  SMS_SET_TEXT_MODE,
  SMS_WAIT_TEXT_MODE,
  SMS_SEND_CMGS,
  SMS_WAIT_PROMPT,
  SMS_SEND_MESSAGE,
  SMS_WAIT_SEND_RESULT,
  SMS_DONE
};

enum TSState {
  TS_IDLE,
  TS_SAPBR_SET_CONTYPE,
  TS_WAIT_SAPBR_CONTYPE,
  TS_SAPBR_SET_APN,
  TS_WAIT_SAPBR_APN,
  TS_SAPBR_OPEN,
  TS_WAIT_SAPBR_OPEN,
  TS_HTTPINIT,
  TS_WAIT_HTTPINIT,
  TS_HTTPPARA_CID,
  TS_WAIT_HTTPPARA_CID,
  TS_HTTPPARA_URL,
  TS_WAIT_HTTPPARA_URL,
  TS_HTTPPARA_CONTENT,
  TS_WAIT_HTTPPARA_CONTENT,
  TS_HTTPDATA,
  TS_WAIT_HTTPDATA,
  TS_HTTPACTION,
  TS_WAIT_HTTPACTION,
  TS_HTTPREAD,
  TS_WAIT_HTTPREAD,
  TS_HTTPTERM,
  TS_WAIT_HTTPTERM,
  TS_SAPBR_CLOSE,
  TS_WAIT_SAPBR_CLOSE,
  TS_DONE
};

SmsState smsState = SMS_IDLE;
TSState tsState = TS_IDLE;

// buffers for GSM comms
String gsmRxBuffer = "";
String smsToSend = "";
String tsPostData = "";

// timestamps for waiting
unsigned long gsmCmdSentAt = 0;
unsigned long gsmTimeout = 0;

// helper flags
bool smsRequested = false;
bool tsRequested = false;
bool ammoniaAlertSentRack1 = false;

// ----------------- SETUP -----------------
void setup() {
  Serial.begin(9600);
  delay(100);

  pinMode(EXHAUST_PIN, OUTPUT);
  pinMode(HEATER_PIN, OUTPUT);
  pinMode(MIST_PIN, OUTPUT);
  digitalWrite(EXHAUST_PIN, HIGH); // OFF
  digitalWrite(HEATER_PIN, HIGH);  // OFF
  digitalWrite(MIST_PIN, HIGH);    // OFF

  // I2C and LCD
  Wire.begin(21, 22);
  lcd.init();
  lcd.backlight();

  // RTC
  if (!rtc.begin()) {
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("RTC NOT FOUND");
    while (1);
  }
  DateTime manualStart(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MIN, START_SEC);
  rtc.adjust(manualStart);
  Serial.println("RTC set to manual start time.");

  // DHT
  dht.begin();

  // Serial2 for SIM900A
  Serial2.begin(GSM_BAUD, SERIAL_8N1, GSM_RX, GSM_TX);

  lcd.clear();
  lcd.setCursor(0,0);
  lcd.print("System Starting");
  delay(1000);

  gsmRxBuffer = "";
  smsState = SMS_IDLE;
  tsState = TS_IDLE;
}

// ----------------- MAIN LOOP -----------------
void loop() {
  DateTime now = rtc.now();
  DateTime start(START_YEAR, START_MONTH, START_DAY, START_HOUR, START_MIN, START_SEC);
  long daysSinceStart = (now.unixtime() - start.unixtime()) / 86400L;
  if (daysSinceStart < 0) daysSinceStart = 0;

  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();

  int mq1Raw = analogRead(MQ1_PIN);
  float mq1Ppm = mapMQRawToPpm(mq1Raw);

  unsigned long t = millis();

  // --- Print readings to Serial Monitor ---
  Serial.print("Temp: "); Serial.print(temperature);
  Serial.print(" C, Hum: "); Serial.print(humidity);
  Serial.print(" %, Rack1 NH4: "); Serial.print(mq1Ppm); Serial.println(" ppm");

  // LCD update
  if (t - lastLCD >= LCD_INTERVAL) {
    lastLCD = t;
    displayOnLCD(now, temperature, humidity);
  }

  // Control logic
  if (daysSinceStart < 15) {
    controlTemperature(temperature, 30.0, 35.0);
  } else {
    controlTemperature(temperature, 25.0, 30.0);
  }
  controlHumidity(humidity, 60.0, 80.0);

  // --- SMS logic for ammonia detection ---
  if (mq1Ppm >= 10.0 && mq1Ppm <= 300.0) {
    if (!ammoniaAlertSentRack1 && (t - lastSMS >= SMS_INTERVAL)) {
      startSMS("Foul Onion detected in Rack 1");
      ammoniaAlertSentRack1 = true;
    }
  } else {
    ammoniaAlertSentRack1 = false;
  }

  // Trigger ThingSpeak
  if (t - lastSendTS >= SEND_INTERVAL && !tsRequested) {
    lastSendTS = t;
    startThingSpeak(temperature, humidity, mq1Ppm);
  }

  // GSM read
  while (Serial2.available()) {
    char c = (char)Serial2.read();
    gsmRxBuffer += c;
  }
  if (gsmRxBuffer.length() > 0) {
    Serial.print("GSM RX: "); Serial.println(gsmRxBuffer);
  }

  // Process SMS state machine
  handleSMS();

  // Process ThingSpeak state machine
  handleThingSpeak();
}

// ----------------- HELPERS -----------------
bool gsmBufferHas(const char* token) {
  return gsmRxBuffer.indexOf(token) >= 0;
}
void gsmClearBuffer() { gsmRxBuffer = ""; }

void startSMS(String message) {
  if (smsRequested || smsState != SMS_IDLE) return;
  smsToSend = message;
  smsRequested = true;
  smsState = SMS_SET_TEXT_MODE;
  Serial.println("Requested SMS send: " + message);
}

void startThingSpeak(float temp, float hum, float mq1ppm) {
  if (tsRequested || tsState != TS_IDLE) return;
  tsPostData = String("api_key=") + THINGSPEAK_API_KEY +
               "&field1=" + String(temp, 2) +
               "&field2=" + String(hum, 2) +
               "&field3=" + String(mq1ppm, 2);
  tsRequested = true;
  tsState = TS_SAPBR_SET_CONTYPE;
  Serial.println("Requested ThingSpeak send.");
}

float mapMQRawToPpm(int raw) {
  float v = constrain((float)raw, 0.0, MQ_ADC_MAX);
  return MQ_PPM_MIN + (v / MQ_ADC_MAX) * (MQ_PPM_MAX - MQ_PPM_MIN);
}

void displayOnLCD(DateTime now, float temp, float hum) {
  lcd.clear();
  char line1[17], line2[17];
  snprintf(line1, sizeof(line1), "%02d:%02d:%02d %02d/%02d",
           now.hour(), now.minute(), now.second(), now.day(), now.month());
  lcd.setCursor(0,0);
  lcd.print(line1);

  if (isnan(temp) || isnan(hum)) {
    lcd.setCursor(0,1);
    lcd.print("SENS ERROR");
  } else {
    snprintf(line2, sizeof(line2), "T:%.1fC H:%.1f%%", temp, hum);
    lcd.setCursor(0,1);
    lcd.print(line2);
  }
}

// ----------------- RELAY CONTROL -----------------
void controlTemperature(float temp, float low, float high) {
  if (!isnan(temp)) {
    if (temp > high) {
      digitalWrite(EXHAUST_PIN, LOW); // turn fan ON (for active-low relay)
      digitalWrite(HEATER_PIN, HIGH);   // heater OFF
    } else if (temp < low) {
      digitalWrite(HEATER_PIN, LOW);  // heater ON
      digitalWrite(EXHAUST_PIN, HIGH);  // fan OFF
    } else {
      digitalWrite(EXHAUST_PIN, HIGH);  // fan OFF
      digitalWrite(HEATER_PIN, HIGH);   // heater OFF
    }
  }
}

void controlHumidity(float hum, float low, float high) {
  if (isnan(hum)) return;

  if (hum < low) {
    digitalWrite(MIST_PIN, LOW);  // Mist ON
  } else {
    digitalWrite(MIST_PIN, HIGH); // Mist OFF
  }
}

// ----------------- GSM STATE HANDLERS -----------------
void handleSMS() {
  switch (smsState) {
    case SMS_IDLE: break;
    case SMS_SET_TEXT_MODE:
      Serial2.print("AT+CMGF=1\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      smsState = SMS_WAIT_TEXT_MODE; gsmClearBuffer(); break;
    case SMS_WAIT_TEXT_MODE:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        smsState = SMS_SEND_CMGS; gsmClearBuffer();
      }
      break;
    case SMS_SEND_CMGS:
      Serial2.print(String("AT+CMGS=\"") + USER_PHONE_NUMBER + "\"\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 3000UL;
      smsState = SMS_WAIT_PROMPT; gsmClearBuffer(); break;
    case SMS_WAIT_PROMPT:
      if (gsmBufferHas(">") || millis() - gsmCmdSentAt > gsmTimeout) {
        smsState = SMS_SEND_MESSAGE; gsmClearBuffer();
      }
      break;
    case SMS_SEND_MESSAGE:
      Serial2.print(smsToSend);
      Serial2.write(26); // Ctrl+Z
      gsmCmdSentAt = millis(); gsmTimeout = 10000UL;
      smsState = SMS_WAIT_SEND_RESULT; gsmClearBuffer(); break;
    case SMS_WAIT_SEND_RESULT:
      if (gsmBufferHas("+CMGS") || gsmBufferHas("OK") ||
          gsmBufferHas("ERROR") || millis() - gsmCmdSentAt > gsmTimeout) {
        Serial.println("SMS send finished");
        smsState = SMS_DONE; gsmClearBuffer();
      }
      break;
    case SMS_DONE:
      smsRequested = false; lastSMS = millis();
      smsState = SMS_IDLE; break;
  }
}

void handleThingSpeak() {
  switch (tsState) {
    case TS_IDLE: break;
    case TS_SAPBR_SET_CONTYPE:
      Serial2.print("AT+SAPBR=3,1,\"CONTYPE\",\"GPRS\"\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_SAPBR_CONTYPE; gsmClearBuffer(); break;
    case TS_WAIT_SAPBR_CONTYPE:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_SAPBR_SET_APN; gsmClearBuffer();
      }
      break;
    case TS_SAPBR_SET_APN:
      Serial2.print(String("AT+SAPBR=3,1,\"APN\",\"") + APN + "\"\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_SAPBR_APN; gsmClearBuffer(); break;
    case TS_WAIT_SAPBR_APN:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_SAPBR_OPEN; gsmClearBuffer();
      }
      break;
    case TS_SAPBR_OPEN:
      Serial2.print("AT+SAPBR=1,1\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 5000UL;
      tsState = TS_WAIT_SAPBR_OPEN; gsmClearBuffer(); break;
    case TS_WAIT_SAPBR_OPEN:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPINIT; gsmClearBuffer();
      }
      break;
    case TS_HTTPINIT:
      Serial2.print("AT+HTTPINIT\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_HTTPINIT; gsmClearBuffer(); break;
    case TS_WAIT_HTTPINIT:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPPARA_CID; gsmClearBuffer();
      }
      break;
    case TS_HTTPPARA_CID:
      Serial2.print("AT+HTTPPARA=\"CID\",1\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_HTTPPARA_CID; gsmClearBuffer(); break;
    case TS_WAIT_HTTPPARA_CID:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPPARA_URL; gsmClearBuffer();
      }
      break;
    case TS_HTTPPARA_URL:
      Serial2.print("AT+HTTPPARA=\"URL\",\"http://api.thingspeak.com/update\"\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_HTTPPARA_URL; gsmClearBuffer(); break;
    case TS_WAIT_HTTPPARA_URL:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPPARA_CONTENT; gsmClearBuffer();
      }
      break;
    case TS_HTTPPARA_CONTENT:
      Serial2.print("AT+HTTPPARA=\"CONTENT\",\"application/x-www-form-urlencoded\"\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_HTTPPARA_CONTENT; gsmClearBuffer(); break;
    case TS_WAIT_HTTPPARA_CONTENT:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        Serial2.print("AT+HTTPDATA=" + String(tsPostData.length()) + ",10000\r\n");
        gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
        tsState = TS_WAIT_HTTPDATA; gsmClearBuffer();
      }
      break;
    case TS_WAIT_HTTPDATA:
      if (gsmBufferHas("DOWNLOAD") || millis() - gsmCmdSentAt > gsmTimeout) {
        Serial2.print(tsPostData);
        gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
        tsState = TS_HTTPACTION; gsmClearBuffer();
      }
      break;
    case TS_HTTPACTION:
      Serial2.print("AT+HTTPACTION=1\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 15000UL;
      tsState = TS_WAIT_HTTPACTION; gsmClearBuffer(); break;
    case TS_WAIT_HTTPACTION:
      if (gsmBufferHas("+HTTPACTION") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPREAD; gsmClearBuffer();
      }
      break;
    case TS_HTTPREAD:
      Serial2.print("AT+HTTPREAD\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 5000UL;
      tsState = TS_WAIT_HTTPREAD; gsmClearBuffer(); break;
    case TS_WAIT_HTTPREAD:
      if (gsmBufferHas("OK") || gsmBufferHas("ERROR") ||
          millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_HTTPTERM; gsmClearBuffer();
      }
      break;
    case TS_HTTPTERM:
      Serial2.print("AT+HTTPTERM\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 2000UL;
      tsState = TS_WAIT_HTTPTERM; gsmClearBuffer(); break;
    case TS_WAIT_HTTPTERM:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_SAPBR_CLOSE; gsmClearBuffer();
      }
      break;
    case TS_SAPBR_CLOSE:
      Serial2.print("AT+SAPBR=0,1\r\n");
      gsmCmdSentAt = millis(); gsmTimeout = 5000UL;
      tsState = TS_WAIT_SAPBR_CLOSE; gsmClearBuffer(); break;
    case TS_WAIT_SAPBR_CLOSE:
      if (gsmBufferHas("OK") || millis() - gsmCmdSentAt > gsmTimeout) {
        tsState = TS_DONE; gsmClearBuffer();
      }
      break;
    case TS_DONE:
      tsRequested = false;
      tsState = TS_IDLE;
      Serial.println("ThingSpeak send complete");
      break;
  }
}
