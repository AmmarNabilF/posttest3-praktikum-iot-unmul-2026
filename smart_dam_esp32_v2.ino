#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// --- Konfigurasi WiFi & MQTT ---
const char* ssid = "cruxx";
const char* password = "siapatawu";
const char* mqtt_server = "broker.emqx.io"; 

WiFiClient espClient;
PubSubClient client(espClient);

// --- Konfigurasi Pin ESP32-C3 ---
const int WATER_PIN = 4;
const int SERVO_PIN = 3;
const int BUZZER_PIN = 2;

Servo doorServo;

// --- Variabel Global ---
String currentMode = "AUTO"; 
int waterLevel = 0;
String statusLevel = "Aman";
int currentServoAngle = 0;
bool isBuzzerOn = false;

// Variabel Non-Blocking Timer
unsigned long previousMillis = 0;
unsigned long publishMillis = 0;
unsigned long lastReconnectAttempt = 0; // Timer untuk reconnect
const long blinkInterval = 500; 
const long publishInterval = 2000; 

void setup_wifi() {
  delay(10);
  Serial.print("Connecting to WiFi..");
  WiFi.begin(ssid, password);
  
  // Timeout WiFi agar tidak stuck selamanya
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(" Connected!");
  } else {
    Serial.println(" Failed! Berjalan Offline.");
  }
}

void callback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  msg.trim(); // PENTING: Menghapus spasi/enter tersembunyi
  String topicStr = String(topic);

  Serial.print("Pesan Masuk [");
  Serial.print(topicStr);
  Serial.print("]: ");
  Serial.println(msg);

  if (topicStr == "dam/system/mode") {
    currentMode = msg;
    Serial.println("Mode berubah menjadi: " + currentMode);
  }
  
  if (currentMode == "MANUAL") {
    if (topicStr == "dam/actuator/servo") {
      currentServoAngle = msg.toInt();
      doorServo.write(currentServoAngle);
    } 
    else if (topicStr == "dam/actuator/buzzer") {
      isBuzzerOn = (msg == "ON");
      if(!isBuzzerOn) digitalWrite(BUZZER_PIN, LOW);
    }
  }
}

boolean reconnect() {
  Serial.print("Mencoba koneksi MQTT...");
  if (client.connect("ESP32C3_DamClient_K6")) { 
    Serial.println("Terhubung!");
    client.subscribe("dam/system/mode");
    client.subscribe("dam/actuator/servo");
    client.subscribe("dam/actuator/buzzer");
    return true;
  } else {
    Serial.print("Gagal, state=");
    Serial.println(client.state());
    return false;
  }
}

void setup() {
  // AKTIFKAN SERIAL MONITOR
  Serial.begin(115200); 
  Serial.println("Memulai Sistem Bendungan Pintar...");

  pinMode(WATER_PIN, INPUT); // Biasakan deklarasi input sensor
  pinMode(BUZZER_PIN, OUTPUT);
  doorServo.attach(SERVO_PIN);
  doorServo.write(0);
  digitalWrite(BUZZER_PIN, LOW);
  
  setup_wifi();
  client.setServer(mqtt_server, 1883);
  client.setCallback(callback);
}

void loop() {
  unsigned long currentMillis = millis();

  // Non-Blocking MQTT Reconnect (Sistem tetap jalan meski offline)
  if (!client.connected()) {
    if (currentMillis - lastReconnectAttempt > 5000) {
      lastReconnectAttempt = currentMillis;
      if (reconnect()) {
        lastReconnectAttempt = 0;
      }
    }
  } else {
    client.loop();
  }

  // 1. Baca Sensor
  waterLevel = analogRead(WATER_PIN);
  
  // 2. Logika Otomatisasi (Selalu tereksekusi asalkan mode AUTO)
  if (currentMode == "AUTO") {
    if (waterLevel <= 800) {
      statusLevel = "Aman";
      currentServoAngle = 0;
      doorServo.write(currentServoAngle);
      digitalWrite(BUZZER_PIN, LOW);
      isBuzzerOn = false;
    } 
    else if (waterLevel > 800 && waterLevel <= 1500) {
      statusLevel = "Waspada";
      currentServoAngle = 90;
      doorServo.write(currentServoAngle);
      digitalWrite(BUZZER_PIN, LOW);
      isBuzzerOn = false;
    } 
    else if (waterLevel > 1500) {
      statusLevel = "Bahaya";
      currentServoAngle = 180;
      doorServo.write(currentServoAngle);
      isBuzzerOn = true;
      
      // Buzzer Kedip
      if (currentMillis - previousMillis >= blinkInterval) {
        previousMillis = currentMillis;
        digitalWrite(BUZZER_PIN, !digitalRead(BUZZER_PIN));
      }
    }
  } else {
    statusLevel = "MANUAL OVERRIDE";
    if (isBuzzerOn) {
       digitalWrite(BUZZER_PIN, HIGH); 
    } else {
       digitalWrite(BUZZER_PIN, LOW);
    }
  }

  // 3. Publish & Print ke Serial Monitor (Tiap 2 detik)
  if (currentMillis - publishMillis >= publishInterval) {
    publishMillis = currentMillis;
    
    // Tampilkan di layar komputermu
    Serial.print("Mode: "); Serial.print(currentMode);
    Serial.print(" | Level Air: "); Serial.print(waterLevel);
    Serial.print(" | Status: "); Serial.println(statusLevel);
    
    // Kirim ke MQTT jika terhubung
    if (client.connected()) {
      client.publish("dam/sensor/waterlevel", String(waterLevel).c_str());
      client.publish("dam/sensor/status", statusLevel.c_str());
      client.publish("dam/actuator/servo", String(currentServoAngle).c_str());
      client.publish("dam/actuator/buzzer", isBuzzerOn ? "ON" : "OFF");
      
      // Opsional: Pastikan Kodular tahu mode apa yang sedang aktif
      client.publish("dam/system/mode", currentMode.c_str()); 
    }
  }
}