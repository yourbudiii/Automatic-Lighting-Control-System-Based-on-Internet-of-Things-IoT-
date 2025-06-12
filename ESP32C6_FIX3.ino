#include <WiFi.h>
#include <PubSubClient.h>
#include <WiFiManager.h>
#include <WiFiClientSecure.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Konfigurasi WiFi via WiFi Manager
WiFiManager wifiManager;
char ssid[32];
char pass[32];

// Konfigurasi MQTT Broker
const char* mqttServer = "99dea6c09ad3478696b5981a6d4ec886.s1.eu.hivemq.cloud";
const int mqttPort = 8883;
const char* mqtt_user = "adminLCS";
const char* mqtt_pass = "adminLCS123";
const char* mqttClientID = "LCS_relay_client";
const char* mqttControlTopic = "LCS/relayLCS/control";
const char* mqttModeTopic = "LCS/relayLCS/mode/command";
const char* mqttSensorPMTopic = "LCS/sensorLCS/PM/";
const char* mqttRelayTopic = "LCS/relayLCS/status/";

bool isAutoMode = true;
bool isNoonExecuted = false;
bool isAfternoonExecuted = false;
bool isEveningExecuted = false;
bool isMorningExecuted = false;

// Variabel global untuk flag
String mode = "AUTO";  // Mode default

//-----------------------------------------------
// Inisialisasi WiFi dan MQTT
WiFiClientSecure espClient;
PubSubClient client(espClient);

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);  // GMT+7

// Relay
#define NUM_RELAYS 20
int relayStates[NUM_RELAYS];

HardwareSerial SerialMega(1);
#define ESP32_RX 16
#define ESP32_TX 17

void setup() {
  Serial.begin(9600);
  SerialMega.begin(9600, SERIAL_8N1, ESP32_RX, ESP32_TX);

  connectWiFiManager();
  espClient.setInsecure();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
  connectMQTT();

  timeClient.begin();
}

void loop() {
  if (!client.connected()) {
    connectMQTT();
  }
  timeClient.update();
  sendStatusRelayDataSensor();
  client.loop();
  ConfigWiFi();

  // Jadwal pengaturan waktu
  //handleSchedule(17, 0, isMorningExecuted, "WAKTU_KERJA");                //Waktu kerja menandakan pukul 07.00
  //handleSchedule(7, 41, isNoonExecuted, "WAKTU_ISTIRAHAT_MULAI");         //Waktu istirahat menandakan pukul 12.00
  //handleSchedule(7, 45, isAfternoonExecuted, "WAKTU_ISTIRAHAT_SELESAI");  //Waktu istirahat selesai menandakan pukul 13.00
  //handleSchedule(6 , 0, isEveningExecuted, "WAKTU_PULANG");               //Waktu pulang menandakan pukul 17.00
}

void handleSchedule(int targetHour, int targetMinute, bool &flag, const String &command) {
  if (timeClient.getHours() == targetHour && timeClient.getMinutes() == targetMinute && !flag) {
    for (int i = 0; i < 3; i++) {
      SerialMega.println(command);
      delay(500);
    }
    flag = true;
  }
  if (timeClient.getMinutes() != targetMinute) {
    flag = false;
  }
}

void connectWiFiManager() {
  Serial.println("Configuring WiFi");
  if (!wifiManager.autoConnect("Lighting Control-AP", "password123")) {
    ESP.restart();
  }
  strcpy(ssid, WiFi.SSID().c_str());
  strcpy(pass, WiFi.psk().c_str());

  Serial.println("WiFi Tersambung!");
  Serial.print("SSID : ");
  Serial.println(ssid);
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
}

void connectMQTT() {
  int retryCount = 0;
  while (!client.connected() && retryCount < 10) {
    Serial.println("Menghubungkan ke MQTT...");
    if (client.connect(mqttClientID, mqtt_user, mqtt_pass)) {
      Serial.println("Terhubung ke MQTT Broker!");
      client.subscribe(mqttControlTopic);
      client.subscribe(mqttModeTopic);
      return;
    } else {
      Serial.println("Gagal terhubung. Mencoba lagi...");
      delay(2000);
      retryCount++;
    }
  }
  if (!client.connected()) {
    ESP.restart();
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }

  if (String(topic) == mqttModeTopic) {
    if (message == "AUTO" || message == "MANUAL") {
      mode = message;

      SerialMega.println(mode);
      Serial.println("Mode diubah menjadi: " + mode);
    }
  }

  else if (String(topic) == mqttControlTopic) {
    if (message.startsWith("RELAY_")) {
      int colonIndex = message.indexOf(':');
      if (colonIndex != -1) {
        String relayInfo = message.substring(0, colonIndex);
        String relayAction = message.substring(colonIndex + 1);
        int relayIndex = relayInfo.substring(6).toInt();
        if (relayIndex >= 0 && relayIndex < NUM_RELAYS) {
          relayStates[relayIndex] = (relayAction == "ON") ? HIGH : LOW;
          sendRelayStatus(relayIndex);
        }
      }
    }
  }
}

void sendRelayStatus(int relayIndex) {
  String relayMessage = "RELAY_" + String(relayIndex) + ":" + (relayStates[relayIndex] == HIGH ? "1" : "0");
  SerialMega.println(relayMessage);
}

void sendStatusRelayDataSensor() {
  if (SerialMega.available() > 0) {
    String data = SerialMega.readStringUntil('\n');
    Serial.println("Data diterima: " + data);

    if (data.startsWith("RELAY_") && data.indexOf(':') != -1) {
      int colonIndex = data.indexOf(':');
      String relayInfo = data.substring(0, colonIndex);
      String relayStatus = data.substring(colonIndex + 1);
      if (relayInfo.startsWith("RELAY_")) {
        int relayIndex = relayInfo.substring(6).toInt();
        if (relayIndex >= 0 && relayIndex <= 19) {
          String mqttStatus = (relayStatus == "1") ? "ON" : "OFF";
          String topic = String(mqttRelayTopic) + relayIndex;
          client.publish(topic.c_str(), mqttStatus.c_str());
        }
      }
    }

    if (data.startsWith("DATA:")) {
      float voltage = extractValue(data, 'V');
      float current = extractValue(data, 'C');
      float power = extractValue(data, 'P');
      float energy = extractValue(data, 'E');
      float frequency = extractValue(data, 'F');
      float powerfactor = extractValue(data, 'Q');

      client.publish((String(mqttSensorPMTopic) + "voltage").c_str(), String(voltage).c_str());
      client.publish((String(mqttSensorPMTopic) + "current").c_str(), String(current).c_str());
      client.publish((String(mqttSensorPMTopic) + "power").c_str(), String(power).c_str());
      client.publish((String(mqttSensorPMTopic) + "energy").c_str(), String(energy).c_str());
      client.publish((String(mqttSensorPMTopic) + "frequency").c_str(), String(frequency).c_str());
      client.publish((String(mqttSensorPMTopic) + "powerfactor").c_str(), String(powerfactor).c_str());
    }
  }
}

float extractValue(String data, char parameter) {
  int startIndex = data.indexOf(parameter + String(':'));
  if (startIndex == -1) return 0;
  startIndex += 2;
  int endIndex = data.indexOf(',', startIndex);
  if (endIndex == -1) endIndex = data.length();
  return data.substring(startIndex, endIndex).toFloat();
}

void ConfigWiFi() {
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();
    if (command.equalsIgnoreCase("OPEN_CONFIG")) {
      wifiManager.startConfigPortal("Lighting Control-AP", "password123");
      ESP.restart();
    }
  }
}
