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

bool photoReady = true;

HardwareSerial serial2(1); // RX pin: SERIAL2_RX_PIN, TX pin: SERIAL2_TX_PIN

bool taskCompleted = false;

// Define Firebase Data objects
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig configF;

// Function prototypes
void fcsUploadCallback(FCS_UploadStatusInfo info);

void initWiFi() {
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println("Connecting to WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.println("Connecting to WiFi...");
  }
  Serial.println("WiFi connected");
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
    config.frame_size = FRAMESIZE_UXGA;
    config.jpeg_quality = 10;
    config.fb_count = 1;
  } else {
    config.frame_size = FRAMESIZE_SVGA;
    config.jpeg_quality = 12;
    config.fb_count = 1;
  }

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Camera init failed with error 0x%x\n", err);
    ESP.restart();
  }
}

void capturePhotoSaveLittleFS( void ) {
  // Dispose first pictures because of bad quality
  camera_fb_t* fb = NULL;
  // Skip first 3 frames (increase/decrease number as needed).
  for (int i = 0; i < 4; i++) {
    fb = esp_camera_fb_get();
    esp_camera_fb_return(fb);
    fb = NULL;
  }
  // Take a new photo
  fb = NULL;  
  fb = esp_camera_fb_get();  
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
  serial2.begin(SERIAL2_BAUD_RATE,SERIAL_8N1, SERIAL2_RX_PIN, SERIAL2_TX_PIN);
  initWiFi();
  initLittleFS();
  initCamera();
  
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  // Firebase configuration
  configF.api_key = API_KEY;
  auth.user.email = USER_EMAIL;
  auth.user.password = USER_PASSWORD;
  configF.token_status_callback = tokenStatusCallback;
  Firebase.begin(&configF, &auth);
  Firebase.reconnectWiFi(true);
}

void loop() {
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
  if (photoReady ) {
    capturePhotoSaveLittleFS();
    photoReady = false;
  }

  delay(1000); // Tunggu sebentar sebelum upload ke Firebase

  // Upload ke Firebase jika siap
  if (Firebase.ready() && !taskCompleted) {
    taskCompleted = true;
    if (Firebase.Storage.upload(&fbdo, STORAGE_BUCKET_ID, FILE_PHOTO_PATH, mem_storage_type_flash, filePath, "image/jpeg", fcsUploadCallback)) {
      Serial.printf("\nDownload URL: %s\n", fbdo.downloadURL().c_str());
      url = fbdo.downloadURL().c_str();
    } else {
      Serial.printf("Upload failed: %s\n", fbdo.errorReason().c_str());
    }
  }
}
  delay(1000);  // Small delay before next loop iteration
}
