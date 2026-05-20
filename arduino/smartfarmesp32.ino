#include <WiFi.h>
#include <PubSubClient.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <LiquidCrystal_I2C.h>
#include <Preferences.h>

// === KONFIGURASI WIFI & MQTT ===
const char* ssid = "azkiyaa";
const char* password = "dafrotfarm";
const char* mqtt_server = "broker.mqtt-dashboard.com";
const int mqtt_port = 1883;

// === PIN ===
#define SOIL_PIN      34
#define RAIN_PIN      18
#define DHTPIN        4
#define ONE_WIRE_BUS  5
#define RELAY_PIN     32
#define DHTTYPE       DHT11

// === INISIALISASI OBJEK ===
DHT dht(DHTPIN, DHTTYPE);
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature ds18b20(&oneWire);
LiquidCrystal_I2C lcd(0x27, 16, 2);
WiFiClient espClient;
PubSubClient client(espClient);
Preferences preferences;

// === TOPIK MQTT ===
const char* topic_temp     = "/dafrot-mqtt/temp";
const char* topic_humi     = "/dafrot-mqtt/humi";
const char* topic_tempds   = "/dafrot-mqtt/tempds";
const char* topic_soil     = "/dafrot-mqtt/soil";
const char* topic_rain     = "/dafrot-mqtt/rain";
const char* topic_pump_out = "/dafrot-mqtt/pump";
const char* topic_manual   = "/dafrot-mqtt/in";

// === VARIABEL ===
bool manualPump = false;

// === FUNGSI SETUP WIFI ===
void setup_wifi() {
  delay(100);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi Connected");
}

// === FUNGSI RECONNECT MQTT ===
void reconnect() {
  while (!client.connected()) {
    if (client.connect("ESP32-Dafrot")) {
      client.subscribe(topic_manual);
      Serial.println("MQTT Connected");
    } else {
      delay(1000);
    }
  }
}

// === CALLBACK MQTT (Pesan Manual Control) ===
void callback(char* topic, byte* payload, unsigned int length) {
  payload[length] = '\0';
  String msg = String((char*)payload);
  msg.toLowerCase(); // Konversi ke lowercase untuk konsistensi

  if (String(topic) == topic_manual) {
    if (msg == "true" || msg == "1") {
      manualPump = true;
      digitalWrite(RELAY_PIN, HIGH); // POMPA MATI saat manual aktif
    } else {
      manualPump = false;
      // Relay akan diatur oleh kontrol otomatis di loop()
    }
    preferences.putBool("manualPump", manualPump); // Simpan status ke NVS
  }
}

// === SETUP AWAL ===
void setup() {
  Serial.begin(115200);
  dht.begin();
  ds18b20.begin();
  lcd.init();
  lcd.backlight();
  lcd.clear();

  pinMode(SOIL_PIN, INPUT);
  pinMode(RAIN_PIN, INPUT_PULLUP);
  pinMode(RELAY_PIN, OUTPUT);

  setup_wifi();
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);

  // Baca status manual terakhir dari NVS
  preferences.begin("dafrot", false);
  manualPump = preferences.getBool("manualPump", false);
  digitalWrite(RELAY_PIN, manualPump ? HIGH : LOW); // Sesuaikan inisialisasi relay
}

// === LOOP UTAMA ===
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // === BACA SENSOR ===
  float temp = dht.readTemperature();
  float humi = dht.readHumidity();
  ds18b20.requestTemperatures();
  float tempDS = ds18b20.getTempCByIndex(0);
  int soilAnalog = analogRead(SOIL_PIN);
  int soilPercent = map(soilAnalog, 4095, 0, 0, 100);
  int rainStatus = digitalRead(RAIN_PIN);  // 0 = Hujan, 1 = Cerah

  // === VALIDASI DATA ===
  bool validData = true;
  if (isnan(temp) || isnan(humi)) validData = false;
  if (tempDS == DEVICE_DISCONNECTED_C) validData = false;

  // === PUBLIKASI DATA SENSOR ===
  if (validData) {
    client.publish(topic_temp, String(temp).c_str(), false);
    client.publish(topic_humi, String(humi).c_str(), false);
    client.publish(topic_tempds, String(tempDS).c_str(), false);
  }
  client.publish(topic_soil, String(soilPercent).c_str(), false);
  client.publish(topic_rain, String(rainStatus).c_str(), false);

  // === KONTROL POMPA ===
  if (manualPump) {
    // Mode Manual: Pompa MATI (RELAY HIGH)
    client.publish(topic_pump_out, "OFF", false);
    return; // Keluar dari loop, abaikan kontrol otomatis
  } else {
    // Mode Otomatis
    if (rainStatus == 1 && soilPercent < 65) {
      digitalWrite(RELAY_PIN, LOW);  // POMPA NYALA
      client.publish(topic_pump_out, "ON", false);
    } else {
      digitalWrite(RELAY_PIN, HIGH); // POMPA MATI
      client.publish(topic_pump_out, "OFF", false);
    }
  }

  // === TAMPILAN LCD ===
  lcd.setCursor(0, 0);
  lcd.print("S:");
  lcd.print(soilPercent);
  lcd.print("% R:");
  lcd.print(rainStatus == 0 ? "H" : "C");
  lcd.print(" P:");
  lcd.print(digitalRead(RELAY_PIN) == LOW ? "ON " : "OFF");

  lcd.setCursor(0, 1);
  lcd.print("T:");
  lcd.print(temp, 1);
  lcd.print(" H:");
  lcd.print(humi, 0);
  lcd.print(" D:");
  lcd.print(tempDS, 1);

  delay(5000); // Delay 5 detik antar pembacaan
}

