// #include "firebase_config.h"
#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <time.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include <FirebaseClient.h>
#include <FirebaseJson.h>
#include <vector>
#include <utility>

#include <config.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

DefaultNetwork network;

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD);

FirebaseApp app;

#include <WiFiClientSecure.h>

WiFiClientSecure ssl_client;

using AsyncClient = AsyncClientClass;

AsyncClient aClient(ssl_client, getNetwork(network));

RealtimeDatabase Database;

AsyncResult aResult_no_callback;

MFRC522 mfrc522(RFID_SS_PIN, RFID_RST_PIN);

// Structure to hold working hours
struct WorkingHours
{
  uint8_t startHour = 8;
  uint8_t startMinute = 0;
  uint8_t endHour = 9;
  uint8_t endMinute = 0;
  uint8_t checkOutStartHour = 13;
  uint8_t checkOutStartMinute = 0;
  uint8_t checkOutEndHour = 15;
  uint8_t checkOutEndMinute = 0;
};

// Define the AutoCheckTime structure
struct AutoCheckTime
{
  uint8_t hour = 12;
  uint8_t minute = 0;
};

// Optimized global variables
unsigned long lastAutoCheckTime = 0;
unsigned long lastAutoCheckDisplay = 0;
const unsigned long DS_CHECK_INTERVAL = 2000;
const unsigned long AUTO_CHECK_INTERVAL = 60000;

WorkingHours workingHours;
AutoCheckTime autoCheckTime;

// Preallocate buffers
char dateStr[11];
char timeStr[6];
char tempBuffer[64];
char imgNameBuffer[32];

HardwareSerial SerialMaster(2);

// Function declarations
bool fetchAutoCheckTime();
bool fetchWorkingHours();
void alert(bool isSuccess);
void warning(bool isSuccess);
void displayLCD(const char *line1, const char *line2);
void getCurrentDateTime();
const char *getAttendanceStatus(const char *timeStr);
const char *getCheckOutStatus(const char *timeStr);
bool isValidUID(const char *uid);
const char *getEmployeeName(const char *uid);
void generateRandomFilename(char *buffer, size_t size);
bool handleCheckIn(const char *uid);
bool handleCheckOut(const char *uid);
void printResult(AsyncResult &aResult);
void reconnectWiFi();
void authHandler();

// Updated function to fetch working hours using synchronous Firebase Client API
bool fetchWorkingHours()
{
  bool success = true;
  String checkInStartPath = "workingHours/checkIn/start";
  String checkInEndPath = "workingHours/checkIn/end";
  String checkOutStartPath = "workingHours/checkOut/start";
  String checkOutEndPath = "workingHours/checkOut/end";

  String startTime = Database.get<String>(aClient, checkInStartPath.c_str());
  if (aClient.lastError().code() == 0)
  {
    sscanf(startTime.c_str(), "%hhu:%hhu",
           &workingHours.startHour, &workingHours.startMinute);
  }
  else
  {
    success = false;
  }

  String endTime = Database.get<String>(aClient, checkInEndPath.c_str());
  if (aClient.lastError().code() == 0)
  {
    sscanf(endTime.c_str(), "%hhu:%hhu",
           &workingHours.endHour, &workingHours.endMinute);
  }
  else
  {
    success = false;
  }

  String checkOutStartTime = Database.get<String>(aClient, checkOutStartPath.c_str());
  if (aClient.lastError().code() == 0)
  {
    sscanf(checkOutStartTime.c_str(), "%hhu:%hhu",
           &workingHours.checkOutStartHour, &workingHours.checkOutStartMinute);
  }
  else
  {
    success = false;
  }

  String checkOutEndTime = Database.get<String>(aClient, checkOutEndPath.c_str());
  if (aClient.lastError().code() == 0)
  {
    sscanf(checkOutEndTime.c_str(), "%hhu:%hhu",
           &workingHours.checkOutEndHour, &workingHours.checkOutEndMinute);
  }
  else
  {
    success = false;
  }

  return success;
}

bool fetchAutoCheckTime()
{
  String autoCheckPath = "workingHours/autocek";
  String autoTimeStr = Database.get<String>(aClient, autoCheckPath.c_str());

  if (aClient.lastError().code() == 0)
  {
    sscanf(autoTimeStr.c_str(), "%hhu:%hhu",
           &autoCheckTime.hour, &autoCheckTime.minute);
    return true;
  }
}

void generateRandomFilename(char *buffer, size_t size)
{
  static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  const size_t charsetSize = sizeof(charset) - 1;

  size_t pos = 0;
  for (int i = 0; i < 8 && pos < size - 1; i++)
  {
    buffer[pos++] = charset[random(0, charsetSize)];
  }

  // Get current time
  struct timeval tv;
  gettimeofday(&tv, NULL);

  // If time hasn't been synchronized yet (epoch time is very small),
  // use millis() as a fallback
  unsigned long epochTime;
  if (tv.tv_sec < 1600000000)
  {                       // if time is before Sept 2020, it's probably not synchronized
    epochTime = millis(); // Use millis as fallback
  }
  else
  {
    epochTime = tv.tv_sec + 8 * 3600; // GMT+8
  }

  snprintf(buffer + pos, size - pos, "_%lu", epochTime);
}

inline uint16_t timeToMinutes(const char *timeStr)
{
  uint8_t hour = 0, minute = 0;
  sscanf(timeStr, "%hhu:%hhu", &hour, &minute);
  return hour * 60 + minute;
}

const char *getAttendanceStatus(const char *timeStr)
{
  uint16_t currentMinutes = timeToMinutes(timeStr);
  uint16_t startMinutes = workingHours.startHour * 60 + workingHours.startMinute;
  uint16_t endMinutes = workingHours.endHour * 60 + workingHours.endMinute;
  if (currentMinutes < startMinutes)
    return "Lebih Awal";
  if (currentMinutes <= endMinutes)
    return "Tepat Waktu";
  return "Terlambat";
}

const char *getCheckOutStatus(const char *timeStr)
{
  uint16_t currentMinutes = timeToMinutes(timeStr);
  uint16_t startMinutes = workingHours.checkOutStartHour * 60 + workingHours.checkOutStartMinute;
  uint16_t endMinutes = workingHours.checkOutEndHour * 60 + workingHours.checkOutEndMinute;

  if (currentMinutes < startMinutes)
    return "Terlalu Awal";
  if (currentMinutes <= endMinutes)
    return "Tepat Waktu";
  return "Lembur";
}

void displayLCD(const char *line1, const char *line2)
{
  static char currentLine1[17];
  static char currentLine2[17];

  if (strcmp(line1, currentLine1) != 0 || strcmp(line2, currentLine2) != 0)
  {
    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print(line1);
    lcd.setCursor(0, 1);
    lcd.print(line2);

    strncpy(currentLine1, line1, 16);
    strncpy(currentLine2, line2, 16);
    currentLine1[16] = currentLine2[16] = '\0';
  }
}

void alert(bool isSuccess)
{
  if (isSuccess)
  {
    digitalWrite(BUZZER_PIN, HIGH);
    delay(1000);
    digitalWrite(BUZZER_PIN, LOW);
  }
  else
  {
    for (int i = 0; i < 3; i++)
    {
      digitalWrite(BUZZER_PIN, HIGH);
      digitalWrite(LED_PIN, HIGH);
      delay(200);
      digitalWrite(BUZZER_PIN, LOW);
      digitalWrite(LED_PIN, LOW);
      delay(200);
    }
  }
}

void warning(bool isSuccess)
{
  if (isSuccess)
  {
    digitalWrite(LED_PIN, HIGH);
    for (int i = 0; i < 6; i++)
    {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(80);
      digitalWrite(BUZZER_PIN, LOW);
      delay(80);
    }
    digitalWrite(LED_PIN, LOW);
  }
  else
  {
    digitalWrite(LED_PIN, HIGH);
    for (int i = 0; i < 3; i++)
    {
      digitalWrite(BUZZER_PIN, HIGH);
      delay(1000);
      digitalWrite(BUZZER_PIN, LOW);
      delay(1000);
    }
    digitalWrite(LED_PIN, LOW);
  }
}

void getCurrentDateTime()
{
  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);

  snprintf(dateStr, sizeof(dateStr), "%04d-%02d-%02d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  snprintf(timeStr, sizeof(timeStr), "%02d:%02d",
           timeinfo.tm_hour, timeinfo.tm_min);
}

// Updated function to check UID validity using synchronous Firebase Client API
bool isValidUID(const char *uid)
{
  String path = "/employees/";
  path += uid;
  path += "/status";

  Serial.print("Code UID: ");
  Serial.println(uid);

  int connection = Database.get<int>(aClient, "/connection/code");
  Serial.print("Code Connection : ");
  Serial.println(connection);

  String status;
  unsigned long startTime = millis(); // Catat waktu mulai

  // Loop untuk retry jika status NULL atau kosong, tetapi tidak lebih dari 10 detik
  do
  {
    status = Database.get<String>(aClient, path.c_str());

    // Cetak hasil ke Serial Monitor
    Serial.print("Status UID: ");
    Serial.println(status);

    // Jika terjadi error, coba ulangi hingga timeout tercapai
    if (aClient.lastError().code() != 0 || status == "NULL" || status.length() == 0)
    {
      Serial.println("Status NULL atau kosong, mencoba kembali...");
      delay(500); // Beri jeda 500ms sebelum mencoba lagi
    }
    else
    {
      break; // Keluar dari loop jika status valid
    }

  } while (millis() - startTime < 60000); // Berhenti setelah 10 detik

  // Hapus tanda kutip jika ada
  if (status.startsWith("\"") && status.endsWith("\""))
  {
    status = status.substring(1, status.length() - 1);
  }

  // Return true jika status adalah "Active", jika tidak return false
  return status == "Active";
}

// Updated function to get employee name using synchronous Firebase Client API
const char *getEmployeeName(const char *uid)
{
  static char nameBuffer[32] = "Unknown";

  String path = "/employees/";
  path += uid;
  path += "/name";

  String name = Database.get<String>(aClient, path.c_str());
  Serial.print("Name UID: ");
  Serial.println(name);

  if (aClient.lastError().code() == 0)
  {
    // Remove quotes if present
    if (name.startsWith("\"") && name.endsWith("\""))
    {
      name = name.substring(1, name.length() - 1);
    }
    strncpy(nameBuffer, name.c_str(), 31);
    nameBuffer[31] = '\0';
    return nameBuffer;
  }

  return "Unknown";
}

// Updated function to handle check-in using synchronous Firebase Client API
bool handleCheckIn(const char *uid)
{
  const char *name = getEmployeeName(uid);
  const char *attendanceStatus = getAttendanceStatus(timeStr);

  snprintf(tempBuffer, sizeof(tempBuffer), "%s/%s/%s/time",
           FIREBASE_PATH_ATTENDANCE, dateStr, uid);

  // Check if already checked in
  int connection = Database.get<int>(aClient, "/connection/code");
  Serial.print("Code Connection : ");
  Serial.println(connection);
  String checkInTime = Database.get<String>(aClient, tempBuffer);
  // Cetak hasil ke Serial Monitor
  Serial.print("checkInTime(IN): ");
  Serial.println(checkInTime);
  if (aClient.lastError().code() == 0 && checkInTime != "null")
  {
    displayLCD("Already", "checked in!");
    alert(false);
    return false;
  }

  // Generate image URL for check-in
  generateRandomFilename(imgNameBuffer, sizeof(imgNameBuffer));
  snprintf(tempBuffer, sizeof(tempBuffer), "{\"fileName\": \"%s\"}", imgNameBuffer);
  SerialMaster.println(tempBuffer);

  char imageUrl[64];
  snprintf(imageUrl, sizeof(imageUrl), "%s.jpg", imgNameBuffer);

  displayLCD("On process", "Upload data...");

  // Construct base path components
  String basePath = String(FIREBASE_PATH_ATTENDANCE) + "/" + dateStr + "/" + uid;

  // Store attendance record using synchronous set operations
  bool success = true;

  if (!Database.set<String>(aClient, (basePath + "/time").c_str(), String(timeStr)))
  {
    success = false;
  }

  if (!Database.set<String>(aClient, (basePath + "/name").c_str(), String(name)))
  {
    success = false;
  }

  if (!Database.set<String>(aClient, (basePath + "/status").c_str(), String(attendanceStatus)))
  {
    success = false;
  }

  if (!Database.set<String>(aClient, (basePath + "/imageUrl").c_str(), String(imageUrl)))
  {
    success = false;
  }

  Serial.println("Successfuly send datas to Firebase");
  Serial.println("----------------------------------");

  if (success)
  {
    displayLCD(name, attendanceStatus);
    alert(true);
    return true;
  }
  else
  {
    Serial.println("Failed send datas to Firebase !");
    Serial.println("----------------------------------");
    displayLCD("Error", "Data not saved");
    alert(false);
    return false;
  }
}

// Updated function to handle check-out using synchronous Firebase Client API
bool handleCheckOut(const char *uid)
{
  const char *name = getEmployeeName(uid);
  const char *checkOutStatus = getCheckOutStatus(timeStr);

  String basePath = String(FIREBASE_PATH_ATTENDANCE) + "/" + dateStr + "/" + uid;

  // Check if check-in exists
  int connection = Database.get<int>(aClient, "/connection/code");
  Serial.print("Code Connection : ");
  Serial.println(connection);
  String checkInTime = Database.get<String>(aClient, (basePath + "/time").c_str());
  // Cetak hasil ke Serial Monitor
  Serial.print("checkInTime(OUT): ");
  Serial.println(checkInTime);
  if (aClient.lastError().code() == 0 && checkInTime == "null")
  {
    displayLCD("No check-in", "CekIn Processing");
    handleCheckIn(uid); // Automatically Check-In before Check-Out
    alert(false);
    return false;
  }

  // Check if already checked out
  Serial.print("Code Connection : ");
  Serial.println(connection);
  String checkOutTime = Database.get<String>(aClient, (basePath + "/time_checkout").c_str());
  // Cetak hasil ke Serial Monitor
  Serial.print("checkOutTime(OUT): ");
  Serial.println(checkOutTime);
  Serial.println("----------------------------------");
  if (aClient.lastError().code() == 0 && checkOutTime != "null")
  {
    displayLCD("Already", "checked out!");
    alert(false);
    return false;
  }

  // Generate image URL for check-out
  generateRandomFilename(imgNameBuffer, sizeof(imgNameBuffer));
  snprintf(tempBuffer, sizeof(tempBuffer), "{\"fileName\": \"%s\"}", imgNameBuffer);
  SerialMaster.println(tempBuffer);

  char imageUrl[64];
  snprintf(imageUrl, sizeof(imageUrl), "%s.jpg", imgNameBuffer);

  displayLCD("On process", "Upload data...");

  // Store check-out record using synchronous operations
  bool success = true;

  if (!Database.set<String>(aClient, (basePath + "/time_checkout").c_str(), String(timeStr)))
  {
    success = false;
  }

  if (!Database.set<String>(aClient, (basePath + "/status_checkout").c_str(), String(checkOutStatus)))
  {
    success = false;
  }

  if (!Database.set<String>(aClient, (basePath + "/image_checkout").c_str(), String(imageUrl)))
  {
    success = false;
  }
  Serial.println("Successfuly send datas to Firebase");
  Serial.println("----------------------------------");

  if (success)
  {
    displayLCD(name, checkOutStatus);
    alert(true);
    return true;
  }
  else
  {
    displayLCD("Error", "Data not saved");
    Serial.println("Failed send datas to Firebase !");
    Serial.println("----------------------------------");
    alert(false);
    return false;
  }
}

void handleAutoAttendance()
{
  getCurrentDateTime();

  time_t now;
  struct tm timeinfo;
  time(&now);
  localtime_r(&now, &timeinfo);
  int day = timeinfo.tm_wday;

  Serial.print("day status : ");
  Serial.println(day);

  if (timeinfo.tm_wday == 0)
  {
    displayLCD("Hari Minggu", "Auto cek OFF");
    return;
  }

  uint8_t currentHour, currentMinute;
  sscanf(timeStr, "%hhu:%hhu", &currentHour, &currentMinute);
  uint16_t currentTimeInMinutes = currentHour * 60 + currentMinute;
  uint16_t autoCheckTimeInMinutes = autoCheckTime.hour * 60 + autoCheckTime.minute;
  uint16_t endCheckTimeInMinutes = autoCheckTimeInMinutes + 3;

  if (currentTimeInMinutes >= autoCheckTimeInMinutes && currentTimeInMinutes < endCheckTimeInMinutes)
  {
    displayLCD("Please wait..", "autoMode On");
    Serial.println(F("Running automatic attendance check..."));

    int connection = Database.get<int>(aClient, "/connection/code");
    Serial.print("Code Connection : ");
    Serial.println(connection);

    String jsonStr = Database.get<String>(aClient, FIREBASE_PATH_EMPLOYEE);

    if (aClient.lastError().code() == 0)
    {
      FirebaseJson employeesJson(jsonStr);
      std::vector<std::pair<String, String>> employeesList;

      size_t count = employeesJson.iteratorBegin();

      for (size_t i = 0; i < count; i++)
      {
        int type = 0;
        String key, value;
        employeesJson.iteratorGet(i, type, key, value);

        if (type == FirebaseJson::JSON_OBJECT || value.startsWith("{"))
        {
          FirebaseJson employeeJson(value);
          FirebaseJsonData nameResult, statusResult;
          employeeJson.get(nameResult, "name");
          employeeJson.get(statusResult, "status");

          String name = nameResult.stringValue;
          String status = statusResult.stringValue;

          if (status == "Active")
          {
            employeesList.push_back(std::make_pair(key, name));
          }
        }
      }
      employeesJson.iteratorEnd();

      for (const auto &employee : employeesList)
      {
        String uid = employee.first;
        String name = employee.second;

        Serial.print("Active Employee: ");
        Serial.println(name);

        String attendancePath = String(FIREBASE_PATH_ATTENDANCE) + "/" + dateStr + "/" + uid + "/time";
        String checkInTime = Database.get<String>(aClient, attendancePath.c_str());
        Serial.print("Time :");
        Serial.println(checkInTime);

        if (checkInTime == "null")
        {
          String basePath = String(FIREBASE_PATH_ATTENDANCE) + "/" + dateStr + "/" + uid;

          Database.set<String>(aClient, (basePath + "/time").c_str(), "-");
          Database.set<String>(aClient, (basePath + "/name").c_str(), name);
          Database.set<String>(aClient, (basePath + "/status").c_str(), "Tidak Hadir");
          Database.set<String>(aClient, (basePath + "/imageUrl").c_str(), "auto_attendance.jpg");
          Database.set<String>(aClient, (basePath + "/time_checkout").c_str(), "-");
          Database.set<String>(aClient, (basePath + "/status_checkout").c_str(), "Tidak Hadir");
          Database.set<String>(aClient, (basePath + "/imageUrl_checkout").c_str(), "auto_attendance.jpg");

          Serial.print(F("Auto-marked attendance for: "));
          Serial.println(name);
        }
      }

      Serial.println("----------------------------------");
    }
    else
    {
      Serial.println(F("Failed to get employee data from Firebase"));
      Serial.print(F("Error message: "));
      Serial.println(aClient.lastError().message().c_str());
      Serial.print(F("Error code: "));
      Serial.println(aClient.lastError().code());

      displayLCD("Error", "Firebase Error");
      delay(2000);
    }

    displayLCD("Completed check", "autoMode off");
  }
  else
  {
    Serial.println(F("Not time for AutoMode"));
    Serial.println("----------------------------------");
  }
}

void authHandler()
{
  // Blocking authentication handler with timeout
  unsigned long ms = millis();
  while (app.isInitialized() && !app.ready() && millis() - ms < 120 * 1000)
  {
    JWT.loop(app.getAuth());
    if (aResult_no_callback.isEvent())
    {
      Serial.printf("Event task: %s, msg: %s, code: %d\n",
                    aResult_no_callback.uid().c_str(),
                    aResult_no_callback.appEvent().message().c_str(),
                    aResult_no_callback.appEvent().code());
    }

    if (aResult_no_callback.isDebug())
    {
      Serial.printf("Debug task: %s, msg: %s\n",
                    aResult_no_callback.uid().c_str(),
                    aResult_no_callback.debug().c_str());
    }

    if (aResult_no_callback.isError())
    {
      Serial.printf("Error task: %s, msg: %s, code: %d\n",
                    aResult_no_callback.uid().c_str(),
                    aResult_no_callback.error().message().c_str(),
                    aResult_no_callback.error().code());
    }
  }
}

void setup()
{
  Serial.begin(115200);
  SerialMaster.begin(115200, SERIAL_8N1, SERIAL_TX_PIN, SERIAL_RX_PIN);
  SPI.begin();
  mfrc522.PCD_Init();

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();

  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(LED_PIN, OUTPUT);

  displayLCD("Connecting to", "WiFi...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  // Wait for WiFi with timeout
  unsigned long startAttempt = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < 30000)
  {
    delay(300);
  }

  if (WiFi.status() != WL_CONNECTED)
  {
    displayLCD("WiFi Failed", "Check Settings");
    delay(2000);
    ESP.restart();
  }
  Serial.println("----------------------------------");
  Serial.println("Wifi Connected. ");
  Serial.println("----------------------------------");

  configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com", "time.nist.gov");

  // Wait for time sync with timeout
  time_t now = time(nullptr);
  while (now < 1677836800)
  {
    delay(100);
    now = time(nullptr);
  }
  Serial.println("Time sync success. ");
  Serial.println("----------------------------------");

  displayLCD("WiFi Connected", "Time Sync");

  // Firebase configuration
  ssl_client.setInsecure();
  initializeApp(aClient, app, getAuth(user_auth), aResult_no_callback);
  Serial.println("Connecting to firebase ....");
  authHandler();
  Serial.println("Firebase Connected. ");
  Serial.println("----------------------------------");
  app.getApp<RealtimeDatabase>(Database);
  Database.url(DATABASE_URL);

  // Initial working hours fetch
  fetchWorkingHours();
  fetchAutoCheckTime();

  Serial.println("Fetching working hour success. ");
  Serial.println("----------------------------------");
  displayLCD("Ready for", "Attendance");
}

void loop()
{
  unsigned long currentMillis = millis();

  authHandler();

  // Update display periodically
  if (currentMillis - lastAutoCheckDisplay >= DS_CHECK_INTERVAL)
  {
    lastAutoCheckDisplay = currentMillis;
    getCurrentDateTime();

    // Check current time to display appropriate message
    uint16_t currentMinutes = timeToMinutes(timeStr);
    uint16_t checkInStartMinutes = workingHours.startHour * 60 + workingHours.startMinute;
    uint16_t checkInEndMinutes = workingHours.endHour * 60 + workingHours.endMinute;
    uint16_t checkOutStartMinutes = workingHours.checkOutStartHour * 60 + workingHours.checkOutStartMinute;
    uint16_t checkOutEndMinutes = workingHours.checkOutEndHour * 60 + workingHours.checkOutEndMinute;

    if (currentMinutes >= checkInStartMinutes && currentMinutes <= checkInEndMinutes)
    {
      displayLCD("Ready for", "Check-in");
    }
    else if (currentMinutes >= checkOutStartMinutes && currentMinutes <= checkOutEndMinutes)
    {
      displayLCD("Ready for", "Check-out");
    }
    else
    {
      displayLCD("Attendance", "System Ready");
    }
  }

  // Check for auto attendance (run once per minute)
  if (currentMillis - lastAutoCheckTime >= AUTO_CHECK_INTERVAL)
  {
    lastAutoCheckTime = currentMillis;
    if (WiFi.status() == WL_CONNECTED && app.ready())
    {
      handleAutoAttendance();
    }
  }

  // Check for new card
  if (!mfrc522.PICC_IsNewCardPresent() || !mfrc522.PICC_ReadCardSerial())
  {
    delay(50);
    return;
  }

  // Build UID string
  char uid[9];
  uint8_t pos = 0;
  for (byte i = 0; i < mfrc522.uid.size && pos < 8; i++)
  {
    pos += snprintf(uid + pos, sizeof(uid) - pos, "%02X", mfrc522.uid.uidByte[i]);
  }
  uid[8] = '\0';

  displayLCD("Card Detected", "Please wait..");

  if (WiFi.status() == WL_CONNECTED && app.ready())
  {
    delay(200);
    displayLCD("Connecting and", "Checking...");
    getCurrentDateTime();

    // Direct check against Firebase
    if (isValidUID(uid))
    {
      // Determine whether to handle as check-in or check-out based on time
      uint16_t currentMinutes = timeToMinutes(timeStr);
      uint16_t startMinutes = workingHours.startHour * 60 + workingHours.startMinute - 60;
      uint16_t endMinutes = workingHours.endHour * 60 + workingHours.endMinute;
      uint16_t lockinMinutes = endMinutes + 3 * 60;

      if (currentMinutes >= startMinutes && currentMinutes <= lockinMinutes)
      {
        // Check-in time range
        handleCheckIn(uid);
      }
      else
      {
        // Default to check-out
        handleCheckOut(uid);
      }
    }
    else
    {
      String cek = Database.get<String>(aClient, "/employees/" + String(uid) + "/status");
      Serial.print("Nilai cek status : ");
      Serial.println(cek);
      if (cek == "null")
      {
        String unregisteredPath = String(FIREBASE_PATH_UNREGISTERED) + "/UID";
        Serial.println("Successfuly send datas to Firebase");
        Serial.println("----------------------------------");
        Database.set<String>(aClient, unregisteredPath.c_str(), String(uid));
        displayLCD("Unknown Card !", "Registered first");
        warning(true);
        delay(1000);
        return;
      }
      warning(false);
      displayLCD("Connection to", "Database Timeout");
      delay(1000);
      displayLCD("Please try Again", "or RestartDevice");
    }
  }
  else
  {
    displayLCD("Database", "Connection Error");
    Serial.println("Error connection to firebase");
    Serial.println("----------------------------------");
    warning(false);
    reconnectWiFi();
  }

  mfrc522.PICC_HaltA();
  mfrc522.PCD_StopCrypto1();

  delay(1000);
}

void reconnectWiFi()
{
  displayLCD("WiFi Lost", "Reconnecting...");
  WiFi.disconnect();
  WiFi.reconnect();

  unsigned long startAttemptTime = millis();
  bool connected = false;

  // Try to reconnect for 30 seconds
  while (!connected && millis() - startAttemptTime < 30000)
  {
    if (WiFi.status() == WL_CONNECTED)
    {
      connected = true;
      displayLCD("WiFi", "Connected!");
      delay(1000);
      displayLCD("Please try", "again attendance");
      break;
    }
    delay(500);
  }

  if (!connected)
  {
    displayLCD("WiFi Failed!", "Check settings");
    delay(1000);
    displayLCD("Restarting", "device Please !!");
    delay(2000);
  }
}

// Helper function to print async results
void printResult(AsyncResult &aResult)
{
  if (aResult.isEvent())
  {
    Serial.printf("Event task: %s, msg: %s, code: %d\n",
                  aResult.uid().c_str(),
                  aResult.appEvent().message().c_str(),
                  aResult.appEvent().code());
  }

  if (aResult.isDebug())
  {
    Serial.printf("Debug task: %s, msg: %s\n",
                  aResult.uid().c_str(),
                  aResult.debug().c_str());
  }

  if (aResult.isError())
  {
    Serial.printf("Error task: %s, msg: %s, code: %d\n",
                  aResult.uid().c_str(),
                  aResult.error().message().c_str(),
                  aResult.error().code());
  }

  if (aResult.available())
  {
    Serial.printf("task: %s, payload: %s\n", aResult.uid().c_str(), aResult.c_str());
  }
}