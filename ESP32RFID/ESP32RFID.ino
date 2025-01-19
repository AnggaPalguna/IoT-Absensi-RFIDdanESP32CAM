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

FirebaseData fbdo;
FirebaseData fbdo_check;
FirebaseAuth auth;
FirebaseConfig config;

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

struct WorkingHours {
  int checkInStart_hour = 8;
  int checkInStart_minute = 0;
  int checkInEnd_hour = 9;
  int checkInEnd_minute = 30;
  int checkOutStart_hour = 14;
  int checkOutStart_minute = 0;
  int checkOutEnd_hour = 15;
  int checkOutEnd_minute = 0;
};

WorkingHours workingHours;

// Fungsi untuk mendapatkan tanggal dalam format YYYY-MM-DD
String getCurrentDate() {
  struct tm timeinfo;
  char dateString[11];
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);
  sprintf(dateString, "%04d-%02d-%02d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  return String(dateString);
}

String getCurrentTime() {
  struct tm timeinfo;
  char timeString[6];
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);
  return String(timeString);
}

bool isValidUID(const String &uid) {
  char path[100];
  sprintf(path, FIREBASE_PATH_EMPLOYEE_STATUS, uid.c_str());

  if (Firebase.RTDB.getString(&fbdo_check, path)) {
    if (fbdo_check.dataType() == "string") {
      String status = fbdo_check.stringData();
      return status == "Active";
    }
  }
  return false;
}

String getEmployeeName(const String &uid) {
  char path[100];
  sprintf(path, FIREBASE_PATH_EMPLOYEE_NAME, uid.c_str());

  if (Firebase.RTDB.getString(&fbdo_check, path)) {
    if (fbdo_check.dataType() == "string") {
      return fbdo_check.stringData();
    }
  }
  return "Unknown";
}

bool isValidCheckInTime(int hour, int minute) {
  int totalMinutes = hour * 60 + minute;
  int startMinutes = workingHours.checkInStart_hour * 60 + workingHours.checkInStart_minute;
  int endMinutes = workingHours.checkInEnd_hour * 60 + workingHours.checkInEnd_minute;
  return totalMinutes >= startMinutes && totalMinutes <= endMinutes;
}

bool isValidCheckOutTime(int hour, int minute) {
  int totalMinutes = hour * 60 + minute;
  int startMinutes = workingHours.checkOutStart_hour * 60 + workingHours.checkOutStart_minute;
  int endMinutes = workingHours.checkOutEnd_hour * 60 + workingHours.checkOutEnd_minute;
  return totalMinutes >= startMinutes && totalMinutes <= endMinutes;
}

String determineCheckInType(int hour, int minute) {
  if (!isValidCheckInTime(hour, minute)) {
    if (hour > workingHours.checkInEnd_hour ||
        (hour == workingHours.checkInEnd_hour && minute > workingHours.checkInEnd_minute)) {
      return "late";
    }
    return "invalid_time";
  }
  return "null";
}

String determineCheckOutType(int hour, int minute) {
  if (!isValidCheckOutTime(hour, minute)) {
    if (hour < workingHours.checkOutStart_hour) {
      return "early";
    }
    return "invalid_time";
  }
  return "null";
}

void loadWorkingHours() {
  if (Firebase.RTDB.getString(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/checkIn/start"))) {
    String checkInStart = fbdo.stringData();
    sscanf(checkInStart.c_str(), "%d:%d", &workingHours.checkInStart_hour, &workingHours.checkInStart_minute);
  }
  if (Firebase.RTDB.getString(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/checkIn/end"))) {
    String checkInEnd = fbdo.stringData();
    sscanf(checkInEnd.c_str(), "%d:%d", &workingHours.checkInEnd_hour, &workingHours.checkInEnd_minute);
  }
  if (Firebase.RTDB.getString(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/checkOut/start"))) {
    String checkOutStart = fbdo.stringData();
    sscanf(checkOutStart.c_str(), "%d:%d", &workingHours.checkOutStart_hour, &workingHours.checkOutStart_minute);
  }
  if (Firebase.RTDB.getString(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/checkOut/end"))) {
    String checkOutEnd = fbdo.stringData();
    sscanf(checkOutEnd.c_str(), "%d:%d", &workingHours.checkOutEnd_hour, &workingHours.checkOutEnd_minute);
  }
}

void setup() {
  Serial.begin(115200);
  SPI.begin();
  mfrc522.PCD_Init();

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Menghubungkan WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  Serial.println("\nTerhubung ke WiFi");
  Serial.println("IP: " + WiFi.localIP().toString());

  configTime(7 * 3600, 0, "pool.ntp.org"); // GMT+7

  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  config.token_status_callback = tokenStatusCallback;

  loadWorkingHours();
}

void loop() {
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial()) {
    return;
  }

  String uid = "";
  for (byte i = 0; i < mfrc522.uid.size; i++) {
    if (mfrc522.uid.uidByte[i] < 0x10)
      uid += "0";
    uid += String(mfrc522.uid.uidByte[i], HEX);
  }
  uid.toUpperCase();

  if (Firebase.ready()) {
    String currentDate = getCurrentDate();
    String currentTime = getCurrentTime();
    
    int hour, minute;
    sscanf(currentTime.c_str(), "%d:%d", &hour, &minute);

    if (isValidUID(uid)) {
      String name = getEmployeeName(uid);
      String attendancePath = String(FIREBASE_PATH_ATTENDANCE) + "/" + uid + "/" + currentDate;

      // Cek apakah sudah ada data attendance hari ini
      if (Firebase.RTDB.getString(&fbdo_check, attendancePath + "/checkIn/time")) {
        // Sudah ada check-in, cek apakah sudah check-out
        if (!Firebase.RTDB.getString(&fbdo_check, attendancePath + "/checkOut/time")) {
          // Belum check-out, lakukan check-out
          String checkOutType = determineCheckOutType(hour, minute);
          bool isValid = checkOutType == "null" || checkOutType == "early";

          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/time", currentTime);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/photoUrl", "https://example.com/photo.jpg");
          Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkOut/isValid", isValid);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/type", checkOutType);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/name", name);

          Serial.println("Check-out: " + name + " (" + checkOutType + ")");
        } else {
          Serial.println("Sudah melakukan check-out hari ini");
        }
      } else {
        // Belum ada check-in
        bool isCheckOutTime = hour >= workingHours.checkOutStart_hour;
        String checkInType = isCheckOutTime ? "late" : determineCheckInType(hour, minute);
        bool isValid = checkInType == "null" || checkInType == "late";

        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/time", currentTime);
        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/photoUrl", "https://example.com/photo.jpg");
        Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkIn/isValid", isValid);
        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/type", checkInType);
        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/name", name);

        // Jika tap di waktu check-out tanpa check-in sebelumnya
        if (isCheckOutTime) {
          String checkOutType = determineCheckOutType(hour, minute);
          bool isValidCheckOut = checkOutType == "null" || checkOutType == "early";

          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/time", currentTime);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/photoUrl", "https://example.com/photo.jpg");
          Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkOut/isValid", isValidCheckOut);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/type", checkOutType);
          Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/name", name);

          Serial.println("Auto check-in dan check-out: " + name);
        } else {
          Serial.println("Check-in: " + name + " (" + checkInType + ")");
        }
      }
    } else {
      Firebase.RTDB.setString(&fbdo, String(FIREBASE_PATH_UNREGISTERED) + "/" + uid, uid);
      Serial.println("UID tidak terdaftar: " + uid);
    }
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();
  delay(1000);
}