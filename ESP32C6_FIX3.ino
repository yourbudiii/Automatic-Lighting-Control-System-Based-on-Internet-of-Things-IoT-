#define BLYNK_PRINT Serial

//SETTING BLYNK ID, NAME, AUTH_TOKEN
#define BLYNK_TEMPLATE_ID "TMPL6hvp_w4FR"
#define BLYNK_TEMPLATE_NAME "LIGHTING CONTROL SYSTEM"
#define BLYNK_AUTH_TOKEN "mPlKoW6uDMjuWYEXRi6pTbTSaAoHZjOu"

#include <WiFi.h>
#include <BlynkSimpleEsp32.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ThingSpeak.h>
#include <WiFiManager.h>

//------------------------------------
//SETTING WIFI
//------------------------------------
// WiFiManager wifiManager;  //Menggunakan wifi manager
char ssid[32];            // Variabel untuk menyimpan SSID
char pass[32];            // Variabel untuk menyimpan password WiFi

//----------------------------------------------- ------
//KONFIGURASI THINGSPEAK PLATFORM FOR ENERGY MONITORING
//-----------------------------------------------------
const unsigned long channelID = 2787446;       // Ganti dengan Channel ID Anda
const char* writeAPIKey = "OIEQOPWHPMDPER1L";  // Ganti dengan Write API Key Anda
const char* server = "api.thingspeak.com";

//------------------------------------
//------------------------------------
BlynkTimer timer;                    //Pengiriman ke Blynk 100ms

// NTP Client
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 25200, 60000);  // Waktu WIB (GMT+7, offset 25200 detik)

// Inisialisasi WiFi dan ThingSpeak
WiFiClient client;
WiFiManager wifiManager;  //Menggunakan WiFi manager

//------------------------------------------------
// Menggunakan Pin TX RX untuk komunikasi dengan MEGA
//-------------------------------------------------
HardwareSerial SerialMega(1);   //ESP32-C6 PIN RX (4) DAN TX (5)
#define ESP32_RX 4             //Pin LP_RX
#define ESP32_TX 5             //Pin LP_TX

bool isAutoMode = true;  // Menyimpan status mode, default Auto
bool isNoonExecuted = false; // Variabel untuk melacak status pengiriman perintah
bool isAfternoonExecuted = false;
bool isEveningExecuted = false;
bool isMorningExecuted = false;

// Virtual pins untuk relay
int relayOutputPins[] = {V1, V2, V3, V4, V5, V6, V7, V8, V9, V10, V11, V12, V13, V14, V15, V16, V17, V18, V19}; 
int lastRelayState[20] = {0};  // Inisialisasi semua relay dalam kondisi OFF

//------------------------------------
//FUNGSI UTAMA
//------------------------------------
void setup() {
  Serial.begin(115200);                                    // Inisialisasi komunikasi serial untuk monitor
  SerialMega.begin(9600, SERIAL_8N1, ESP32_RX, ESP32_TX);  // Menginisialisasi pin RX dan TX to Mega

  //-------------------------------------
  //SETTING WIFI MENGGUNAKAN WIFI MANAGER
  //-------------------------------------
  Serial.println("Lighting Control System");
  Serial.println("Configuring WiFi");

  // // Uncomment baris berikut jika ingin menghapus kredensial WiFi yang disimpan sebelumnya
  wifiManager.resetSettings();

  // Memulai portal konfigurasi WiFi
  if (!wifiManager.autoConnect("Lighting Control-AP", "password123")) {
    Serial.println("Gagal menghubungkan ke WiFi");
    ESP.restart();  // Restart ESP32 jika gagal
  }

  // Jika berhasil terhubung ke WiFi, dapatkan SSID dan password yang digunakan Blynk
  strcpy(ssid, WiFi.SSID().c_str());  // Menyalin SSID yang terhubung ke variabel ssid
  strcpy(pass, WiFi.psk().c_str());   // Menyalin password yang digunakan ke variabel pass

  // Jika berhasil terhubung ke WiFi
  Serial.println("WiFi Tersambung!");
  Serial.print("SSID :");
  Serial.println(ssid);
  Serial.print("IP : ");
  Serial.println(WiFi.localIP());
  Serial.print("Hostname: ");
  Serial.println(WiFi.getHostname());

  Blynk.begin(BLYNK_AUTH_TOKEN, ssid, pass, "blynk.cloud", 8080); // Menghubungkan ESP32 ke Blynk

  // Mengirimkan status mode Auto saat pertama kali
  Blynk.virtualWrite(V0, isAutoMode ? 1 : 0); 
  timer.setInterval(500L, sendCommandToMega); // Menjadwalkan pengiriman status mode ke Arduino setiap 500ms
  Serial.println("Mode Auto");
  
  timeClient.begin();       // Inisialisasi NTP Client
  ThingSpeak.begin(client); // Inisialisasi ThingSpeak
}

void loop() {
  Blynk.run();                  // Menjalankan Blynk untuk memperbarui status dari aplikasi
  timer.run();                  // Menjalankan timer untuk mengirimkan status ke Arduino
  timeClient.update();
  ConfigWiFi();
  sendDataToBlynkThingspeak();  // Periksa status relay dari Arduino Mega dan kirim ke Blynk

  // Jadwal pengaturan waktu
  handleSchedule(7 , 0, isMorningExecuted, "WAKTU_KERJA");               //Waktu kerja menandakan pukul 07.00
  handleSchedule(12, 0, isNoonExecuted, "WAKTU_ISTIRAHAT_MULAI");         //Waktu istirahat menandakan pukul 12.00
  handleSchedule(13, 0, isAfternoonExecuted, "WAKTU_ISTIRAHAT_SELESAI");  //Waktu istirahat selesai menandakan pukul 13.00
  handleSchedule(17, 0, isEveningExecuted, "WAKTU_PULANG");               //Waktu pulang menandakan pukul 17.00
}

void handleSchedule(int targetHour, int targetMinute, bool &flag, const String &command) {
  if (timeClient.getHours() == targetHour && timeClient.getMinutes() == targetMinute && !flag) {
    for (int i = 0; i < 3; i++) {  // Kirim 3 kali untuk memastikan keterbacaan di Arduino Mega
      SerialMega.println(command);
      Serial.println("Command sent: " + command + " at " + timeClient.getFormattedTime() + " (Attempt " + String(i + 1) + ")");
      delay(500);  // Delay untuk memastikan Mega bisa membaca data dengan stabil
    }
    flag = true;  // Set flag agar tidak mengirim ulang dalam menit yang sama
  }

  // Reset flag saat jam berganti
  if (timeClient.getMinutes() != targetMinute) {
    flag = false;
  }
}

//------------------------------------
//FUNGSI KIRIM KE ARDUINO MEGA
//------------------------------------
//Kirim ke Mega menggunakan serial UART
void sendCommandToMega() {
  if (isAutoMode) {
    SerialMega.println("AUTO");  // Mengirim status Auto ke Arduino Mega
  } else {
    SerialMega.println("MANUAL");  // Mengirim status Manual ke Arduino Mega
  }
}

//------------------------------------
//FUNGSI UPDATE STATUS BLYNK
//------------------------------------
// Kirim status relay ke Virtual Pin i di Blynk
void sendDataToBlynkThingspeak() {
  // Periksa apakah ada data dari Arduino Mega
  if (SerialMega.available() > 0) {
    String data = SerialMega.readStringUntil('\n');   // Baca data sampai newline
    data.trim();                                      // Hapus karakter tak perlu
    Serial.println("Data diterima: " + data);         // Debugging

    // Proses data relay
    if (data.startsWith("RELAY_")) {
      int relayIndex = data.substring(6, data.indexOf(':')).toInt();  // Ambil nomor relay
      int relayState = data.substring(data.indexOf(':') + 1).toInt(); // Ambil status relay

      // Pastikan index valid dan hanya update jika status relay berubah
      if (relayIndex >= 0 && relayIndex < 20 && relayState != lastRelayState[relayIndex]) {
        int virtualPin = relayOutputPins[relayIndex];   // Ambil virtual pin yang sesuai
        Blynk.virtualWrite(virtualPin, relayState);     // Kirim status relay ke Virtual Pin
        lastRelayState[relayIndex] = relayState;        // Simpan status terakhir relay
        Serial.print("Update Blynk: RELAY_");
        Serial.print(relayIndex);
        Serial.print(" -> ");
        Serial.println(relayState);
      }
    }
    
    // Proses data power meter
    else if (data.startsWith("DATA:")) {
      sendDataToThingSpeak(data);  // Kirim data ke ThingSpeak
    }
  }
}

//------------------------------------
//KIRIM DATA POWER METER KE THINGSPEAK
//------------------------------------
void sendDataToThingSpeak(String data) {
  // Validasi format data Power Meter
  if (!data.startsWith("DATA:")) {
    Serial.println("Invalid data power meter format, skipping...");
    return;  // Abaikan data jika format tidak sesuai
  }

  // Parsing data berdasarkan format
  float voltage = extractValue(data, 'V');
  float current = extractValue(data, 'C');
  float power = extractValue(data, 'P');
  float energy = extractValue(data, 'E');
  float frequency = extractValue(data, 'F');

  // Menampilkan data untuk debugging
  Serial.println("Sending to ThingSpeak:");
  Serial.println("Voltage       : " + String(voltage) + " V");
  Serial.println("Current       : " + String(current) + " A");
  Serial.println("Power         : " + String(power) + " W");
  Serial.println("Energy        : " + String(energy) + " kWh");
  Serial.println("Frequency     : " + String(frequency) + " Hz");
  
  // Kirim data ke ThingSpeak
  ThingSpeak.setField(1, voltage);
  ThingSpeak.setField(2, current);
  ThingSpeak.setField(3, power);
  ThingSpeak.setField(4, energy);
  ThingSpeak.setField(5, frequency);

  int response = ThingSpeak.writeFields(channelID, writeAPIKey);
  if (response == 200) {
    Serial.println("Data sent to ThingSpeak successfully");
  } else {
    Serial.println("Error sending data to ThingSpeak: " + String(response));
  }
}

// Fungsi untuk mengekstrak nilai berdasarkan parameter
float extractValue(String data, char parameter) {
  int startIndex = data.indexOf(parameter + String(':'));  // Cari parameter (contoh: "V:")
  if (startIndex == -1) return 0;                          // Jika parameter tidak ditemukan, kembalikan 0
  startIndex += 2;                                         // Lewati "X:" (contoh: "V:")
  int endIndex = data.indexOf(',', startIndex);            // Cari delimiter berikutnya (koma)
  if (endIndex == -1) endIndex = data.length();            // Jika tidak ada koma, ambil sampai akhir string
  return data.substring(startIndex, endIndex).toFloat();   // Ambil nilai sebagai float
}

//--------------------------------------------------------------------
//FUNGSI UNTUK KONFIGURASI WIFI (SEND : OPEN_CONFIG IN SERIAL MONITOR)
//--------------------------------------------------------------------
void ConfigWiFi() {
  // Periksa perintah dari Serial Monitor untuk membuka portal konfigurasi
  if (Serial.available()) {
    String command = Serial.readStringUntil('\n');
    command.trim();

    // Perintah untuk membuka portal konfigurasi WiFi
    if (command.equalsIgnoreCase("OPEN_CONFIG")) {

      // Mulai portal konfigurasi di IP yang terhubung sebelumnya
      wifiManager.startConfigPortal("Lighting Control-AP", "password123");

      // Setelah keluar dari portal, restart perangkat
      ESP.restart();
    }
  }
}

//------------------------------------
//INISIALISASI PIN VIRTUAL BLYNK
//------------------------------------
// Fungsi ini menangani perintah yang datang dari aplikasi Blynk (Virtual Pin V0 untuk mode)
BLYNK_WRITE(V0) {                                       // Virtual Pin V0 digunakan untuk mengubah mode
  int mode = param.asInt();                             // Mendapatkan nilai mode dari aplikasi Blynk
  isAutoMode = (mode == 1);                             // Jika mode = 1, maka mode Auto; jika 0, maka mode Manual
  SerialMega.println(isAutoMode ? "Mode Auto" : "Mode Manual");
  SerialMega.println(isAutoMode ? "AUTO" : "MANUAL");  // Mengirimkan mode ke Arduino Mega
  Serial.print("MODE :");                              // Mode
  Serial.println(mode);
}

BLYNK_WRITE_DEFAULT() {
  int pin = request.pin;        // Mendapatkan nomor Virtual Pin yang dikontrol
  int value = param.asInt();    // Membaca nilai (0 atau 1)

  // Pastikan hanya dapat dieksekusi jika mode MANUAL
  if (!isAutoMode) {  
    if (pin >= V1 && pin <= V19) {      // Pastikan pin sesuai dengan relay yang ada
      int relayIndex = pin - V1;        // Hitung indeks relay berdasarkan Virtual Pin
      SerialMega.print("RELAY_");
      SerialMega.print(relayIndex);
      SerialMega.print(":");
      SerialMega.println(value);
      Serial.print("Mengirim ke Mega: RELAY_");
      Serial.print(relayIndex);
      Serial.print(":");
      Serial.println(value);
    }
  } else {
    Serial.println("Perintah relay diabaikan karena mode AUTO aktif.");
  }
}
