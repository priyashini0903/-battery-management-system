#include <Wire.h>
#include <Adafruit_INA219.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <math.h>

// ---------- WiFi ----------
const char* ssid = "Jeswin";
const char* password = "88888888";
const String serverURL = "http://172.20.10.5:5000/data"; // Flask endpoint

Adafruit_INA219 ina219;

#define DHTPIN 3        
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ---------- LCD ----------
LiquidCrystal_I2C lcd(0x27, 16, 2);

// ---------- Relay ----------
#define RELAY_PIN 14 // Active HIGH relay (D5 on NodeMCU)

// ---------- Battery Parameters ----------
const float MIN_VOLTAGE = 0.0;
const float MAX_VOLTAGE = 6.0;
const float MAX_TEMP = 50.0;
const float MIN_SOH = 85.0;
const int TOTAL_CYCLES = 500;

float previous_soc = -1.0;
int battery_cycles = 0;
int remaining_cycles = TOTAL_CYCLES;
bool cutoff = false;
bool charging = false;
String reason = "Normal";

WiFiClient wifiClient;

void setup() {
  Serial.begin(115200);
  delay(100);
  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW); // Supply ON initially

  // --- Shared I2C for INA219 + LCD ---
  Wire.begin(4, 5); // SDA = D2(GPIO4), SCL = D1(GPIO5)
  ina219.begin();

  lcd.begin(16, 2);
  lcd.backlight();
  lcd.clear();
  lcd.print("BMS Initializing...");
  delay(800);

  dht.begin();

  // --- WiFi ---
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  lcd.clear();
  lcd.print("WiFi Connecting...");
  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(300);
    Serial.print(".");
    tries++;
  }
  lcd.clear();
  if (WiFi.status() == WL_CONNECTED) {
    lcd.print("WiFi Connected");
    Serial.println();
    Serial.print("ESP IP: ");
    Serial.println(WiFi.localIP());
  } else {
    lcd.print("WiFi Failed");
    Serial.println("\nWiFi failed to connect");
  }
  delay(800);
  lcd.clear();
}

String safeFloatStr(float v, int decimals = 2) {
  if (isnan(v) || isinf(v)) v = 0.0;
  return String(v, decimals);
}

float safeFloatVal(float v, float fallback = 0.0) {
  if (isnan(v) || isinf(v)) return fallback;
  return v;
}

void loop() {
  // --- Sensor Readings ---
  float voltage = ina219.getBusVoltage_V();         // V
  float current_mA = ina219.getCurrent_mA();        // mA
  float current_A = current_mA / 1000.0;            // A
  float power_W = ina219.getPower_mW() / 1000.0;    // W
  float tempC = dht.readTemperature();
  if (isnan(tempC)) tempC = 0.0;

  // --- sanitize readings (avoid inf/NaN) ---
  voltage = safeFloatVal(voltage, 0.0);
  current_A = safeFloatVal(current_A, 0.0);
  power_W = safeFloatVal(power_W, voltage * current_A); // fallback compute

  // --- SOC Calculation ---
  float soc;
  if (voltage <= MIN_VOLTAGE) soc = 0.0;
  else if (voltage >= MAX_VOLTAGE) soc = 100.0;
  else soc = ((voltage - MIN_VOLTAGE) / (MAX_VOLTAGE - MIN_VOLTAGE)) * 100.0;

  // --- Battery Cycle Count ---
  if (previous_soc < 20 && soc > 80) battery_cycles++;
  if (battery_cycles < 0) battery_cycles = 0;
  previous_soc = soc;

  // --- SOH and Remaining Cycles ---
  float soh = 100.0 - ((float)battery_cycles / TOTAL_CYCLES) * 100.0;
  if (soh < 0.0) soh = 0.0;
  remaining_cycles = TOTAL_CYCLES - battery_cycles;
  if (remaining_cycles < 0) remaining_cycles = 0;

  // --- Determine State ---
  cutoff = false;
  charging = false;
  reason = "Normal";

  if (voltage > 15.0) {
    cutoff = true;
    reason = "Over Voltage";
  } else if (tempC > MAX_TEMP) {
    cutoff = true;
    reason = "Over Temp";
  } else if (soh < MIN_SOH) {
    cutoff = true;
    reason = "Low SOH";
  } else if (voltage > 6.0 && voltage <= 15.0) {
    charging = true;
    reason = "Charging";
  }

  digitalWrite(RELAY_PIN, cutoff ? HIGH : LOW);

  static unsigned long lastUpdate = 0;
  if (millis() - lastUpdate >= 1000) { // Update every 1 second
    lastUpdate = millis();
    lcd.clear();
    if (cutoff) {
      lcd.setCursor(0, 0);
      lcd.print("CUT-OFF ACTIVE!");
      lcd.setCursor(0, 1);
      lcd.print(reason);
      delay(600);
      lcd.clear();
      lcd.setCursor(0, 0);
      lcd.print("Rem Cycles:");
      lcd.print(remaining_cycles);
      lcd.setCursor(0, 1);
      lcd.print("SOH:");
      lcd.print((int)soh);
      lcd.print("%");
    } else if (charging) {
      lcd.setCursor(0, 0);
      lcd.print("CHARGING...");
      lcd.setCursor(0, 1);
      lcd.print("V:");
      lcd.print(voltage, 2);
      lcd.print(" SOC:");
      lcd.print((int)soc);
      lcd.print("%");
    } else {
      lcd.setCursor(0, 0);
      lcd.print("SOC:");
      lcd.print((int)soc);
      lcd.print("% SOH:");
      lcd.print((int)soh);
      lcd.setCursor(0, 1);
      lcd.print("Cycles:");
      lcd.print(battery_cycles);
    }
  }

  // --- Send to server ---
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(wifiClient, serverURL); // new API usage
    http.addHeader("Content-Type", "application/json");

    // Build sanitized JSON payload
    String payload = String("{") +
      "\"voltage\":" + safeFloatStr(voltage, 2) + "," +
      "\"current\":" + safeFloatStr(current_A, 2) + "," +
      "\"power\":" + safeFloatStr(power_W, 2) + "," +
      "\"temperature\":" + safeFloatStr(tempC, 1) + "," +
      "\"soc\":" + safeFloatStr(soc, 1) + "," +
      "\"soh\":" + safeFloatStr(soh, 1) + "," +
      "\"cycles\":" + String(battery_cycles) + "," +
      "\"remaining_cycles\":" + String(remaining_cycles) + "," +
      "\"cutoff\":" + String(cutoff ? "true" : "false") + "," +
      "\"reason\":\"" + reason + "\"" +
      "}";

    Serial.println("Sending payload:");
    Serial.println(payload);

    int code = http.POST((uint8_t*)payload.c_str(), payload.length());
    if (code > 0) {
      Serial.printf("POST OK [%d]\n", code);
      String resp = http.getString();
      Serial.println("Server response:");
      Serial.println(resp);
    } else {
      Serial.printf("HTTP POST failed: %s\n", http.errorToString(code).c_str());
    }
    http.end();
  } else {
    Serial.println("WiFi not connected - skipping POST");
  }

  delay(1000); 
}
