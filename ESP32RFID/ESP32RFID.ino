#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h>

#include "firebase_config.h"

// Provide the token generation process info.
#include <addons/TokenHelper.h>

// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

// Deklarasi objek Firebase
FirebaseData fbdo;
FirebaseData fbdo_check;
FirebaseAuth auth;
FirebaseConfig config;

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

unsigned long sendDataPrevMillis = 0;
bool signupOK = false;


// Fungsi untuk mengecek apakah UID valid
bool isValidUID(const String& uid) {
  char path[100];
  sprintf(path, FIREBASE_PATH_DATA_STATUS, uid.c_str());
  
  if (Firebase.RTDB.getString(&fbdo_check, path)) {
    if (fbdo_check.dataType() == "string") {
      String status = fbdo_check.stringData();
      return status == "active";
    }
  }
  return false;
}

// Fungsi untuk mendapatkan nama pemilik UID
String getUIDOwnerName(const String& uid) {
  char path[100];
  sprintf(path, FIREBASE_PATH_DATA_NAME, uid.c_str());
  
  if (Firebase.RTDB.getString(&fbdo_check, path)) {
    if (fbdo_check.dataType() == "string") {
      return fbdo_check.stringData();
    }
  }
  return "Unknown";
}


void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  // Koneksi WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nTerhubung ke WiFi");
  Serial.println("IP: " + WiFi.localIP().toString());

  // Konfigurasi waktu
  configTime(8 * 3600, 0, "pool.ntp.org"); // GMT+7

  // Konfigurasi Firebase
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  // Autentikasi pengguna (opsional)
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  // Inisialisasi Firebase dengan konfigurasi
  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  // Aktifkan penyimpanan token di EEPROM untuk ESP32
  config.token_status_callback = tokenStatusCallback;
}

void loop() {
  // Cek kartu RFID baru
  if (!mfrc522.PICC_IsNewCardPresent()) {
    return;
  }

  // Pilih kartu
  if (!mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  // Ambil UID kartu
  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10) {
      uid += "0"; // Menambahkan leading zero
    }
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  // Kirim UID ke Firebase dan cek validitas
  if (Firebase.ready()) {
    // Simpan UID terakhir yang di-scan
    if (Firebase.RTDB.setString(&fbdo, FIREBASE_PATH_LAST_SCAN, uid)) {
      Serial.println("UID terkirim: " + uid);
      
      // Cek apakah UID valid
      if (isValidUID(uid)) {
        String ownerName = getUIDOwnerName(uid);
        String ownerImage = "/random.jpg";
        unsigned long timestamp =time(nullptr);
        
        Serial.println("UID valid!");
        Serial.println("Pemilik: " + ownerName);
        
        // Tambahkan log akses berhasil ke Firebase
        String accessPath = String(FIREBASE_PATH_ACCESS_LOGS) + "/" + uid + "/" + String(millis());
        
        // Menyimpan data log lengkap
        Firebase.RTDB.setString(&fbdo, accessPath + "/status", "success");
        Firebase.RTDB.setString(&fbdo, accessPath + "/timestamp", timestamp);
        Firebase.RTDB.setString(&fbdo, accessPath + "/name", ownerName);
        if (ownerImage.length() > 0) {
          Firebase.RTDB.setString(&fbdo, accessPath + "/image_url", ownerImage);
        }
        
      } else {
        Serial.println("UID tidak valid!");
        unsigned long timestamp = time(nullptr);
        
        // Tambahkan log akses gagal ke Firebase
        String accessPath = String(FIREBASE_PATH_ACCESS_LOGS) + "/" + uid + "/" + String(millis());
        Firebase.RTDB.setString(&fbdo, accessPath + "/status", "failed");
        Firebase.RTDB.setString(&fbdo, accessPath + "/timestamp", timestamp);
      }
    } else {
      Serial.println("Gagal mengirim UID");
      Serial.println(fbdo.errorReason());
    }
  }

  // Hentikan pembacaan kartu
  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(1000);
}