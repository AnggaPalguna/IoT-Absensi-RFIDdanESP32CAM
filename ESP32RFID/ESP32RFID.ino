#include <Arduino.h>
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <random>

#include "firebase_config.h"

// Provide the token generation process info.
#include <addons/TokenHelper.h>

// Provide the RTDB payload printing info and other helper functions.
#include <addons/RTDBHelper.h>

#include <HardwareSerial.h>

// Initialize LCD (0x27 is the default I2C address, adjust if needed)
LiquidCrystal_I2C lcd(0x27, 16, 2);

// Firebase declarations
FirebaseData fbdo;
FirebaseData fbdo_check;
FirebaseAuth auth;
FirebaseConfig config;

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// Button debounce variables
unsigned long lastDebounceTime = 0;
unsigned long debounceDelay = 50;
int lastButtonState = HIGH;
int buttonState;

struct WorkingHours {
  int checkInStart_hour = 8;
  int checkInStart_minute = 0;
  int checkInEnd_hour = 9;
  int checkInEnd_minute = 30;
  int checkOutStart_hour = 14;
  int checkOutStart_minute = 0;
  int checkOutEnd_hour = 15;
  int checkOutEnd_minute = 0;
  int toleranceBeforeCheckIn = 0;    // dalam menit
  int toleranceAfterCheckIn = 120;   // dalam menit
  int toleranceBeforeCheckOut = 120; // dalam menit
  int toleranceAfterCheckOut = 120;  // dalam menit
  int minWorkingTime = 120;          // minimal waktu kerja dalam menit
};

WorkingHours workingHours;

HardwareSerial SerialMaster(2);

// Modify the generateRandomFilename function to also return just the random string
struct RandomFileData {
    String fullUrl;
    String randomPart;
};

RandomFileData generateRandomFilename() {
    const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    const int lengthOfRandomString = 8; // Panjang string acak
    String randomString = "";

    // Generate random string
    for (int i = 0; i < lengthOfRandomString; i++) {
        int randomIndex = random(0, strlen(charset));
        randomString += charset[randomIndex];
    }

    // Get current epoch time in GMT+8
    time_t now;
    time(&now); // Get current time in seconds since epoch (UTC)
    now += 8 * 3600; // Adjust to GMT+8 (8 hours * 3600 seconds per hour)
    String epochTime = String(now); // Convert epoch time to string

    // Combine random string and epoch time
    RandomFileData result;
    result.randomPart = randomString + "_" + epochTime; // Gabungkan string acak dan epoch time
    result.fullUrl = result.randomPart + ".jpg"; // Buat URL lengkap

    return result;
}

// Function to display message on LCD
void displayLCD(String line1, String line2) {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(line1);
  lcd.setCursor(0, 1);
  lcd.print(line2);
}

// Function to handle device reset
void handleReset() {
  int reading = digitalRead(RESET_BTN_PIN);
  
  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }
  
  if ((millis() - lastDebounceTime) > debounceDelay) {
    if (reading != buttonState) {
      buttonState = reading;
      if (buttonState == LOW) {
        displayLCD("Resetting...", "Please wait");
        delay(1000);
        ESP.restart();
      }
    }
  }
  lastButtonState = reading;
}

// Alert function for success/failure
void alert(bool isSuccess) {
  digitalWrite(BUZZER_PIN, HIGH);
  if (!isSuccess) {
    digitalWrite(LED_PIN, HIGH);
  }
  delay(200);
  digitalWrite(BUZZER_PIN, LOW);
  if (!isSuccess) {
    digitalWrite(LED_PIN, LOW);
  }
}

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

// Fungsi untuk mendapatkan waktu format HH:MM
String getCurrentTime() {
  struct tm timeinfo;
  char timeString[6];
  time_t now;
  time(&now);
  localtime_r(&now, &timeinfo);
  strftime(timeString, sizeof(timeString), "%H:%M", &timeinfo);
  return String(timeString);
}

// Fungsi untuk mendapatkan selisih waktu dalam menit
int getTimeDifferenceInMinutes(int hour1, int minute1, int hour2, int minute2) {
  return abs((hour1 * 60 + minute1) - (hour2 * 60 + minute2));
}

// Fungsi untuk mengecek apakah UID terdaftar dan aktif
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

// Fungsi untuk mendapatkan nama karyawan
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
  
  // Load tolerances
  if (Firebase.RTDB.getInt(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/tolerances/beforeCheckIn"))) {
    workingHours.toleranceBeforeCheckIn = fbdo.intData();
  }
  if (Firebase.RTDB.getInt(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/tolerances/afterCheckIn"))) {
    workingHours.toleranceAfterCheckIn = fbdo.intData();
  }
  if (Firebase.RTDB.getInt(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/tolerances/beforeCheckOut"))) {
    workingHours.toleranceBeforeCheckOut = fbdo.intData();
  }
  if (Firebase.RTDB.getInt(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/tolerances/afterCheckOut"))) {
    workingHours.toleranceAfterCheckOut = fbdo.intData();
  }
  if (Firebase.RTDB.getInt(&fbdo, FIREBASE_PATH_WORKING_HOURS + String("/minWorkingTime"))) {
    workingHours.minWorkingTime = fbdo.intData();
  }
}

bool canCheckOut(const String &checkInTime, int currentHour, int currentMinute) {
  int checkInHour, checkInMinute;
  sscanf(checkInTime.c_str(), "%d:%d", &checkInHour, &checkInMinute);
  
  int timeDiff = getTimeDifferenceInMinutes(checkInHour, checkInMinute, currentHour, currentMinute);
  
  // Cek minimal waktu kerja
  if (timeDiff < workingHours.minWorkingTime) {
    return false;
  }
  
  // Cek apakah sudah masuk waktu check-out yang valid
  int currentMinutes = currentHour * 60 + currentMinute;
  int startCheckOutMinutes = workingHours.checkOutStart_hour * 60 + workingHours.checkOutStart_minute;
  int earlyCheckOutMinutes = startCheckOutMinutes - workingHours.toleranceBeforeCheckOut;
  
  return currentMinutes >= earlyCheckOutMinutes;
}

String determineCheckInType(int hour, int minute) {
  int currentMinutes = hour * 60 + minute;
  int startMinutes = workingHours.checkInStart_hour * 60 + workingHours.checkInStart_minute;
  int endMinutes = workingHours.checkInEnd_hour * 60 + workingHours.checkInEnd_minute;
  
  // Cek waktu check-in dalam rentang yang diizinkan
  if (currentMinutes >= startMinutes && currentMinutes <= endMinutes) {
    return "null";
  }
  
  // Cek keterlambatan dalam batas toleransi
  int lateLimit = endMinutes + workingHours.toleranceAfterCheckIn;
  if (currentMinutes > endMinutes && currentMinutes <= lateLimit) {
    return "late";
  }
  
  return "invalid_time";
}

String determineCheckOutType(int hour, int minute) {
  int currentMinutes = hour * 60 + minute;
  int startMinutes = workingHours.checkOutStart_hour * 60 + workingHours.checkOutStart_minute;
  int endMinutes = workingHours.checkOutEnd_hour * 60 + workingHours.checkOutEnd_minute;
  
  // Cek waktu check-out dalam rentang yang diizinkan
  if (currentMinutes >= startMinutes && currentMinutes <= endMinutes) {
    return "null";
  }
  
  // Cek pulang awal dalam batas toleransi
  int earlyLimit = startMinutes - workingHours.toleranceBeforeCheckOut;
  if (currentMinutes >= earlyLimit && currentMinutes < startMinutes) {
    return "early";
  }
  
  // Cek keterlambatan dalam batas toleransi
  int lateLimit = endMinutes + workingHours.toleranceAfterCheckOut;
  if (currentMinutes > endMinutes && currentMinutes <= lateLimit) {
    return "over_time";
  }
  
  return "invalid_time";
}

void setup() {
  Serial.begin(115200);
  SerialMaster.begin(115200, SERIAL_8N1, SERIAL_TX_PIN, SERIAL_RX_PIN);
  
  // Initialize random seed using analog noise
  randomSeed(analogRead(0));
  
  // Initialize I2C for LCD
  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  
  // Initialize pins
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);
  pinMode(RESET_BTN_PIN, INPUT_PULLUP);
  
  digitalWrite(BUZZER_PIN, LOW);
  digitalWrite(LED_PIN, LOW);
  
  displayLCD("Initializing", "Please wait...");
  
  SPI.begin();
  mfrc522.PCD_Init();

  // Connect to WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  displayLCD("Connecting to", "WiFi...");
  
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    delay(300);
  }
  
  displayLCD("WiFi Connected", WiFi.localIP().toString());
  delay(2000);

  // Initialize Firebase and time
  configTime(8 * 3600, 0, "pool.ntp.org");
  
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);
  config.token_status_callback = tokenStatusCallback;

  loadWorkingHours();
  
  displayLCD("Ready for", "Attendance");
  Serial.print("setup or reset complated !");
}

void loop() {
    handleReset();
    
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

            if (Firebase.RTDB.getString(&fbdo_check, attendancePath + "/checkIn/time")) {
                // Check-out logic
                if (!Firebase.RTDB.getString(&fbdo_check, attendancePath + "/checkOut/time")) {
                    if (canCheckOut(fbdo_check.stringData(), hour, minute)) {
                        String checkOutType = determineCheckOutType(hour, minute);
                        bool isValid = checkOutType == "null" || checkOutType == "early";

                        // Generate random filename for photo
                        RandomFileData fileData = generateRandomFilename();

                        // Update Firebase with full URL
                        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/time", currentTime);
                        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/photoUrl", fileData.fullUrl);
                        Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkOut/isValid", isValid);
                        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/type", checkOutType);
                        Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/name", name);

                        // Send random string through SerialMaster
                        String jsonString = "{\"fileName\": \"" + fileData.randomPart + "\"}";
                        SerialMaster.println(jsonString);
                        Serial.println("Sent to SerialMaster: " + jsonString); // Debug output

                        Serial.println("Check-out: " + name + " (Type: " + checkOutType + ", Valid: " + String(isValid) + ")");
                        displayLCD("Check-out OK", name);
                        alert(true);
                    } else {
                        Serial.println("Belum bisa check-out: Minimal waktu kerja belum tercapai");
                        displayLCD("Min work time", "not reached!");
                        alert(false);
                    }
                } else {
                    Serial.println("Sudah melakukan check-out hari ini");
                    displayLCD("Already", "checked out!");
                    alert(false);
                }
            } else {
                // **NEW LOGIC ADDED: Handle late check-in and immediate check-out**
                int currentMinutes = hour * 60 + minute;
                int checkOutStartMinutes = workingHours.checkOutStart_hour * 60 + workingHours.checkOutStart_minute;

                if (currentMinutes >= checkOutStartMinutes) {
                    // Menentukan tipe check-in berdasarkan waktu saat ini
                    String checkInType = determineCheckInType(hour, minute);
                    // Menentukan apakah check-in valid berdasarkan tipe
                    bool isValidcekIn = (checkInType == "null" || checkInType == "late");

                    // Generate random filename for photo
                    RandomFileData fileData = generateRandomFilename();

                    // Update Firebase for check-in
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/time", currentTime);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/photoUrl", fileData.fullUrl);
                    Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkIn/isValid", isValidcekIn);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/type", checkInType);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/name", name);

                    // Update Firebase for check-out
                    String checkOutType = determineCheckOutType(hour, minute);
                    bool isValidcekOut = (checkOutType == "null" || checkOutType == "early" || checkOutType == "over_time");
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/time", currentTime);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/photoUrl", fileData.fullUrl);
                    Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkOut/isValid", isValidcekOut);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/type", checkOutType);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkOut/name", name);

                    // Send random string through SerialMaster
                    String jsonString = "{\"fileName\": \"" + fileData.randomPart + "\"}";
                    SerialMaster.println(jsonString);
                    Serial.println("Sent to SerialMaster: " + jsonString); // Debug output

                    Serial.println("Late check-in and immediate check-out: " + name);
                    displayLCD("Late Check-in", "Check-out Done");
                    alert(true);
                } else {
                    // Regular check-in logic
                    String checkInType = determineCheckInType(hour, minute);
                    bool isValid = checkInType == "null" || checkInType == "late";

                    // Generate random filename for photo
                    RandomFileData fileData = generateRandomFilename();

                    // Update Firebase with full URL
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/time", currentTime);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/photoUrl", fileData.fullUrl);
                    Firebase.RTDB.setBool(&fbdo, attendancePath + "/checkIn/isValid", isValid);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/type", checkInType);
                    Firebase.RTDB.setString(&fbdo, attendancePath + "/checkIn/name", name);

                    // Send random string through SerialMaster
                    String jsonString = "{\"fileName\": \"" + fileData.randomPart + "\"}";
                    SerialMaster.println(jsonString);
                    Serial.println("Sent to SerialMaster: " + jsonString); // Debug output

                    Serial.println("Check-in: " + name + " (Type: " + checkInType + ", Valid: " + String(isValid) + ")");
                    
                    if (isValid) {
                        displayLCD("Check-in OK", name);
                        alert(true);
                    } else {
                        displayLCD("Invalid time", name);
                        alert(false);
                    }
                }
            }
        } else {
            Firebase.RTDB.setString(&fbdo, String(FIREBASE_PATH_UNREGISTERED) + "/" + uid, uid);
            Serial.println("UID tidak terdaftar: " + uid);
            displayLCD("Unknown Card", "Card unregistered");
            alert(false);
        }
    }

    mfrc522.PICC_HaltA();
    mfrc522.PCD_StopCrypto1();
    
    delay(2000);
    displayLCD("Ready for", "Attendance");
}