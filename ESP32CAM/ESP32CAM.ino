#include "Arduino.h"
#include <ArduinoJson.h>
#include <random>
#include "WiFi.h"
#include "esp_camera.h"
#include "soc/soc.h"           // Disable brownout problems
#include "soc/rtc_cntl_reg.h"  // Disable brownout problems
#include "driver/rtc_io.h"
#include <LittleFS.h>
#include <FS.h>
#include <Firebase_ESP_Client.h>
#include <HardwareSerial.h>
#include <addons/TokenHelper.h>

#include "firebase_config.h"

// Define flash LED pin - typically GPIO 4 on ESP32-CAM
#define FLASH_LED_PIN 4

// WiFi reconnection parameters
#define WIFI_RECONNECT_INTERVAL 10000  // Attempt reconnection every 10 seconds
unsigned long previousWifiCheck = 0;
bool wifiConnected = false;

bool photoReady = true;

HardwareSerial serial2(1); // RX pin: SERIAL2_RX_PIN, TX pin: SERIAL2_TX_PIN

bool taskCompleted = false;

// Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

// Function prototypes
void fcsUploadCallback(FCS_UploadStatusInfo info);
bool checkWiFiConnection();

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");
}

bool checkWiFiConnection() {
  unsigned long currentMillis = millis();
  
  // Check WiFi connection status at regular intervals
  if (currentMillis - previousWifiCheck >= WIFI_RECONNECT_INTERVAL || !wifiConnected) {
    previousWifiCheck = currentMillis;
    
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("WiFi connection lost. Reconnecting...");
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
      
      // Wait up to 10 seconds for reconnection
      unsigned long reconnectStartTime = millis();
      while (WiFi.status() != WL_CONNECTED && millis() - reconnectStartTime < 10000) {
        delay(500);
        Serial.print(".");
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi reconnected!");
        Serial.print("IP Address: ");
        Serial.println(WiFi.localIP());
        wifiConnected = true;
        return true;
      } else {
        Serial.println("\nFailed to reconnect to WiFi");
        wifiConnected = false;
        return false;
      }
    } else {
      wifiConnected = true;
      return true;
    }
  }
  
  return wifiConnected;
}

void initLittleFS() {
  if (!LittleFS.begin(true)) {
    Serial.println("An Error has occurred while mounting LittleFS");
    ESP.restart();
  } else {
    Serial.println("LittleFS mounted successfully");
  }
}

void initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;

  if (psramFound()) {
    // Menggunakan resolusi yang lebih optimal untuk wajah
    config.frame_size = FRAMESIZE_UXGA;  // 1024x768 - lebih baik untuk deteksi wajah
    config.jpeg_quality = 0;  // Nilai lebih rendah = kualitas lebih tinggi (range: 0-63)
    config.fb_count = 1;       // Mengurangi buffer count untuk membebaskan memori
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 10;  // Meningkatkan kualitas JPEG
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    ESP.restart();
  }
  
  // Optimize camera sensor settings for better focus
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    // Tingkatkan ketajaman dan pengaturan kamera
    s->set_brightness(s, 1);     // Sedikit lebih terang (-2 sampai 2)
    s->set_contrast(s, 1);       // Tingkatkan kontras sedikit (-2 sampai 2)
    s->set_saturation(s, 0);     // Saturation default (0)
    s->set_sharpness(s, 2);      // Tingkatkan sharpness maksimal (0-3)
    s->set_denoise(s, 1);        // Level denoise
    s->set_quality(s, 10);       // Kualitas JPEG (0-63, nilai lebih rendah = kualitas lebih tinggi)
    s->set_colorbar(s, 0);       // Matikan color bar test
    s->set_whitebal(s, 1);       // Aktifkan white balance
    s->set_gain_ctrl(s, 1);      // Aktifkan auto gain
    s->set_exposure_ctrl(s, 1);  // Aktifkan auto exposure
    s->set_hmirror(s, 0);        // 0 = disable mirroring
    s->set_vflip(s, 0);          // 0 = disable flip vertikal
    s->set_awb_gain(s, 1);       // Aktifkan Auto White Balance gain
    s->set_wb_mode(s, 1);        // White Balance: Auto
    
    // Atur fokus dengan meningkatkan gain untuk kondisi cahaya berbeda
    s->set_gainceiling(s, (gainceiling_t)GAINCEILING_8X); // Meningkatkan gain ceiling
    
    Serial.println("Camera sensor settings optimized for better focus");
  }
}

void capturePhotoSaveLittleFS(void) {
  // Initialize flash LED as output
  pinMode(FLASH_LED_PIN, OUTPUT);
  
  // Pre-warm the sensor
  sensor_t * s = esp_camera_sensor_get();
  if (s) {
    // Persiapan sensor sebelum mengambil foto
    s->set_gain_ctrl(s, 1);      // Pastikan auto gain aktif
    s->set_exposure_ctrl(s, 1);  // Pastikan auto exposure aktif
  }
  
  // Turn on flash LED
  digitalWrite(FLASH_LED_PIN, HIGH);
  Serial.println("Flash LED turned ON");
  
  // Berikan waktu tambahan untuk kamera melakukan stabilisasi
  delay(500);  // Meningkatkan dari 300ms menjadi 500ms
  
  // Dispose first pictures because of bad quality
  camera_fb_t* fb = NULL;
  // Meningkatkan jumlah frame yang dibuang untuk stabilisasi sensor
  for (int i = 0; i < 5; i++) {  // Meningkatkan dari 4 menjadi 5 frame
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
    delay(100);  // Tambahkan delay yang lebih besar antara frame untuk stabilisasi
  }
  
  // Temporarily adjust settings for the final capture
  if (s) {
    // Optimal settings for face focus
    s->set_aec2(s, 1);             // Enable auto exposure DSP
    delay(200);                    // Allow settings to take effect
  }
  
  // Take a new photo
  fb = NULL;  
  fb = esp_camera_fb_get();  
  
  // Turn off flash LED after capture
  digitalWrite(FLASH_LED_PIN, LOW);
  Serial.println("Flash LED turned OFF");
  
  if(!fb) {
    Serial.println("Camera capture failed");
    delay(1000);
    ESP.restart();
  }
  
  // Photo file name
  Serial.printf("Picture file name: %s\n", FILE_PHOTO_PATH);
  File file = LittleFS.open(FILE_PHOTO_PATH, FILE_WRITE);
  // Insert the data in the photo file
  if (!file) {
    Serial.println("Failed to open file in writing mode");
  }
  else {
    file.write(fb->buf, fb->len); // payload (image), payload length
    Serial.print("The picture has been saved in ");
    Serial.print(FILE_PHOTO_PATH);
    Serial.print(" - Size: ");
    Serial.print(fb->len);
    Serial.println(" bytes");
  }
  // Close the file
  file.close();
  esp_camera_fb_return(fb);
  
  // Reset any temporary settings
  if (s) {
    s->set_exposure_ctrl(s, 1);  // Restore auto exposure for next time
  }
}

void fcsUploadCallback(FCS_UploadStatusInfo info) {
  if (info.status == firebase_fcs_upload_status_complete) {
    Serial.println("Upload completed");
    Serial.printf("Download URL: %s\n", fbdo.downloadURL().c_str());
    serial2.println(fbdo.downloadURL().c_str());
  } else if (info.status == firebase_fcs_upload_status_error) {
    Serial.printf("Upload failed, %s\n", info.errorMsg.c_str());
  }
}

void setup() {
  Serial.begin(115200);
  serial2.begin(SERIAL2_BAUD_RATE, SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
  
  // Initialize flash LED pin as output
  pinMode(FLASH_LED_PIN, OUTPUT);
  digitalWrite(FLASH_LED_PIN, LOW); // Ensure flash is off at startup
  
  // Disable brownout detector
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  
  initWiFi();
  initLittleFS();
  initCamera();
  
  // Firebase configuration
  configF.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  configF.token_status_callback = tokenStatusCallback;
  
  // Only initialize Firebase if WiFi is connected
  if (wifiConnected) {
    Firebase.begin(&configF, &auth);
    Firebase.reconnectWiFi(true);
  }
}

void loop() {
  // Check and try to reconnect WiFi if necessary
  if (!checkWiFiConnection()) {
    // If WiFi connection failed, skip the rest of the loop
    delay(1000);
    return;
  }
  
  // If Firebase is not initialized yet and WiFi is now connected, initialize it
  if (wifiConnected && !Firebase.ready() && !Firebase.isTokenExpired()) {
    Firebase.begin(&configF, &auth);
    Firebase.reconnectWiFi(true);
  }
  
  String receivedData;
  String fileName;
  String url;
  taskCompleted = false;
  photoReady = true;
  
  // Read from Serial2 if data is available
  if (serial2.available()) {
    receivedData = serial2.readStringUntil('\n');
    Serial.print("Received data: ");
    Serial.println(receivedData);
  
    // Parse JSON
    StaticJsonDocument<200> jsonDoc; // Ukuran buffer dapat disesuaikan
    DeserializationError error = deserializeJson(jsonDoc, receivedData);

    if (error) {
      Serial.print("JSON Parsing failed: ");
      Serial.println(error.c_str());
      return; // Keluar dari loop jika parsing gagal
    }

    // Ekstrak nilai tertentu dari JSON, misalnya "fileName"
    if (jsonDoc.containsKey("fileName")) {
      fileName = jsonDoc["fileName"].as<String>();
      Serial.print("Parsed fileName: ");
      Serial.println(fileName);
    } else {
      Serial.println("JSON does not contain 'fileName' key");
      return; // Keluar jika key "fileName" tidak ditemukan
    }
    
    String filePath = "/" + fileName + ".jpg";
    // Capture dan simpan foto jika photoReady
    if (photoReady) {
      capturePhotoSaveLittleFS();
      photoReady = false;
    }

    delay(1000); // Tunggu sebentar sebelum upload ke Firebase

    // Upload ke Firebase jika siap dan WiFi terhubung
    if (wifiConnected && Firebase.ready() && !taskCompleted) {
      taskCompleted = true;
      if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, FILE_PHOTO_PATH, mem_storage_type_flash, filePath, "image/jpeg", fcsUploadCallback)) {
        Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
        url = fbdo.downloadURL().c_str();
      } else {
        Serial.printf("Upload failed: %s\n", fbdo.errorReason().c_str());
      }
    } else if (!wifiConnected) {
      Serial.println("Cannot upload to Firebase: WiFi disconnected");
    }
  }
  
  delay(1000);  // Small delay before next loop iteration
}