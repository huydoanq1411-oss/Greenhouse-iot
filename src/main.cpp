#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <LiquidCrystal_I2C.h>

// ---------- Chân ----------
#define DHTPIN 15
#define DHTTYPE DHT22
#define SOIL_PIN 34
#define LDR_PIN 35
#define FAN_PIN 26
#define PUMP_PIN 27
#define LIGHT_PIN 25
#define BUZZER_PIN 14

// ---------- WiFi + MQTT ----------
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
// Phải trùng với TOPIC_PREFIX trong dashboard.html
const char* TOPIC_DATA = "qhuy-greenhouse/data";
const char* TOPIC_CMD  = "qhuy-greenhouse/cmd";
const char* TOPIC_STATUS = "qhuy-greenhouse/status";

// ---------- Ngưỡng (có hysteresis) ----------
const float TEMP_ON  = 32.0, TEMP_OFF  = 30.0;
const int   SOIL_ON  = 35,   SOIL_OFF  = 45;   // bật bơm khi đất khô dưới 35%
const int   LIGHT_ON = 30,   LIGHT_OFF = 40;   // bật đèn khi tối dưới 30%

DHT dht(DHTPIN, DHTTYPE);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

bool autoMode = true;
bool fanOn = false, pumpOn = false, lampOn = false;
unsigned long lastRead = 0, lastReconnect = 0;

// Nhận lệnh từ dashboard
void callback(char* topic, byte* payload, unsigned int length) {
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  msg.toUpperCase();
  Serial.println("CMD: " + msg);

  if (msg == "AUTO") {
    autoMode = true;
  } else if (msg == "MANUAL") {
    autoMode = false;
  } else if (msg == "FAN_ON" || msg == "FAN_OFF" ||
             msg == "PUMP_ON" || msg == "PUMP_OFF" ||
             msg == "LAMP_ON" || msg == "LAMP_OFF") {
    autoMode = false;
    if (msg == "FAN_ON") fanOn = true;
    if (msg == "FAN_OFF") fanOn = false;
    if (msg == "PUMP_ON") pumpOn = true;
    if (msg == "PUMP_OFF") pumpOn = false;
    if (msg == "LAMP_ON") lampOn = true;
    if (msg == "LAMP_OFF") lampOn = false;
  } else {
    Serial.println("Lenh khong hop le");
    return;
  }

  // Áp dụng ngay, không đợi chu kỳ đọc cảm biến
  digitalWrite(FAN_PIN, fanOn);
  digitalWrite(PUMP_PIN, pumpOn);
  digitalWrite(LIGHT_PIN, lampOn);
}

void connectWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);   // kênh 6 là của Wokwi-GUEST
  Serial.print("Dang ket noi WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }
  Serial.println("\nWiFi OK, IP: " + WiFi.localIP().toString());
}

void reconnectMqtt() {
  if (millis() - lastReconnect < 5000) return;   // thử lại mỗi 5 giây
  lastReconnect = millis();

  String clientId = "esp32-gh-" + String((uint32_t)ESP.getEfuseMac(), HEX);
  if (mqtt.connect(clientId.c_str(), TOPIC_STATUS, 0, true, "offline")) {
    Serial.println("MQTT OK");
    mqtt.subscribe(TOPIC_CMD);
    mqtt.publish(TOPIC_STATUS, "online", true);
  } else {
    Serial.printf("MQTT loi, rc=%d\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();
  lcd.init();
  lcd.backlight();

  pinMode(FAN_PIN, OUTPUT);
  pinMode(PUMP_PIN, OUTPUT);
  pinMode(LIGHT_PIN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  lcd.setCursor(0, 0);
  lcd.print("Dang ket noi...");
  connectWiFi();
  lcd.clear();

  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setCallback(callback);
}

void loop() {
  if (!mqtt.connected()) reconnectMqtt();
  mqtt.loop();                       // bắt buộc gọi liên tục để nhận lệnh

  if (millis() - lastRead < 2000) return;   // đọc cảm biến mỗi 2 giây
  lastRead = millis();

  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int soil  = map(analogRead(SOIL_PIN), 0, 4095, 100, 0);
  int light = map(analogRead(LDR_PIN), 0, 4095, 0, 100);

  if (isnan(t) || isnan(h)) {
    Serial.println("Loi doc DHT22!");
    return;
  }

  // Chế độ tự động: có hysteresis
  if (autoMode) {
    if (t > TEMP_ON)       fanOn = true;
    else if (t < TEMP_OFF) fanOn = false;

    if (soil < SOIL_ON)        pumpOn = true;
    else if (soil > SOIL_OFF)  pumpOn = false;

    if (light < LIGHT_ON)        lampOn = true;
    else if (light > LIGHT_OFF)  lampOn = false;
  }

  digitalWrite(FAN_PIN, fanOn);
  digitalWrite(PUMP_PIN, pumpOn);
  digitalWrite(LIGHT_PIN, lampOn);
  digitalWrite(BUZZER_PIN, t > TEMP_ON + 5);

  // LCD (ký tự cuối dòng 1: A = auto, M = manual)
  lcd.setCursor(0, 0);
  lcd.printf("T:%.1fC H:%.0f%% %s", t, h, autoMode ? "A" : "M");
  lcd.setCursor(0, 1);
  lcd.printf("S:%d%% L:%d%%  ", soil, light);

  // Gửi dữ liệu dạng JSON lên MQTT
  char json[160];
  snprintf(json, sizeof(json),
    "{\"temp\":%.1f,\"hum\":%.0f,\"soil\":%d,\"light\":%d,"
    "\"fan\":%d,\"pump\":%d,\"lamp\":%d,\"auto\":%d}",
    t, h, soil, light, fanOn, pumpOn, lampOn, autoMode);

  if (mqtt.connected()) mqtt.publish(TOPIC_DATA, json);
  Serial.println(json);
}