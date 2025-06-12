#include <PZEM004Tv30.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

const int relayOutputPins[] = { 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41 };  //Inisialisasi Pin Relay '0, 1, 2 dst'
const int inputMotionPins[] = { 42, 43, 44, 45, 46, 47, 48, 49 };                                                  //Inisialisasi Pin Motion Sensor '0, 1, 2 dst'

bool isAutoMode = true;        // Default mode Auto
bool lastModeAuto = true;      // Menyimpan mode terakhir (true = AUTO, false = MANUAL)
bool isBreakTime = false;      // Flag untuk waktu istirahat
bool isWorkTime = true;        // Flag untuk waktu kerja (aktif di luar jam pulang)
bool isSelectorActive = true;  // Variabel untuk status selector (AutoSwitch dan ManualSwitch)
bool isSendingData = false;    // Flag untuk mengontrol pengiriman data

// Tambahkan flag untuk status pengiriman
bool motionStatusSent = false;    // Untuk melacak status saat gerakan terdeteksi
bool noMotionStatusSent = false;  // Untuk melacak status saat tidak ada gerakan

const int ManualSwitch = 53;  //Selector Switch posisi 1
const int AutoSwitch = 52;    //Selector Switch posisi 3
const int ManualLamp = 51;    //Pilot Lamp Merah
const int AutoLamp = 50;      //Pilot Lamp Putih

// Inisialisasi PZEM pada Arduino Mega
#define PZEM_RX_PIN 15       // RX ke PZEM
#define PZEM_TX_PIN 14       // TX ke PZEM
PZEM004Tv30 pzem(&Serial3);  // PZEM menggunakan Serial3 pada Mega

LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long previousMillisPM = 0;  // Variabel untuk menyimpan waktu sebelumnya
unsigned long intervalPM = 15000;    // Interval waktu dalam milidetik (1 detik)

unsigned long previousMillisLCD = 0;  // Variabel untuk menyimpan waktu sebelumnya
unsigned long intervalLCD = 15000;    // Interval waktu dalam milidetik (1 detik)

const unsigned long motionTimeout = 30000;   // sesuaikan timer motion sensor dalam milidetik
unsigned long motionTimers[8] = {0};         // Timer untuk masing-masing sensor
bool sensorActive[8] = {false};              // Menyimpan status aktif masing-masing sensor

int previousRelayState[20] = {0}; // Array untuk menyimpan status relay sebelumnya
int previousSensorState[8] = {0}; // Misal ada 10 sensor, sesuaikan jumlahnya

// Deklarasi variabel Global Power Meter
float voltage;
float current;
float power;
float energy;
float frequency;
float pf;

void setup() {
  Serial.begin(115200);   // Untuk komunikasi serial dengan monitor
  Serial1.begin(9600);    // Untuk komunikasi serial dengan ESP32
  Serial3.begin(9600);    // Untuk komunikasi dengan PZEM
  Wire.setClock(200000);  // Fast mode: 200 kHz
  lcd.init();
  lcd.backlight();  // Nyalakan lampu latar LCD

  pinMode(ManualSwitch, INPUT_PULLUP);  //Selector Switch
  pinMode(AutoSwitch, INPUT_PULLUP);

  pinMode(ManualLamp, OUTPUT);  //Indicator Lamp
  pinMode(AutoLamp, OUTPUT);

  // Set up pin input untuk sensor
  for (int i = 0; i < 8; i++) {
    pinMode(inputMotionPins[i], INPUT_PULLUP);  //Kondisi awal HIGH 'pin IO -> GND'
  }

  // Set up pin output untuk relay
  for (int i = 0; i < 20; i++) {
    pinMode(relayOutputPins[i], OUTPUT);
  }
}

void loop() {
  // Deteksi status selector
  if (digitalRead(AutoSwitch) == LOW || digitalRead(ManualSwitch) == LOW) {
    isSelectorActive = true;  // Selector aktif
  } else {
    isSelectorActive = false;  // Selector tidak aktif
  }

  // Jika selector aktif, abaikan command
  if (isSelectorActive) {
    if (digitalRead(AutoSwitch) == LOW) {
      isAutoMode = true;
      digitalWrite(AutoLamp, HIGH);
      digitalWrite(ManualLamp, LOW);
      //Serial.println("Mode: AUTO (Controlled by selector)");
    } else if (digitalRead(ManualSwitch) == LOW) {
      isAutoMode = false;
      digitalWrite(AutoLamp, LOW);
      digitalWrite(ManualLamp, HIGH);
      deactivateAllRelays();
      //Serial.println("Mode: MANUAL (Controlled by selector)");
    }
  } else {
    handleWireless();  // Command dari wireless
  }

  //Menangani fungsi otomatis 
  handleAutoMode();

  // Memanggil fungsi untuk mengambil data sensor
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillisPM >= intervalPM) {
    previousMillisPM = currentMillis;
    ReadPowerMeter();  // Fungsi ini dipanggil setiap 20 detik
  }

  // Memanggil fungsi untuk memperbarui data pada LCD
  if (currentMillis - previousMillisLCD >= intervalLCD) {
    previousMillisLCD = currentMillis;
    displayDataOnLCD();  // Fungsi ini dipanggil setiap 20 detik
  }
}

void handleAutoMode(){
  // Logika otomatis berdasarkan sensor
  if (isAutoMode && !isBreakTime && isWorkTime) {
    bool relayUpdated = false;

    // Loop untuk mendeteksi gerakan
    for (int i = 0; i < 8; i++) {
      int sensorState = digitalRead(inputMotionPins[i]);

      if (sensorState == LOW) {      // Jika sensor mendeteksi gerakan
        sensorActive[i] = true;      // Tandai sensor sebagai aktif
        motionTimers[i] = millis();  // Reset timer sensor ini
        activateRelay(i);            // Aktifkan relay yang sesuai
        relayUpdated = true;
      }
    }

    // Loop untuk memeriksa sensor yang telah habis waktunya
    for (int i = 0; i < 8; i++) {
      if (sensorActive[i] && millis() - motionTimers[i] >= motionTimeout) {
        deactivateRelayTertentu(i);  // Matikan hanya relay yang terkait dengan sensor i
        sensorActive[i] = false;     // Tandai sensor sebagai tidak aktif
        relayUpdated = true;
      }
    }

    // Kirim status relay ke ESP32 hanya jika ada perubahan
    if (relayUpdated) {
      sendRelayStatusToESP32();
    }

  } else if (!isWorkTime) {
    for (int i = 0; i < 20; i++) {
      digitalWrite(relayOutputPins[i], LOW);  // Jika di luar waktu kerja, pastikan semua relay mati
    }
  }
}

//Inisialisasi Pembagian Motion (Case : ) dan Relay ([i]) sesuai zona
void activateRelay(int sensorIndex) {
  switch (sensorIndex) {
    case 0:  //Contractor Pin Motion '22,23'
      digitalWrite(relayOutputPins[0], HIGH);
      break;
    case 1:  //Meeting Room1 Pin Motion '24-26'
      digitalWrite(relayOutputPins[1], HIGH);
      digitalWrite(relayOutputPins[2], HIGH);
      digitalWrite(relayOutputPins[3], HIGH);
      break;
    case 2:  //Meeting Room 2 Pin Motion '27,28'
      digitalWrite(relayOutputPins[5], HIGH);
      digitalWrite(relayOutputPins[6], HIGH);
      break;
    case 3:  //Meeting Room 3 Pin Motion '29'
      digitalWrite(relayOutputPins[7], HIGH);
      break;
    case 4:  //Pantry Room Pin Motion '30'
      digitalWrite(relayOutputPins[8], HIGH);
      break;
    case 5:  //Work/Office Room Pin Motion '31-38'
      for (int i = 9; i <= 16; i++) digitalWrite(relayOutputPins[i], HIGH);
      break;
    case 6:  //Sen.Supervisor Room Pin Motion '39'
      digitalWrite(relayOutputPins[17], HIGH);
      break;
    case 7:  //Manager Room Pin Motion '40'
      digitalWrite(relayOutputPins[18], HIGH);
      break;
  }
}

//Inisialisasi Pembagian Motion (Case : ) dan Relay ([i]) sesuai zona
void deactivateRelayTertentu(int sensorIndex) {
  switch (sensorIndex) {
    case 0:  //Contractor Pin Motion '22,23'
      digitalWrite(relayOutputPins[0], LOW);
      break;
    case 1:  //Meeting Room1 Pin Motion '24-26'
      digitalWrite(relayOutputPins[1], LOW);
      digitalWrite(relayOutputPins[2], LOW);
      digitalWrite(relayOutputPins[3], LOW);
      break;
    case 2:  //Meeting Room 2 Pin Motion '27,28'
      digitalWrite(relayOutputPins[5], LOW);
      digitalWrite(relayOutputPins[6], LOW);
      break;
    case 3:  //Meeting Room 3 Pin Motion '29'
      digitalWrite(relayOutputPins[7], HIGH);
      break;
    case 4:  //Pantry Room Pin Motion '30'
      digitalWrite(relayOutputPins[8], LOW);
      break;
    case 5:  //Work/Office Room Pin Motion '31-38'
      for (int i = 9; i <= 16; i++) digitalWrite(relayOutputPins[i], LOW);
      break;
    case 6:  //Sen.Supervisor Room Pin Motion '39'
      digitalWrite(relayOutputPins[17], LOW);
      break;
    case 7:  //Manager Room Pin Motion '40'
      digitalWrite(relayOutputPins[18], LOW);
      break;
  }
}

// Menonaktifkan semua relay
void deactivateAllRelays() {
  for (int i = 0; i < 20; i++) digitalWrite(relayOutputPins[i], LOW);
}

// Fungsi untuk mengirim status relay ke ESP32
void sendRelayStatusToESP32() {
  if (!isSendingData) {
    isSendingData = true;
    bool sensorChanged = false;

    // Cek perubahan pada sensor
    for (int i = 0; i < 8; i++) {  // Sesuaikan jumlah sensor
      int sensorState = digitalRead(inputMotionPins[i]); // Baca status sensor

      if (sensorState != previousSensorState[i]) {  // Jika sensor berubah
        String data = "SENSOR_" + String(i) + ":" + String(sensorState) + "\n";
        Serial.print(data);     // Tampilkan di Serial Monitor

        previousSensorState[i] = sensorState; // Update status sensor
        sensorChanged = true;
      }
    }

    // Jika ada perubahan sensor, kirim status semua relay
    if (sensorChanged) {
      for (int i = 0; i < 20; i++) {
        int relayState = digitalRead(relayOutputPins[i]);  // Baca status relay
        String data = "RELAY_" + String(i) + ":" + String(relayState) + "\n";
        Serial1.println(data);  // Kirim data relay ke ESP32
        Serial.print(data);     // Tampilkan di Serial Monitor
        previousRelayState[i] = relayState; // Update status relay sebelumnya
      }
    }

    isSendingData = false;
  }
}

//Fungsi untuk IoT dengan kendali dari ESP32
void handleWireless() {
  // Membaca perintah dari serial
  if (Serial1.available()) {
    String command = Serial1.readStringUntil('\n');  // Membaca perintah sampai newline
    command.trim();                                  // Menghapus spasi dan karakter baris baru

      if (command == "AUTO") {
        isAutoMode = true;
        lastModeAuto = true;
        digitalWrite(AutoLamp, HIGH);
        digitalWrite(ManualLamp, LOW);
        //Serial.println("Mode: AUTO (Controlled by Wireless)");
      } else if (command == "MANUAL") {
        isAutoMode = false;
        lastModeAuto = false;
        digitalWrite(AutoLamp, LOW);
        digitalWrite(ManualLamp, HIGH);
        //Serial.println("Mode: MANUAL (Controlled by Wireless)");
      }
      
    // Jika perintah lain seperti "RELAY", proses tanpa mengganggu perubahan mode
    else if (command != "AUTO" && command != "MANUAL") {
      Serial.println("Command received: " + command);
    }

    // Menangani kontrol relay manual
    if (command.startsWith("RELAY_")) {
      int relayNumber = command.substring(6, command.indexOf(":")).toInt();  // Mendapatkan nomor relay
      int state = command.substring(command.indexOf(":") + 1).toInt();       // Mendapatkan status relay (0/1)
      digitalWrite(relayOutputPins[relayNumber], state);                     // Mengaktifkan atau menonaktifkan relay sesuai status
    }

    // Kontrol waktu istirahat
    if (command == "WAKTU_ISTIRAHAT_MULAI") {
      for (int i = 4; i < 20; i++) digitalWrite(relayOutputPins[i], LOW);
      isBreakTime = true;         // Aktifkan mode istirahat
      Serial.println("WAKTU_ISTIRAHAT_MULAI");
      if (lastModeAuto) {
        isAutoMode = true;
        lastModeAuto = true;
        digitalWrite(AutoLamp, HIGH);
        digitalWrite(ManualLamp, LOW);
        Serial.println("Mode: AUTO WAKTU_KERJA");
      } else {
        isAutoMode = false;
        lastModeAuto = false;
        digitalWrite(AutoLamp, LOW);
        digitalWrite(ManualLamp, HIGH);
        Serial.println("Mode: MANUAL WAKTU_KERJA");
      }
    }

    if (command == "WAKTU_ISTIRAHAT_SELESAI") {
      isBreakTime = false;  // Matikan mode istirahat
      if (lastModeAuto) {
        isAutoMode = true;  // Kembali ke mode AUTO
        lastModeAuto = true;
        digitalWrite(AutoLamp, HIGH);
        digitalWrite(ManualLamp, LOW);
      } else {
        isAutoMode = false;  // Kembali ke mode MANUAL
        lastModeAuto = false;
        digitalWrite(AutoLamp, LOW);
        digitalWrite(ManualLamp, HIGH);
      }
      Serial.println("WAKTU_ISTIRAHAT_SELESAI");
    }

    // Kontrol waktu pulang
    if (command == "WAKTU_PULANG") {
      for (int i = 0; i < 20; i++) digitalWrite(relayOutputPins[i], LOW);
      isWorkTime = false;         // Nonaktifkan mode kerja
      Serial.println("WAKTU_PULANG");
      if (lastModeAuto) {
        isAutoMode = true;
        lastModeAuto = true;
        digitalWrite(AutoLamp, HIGH);
        digitalWrite(ManualLamp, LOW);
        Serial.println("Mode: AUTO WAKTU_KERJA");
      } else {
        isAutoMode = false;
        lastModeAuto = false;
        digitalWrite(AutoLamp, LOW);
        digitalWrite(ManualLamp, HIGH);
        Serial.println("Mode: MANUAL WAKTU_KERJA");
      }
    }

    // Kontrol waktu kerja
    if (command == "WAKTU_KERJA") {
      isWorkTime = true;  // Aktifkan mode kerja
      if (lastModeAuto) {
        isAutoMode = true;
        lastModeAuto = true;
        digitalWrite(AutoLamp, HIGH);
        digitalWrite(ManualLamp, LOW);
        Serial.println("Mode: AUTO WAKTU_KERJA");
      } else {
        isAutoMode = false;
        lastModeAuto = false;
        digitalWrite(AutoLamp, LOW);
        digitalWrite(ManualLamp, HIGH);
        Serial.println("Mode: MANUAL WAKTU_KERJA");
      }
    }
  }
}

void ReadPowerMeter() {
  if (!isSendingData) {
    isSendingData = true;

    // Membaca data dari PZEM
    voltage = pzem.voltage();
    current = pzem.current();
    power = pzem.power();
    energy = pzem.energy();
    frequency = pzem.frequency();
    float pf = pzem.pf();  // Baca Power Factor

    // Format data sebagai string
    String data = "DATA:"
                  + String("V:") + String(voltage, 2)
                  + String(",C:") + String(current, 2)
                  + String(",P:") + String(power, 2)
                  + String(",E:") + String(energy, 2)
                  + String(",F:") + String(frequency, 2)
                  + String(",Q:") + String(pf, 2);  // Perbaikan di sini

    Serial.println(data);
    Serial1.println(data);  //Kirim data ReadPZEM ke ESP32

    isSendingData = false;  // Reset flag setelah pengiriman data selesai
  }
}

void displayDataOnLCD() {
  lcd.print(" ");
  lcd.setCursor(0, 0);  // Baris 1
  lcd.print("V: ");
  lcd.print(voltage, 2);  // 2 desimal
  //lcd.print(" V");    // Clear extra characters

  lcd.setCursor(1, 0);  // Baris 2
  lcd.print("I: ");
  lcd.print(current, 2);  // 2 desimal
  //lcd.print(" A");    // Clear extra characters

  lcd.setCursor(2, 0);  // Baris 3
  lcd.print("P: ");
  lcd.print(power, 2);  // 2 desimal
  //lcd.print(" W");    // Clear extra characters

  lcd.setCursor(3, 0);  // Baris 4
  lcd.print("E: ");
  lcd.print(energy, 2);  // 2 desimal
  lcd.print(" kWh");     // Clear extra characters
}