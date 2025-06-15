## IoT-Absensi-RFIDdanESP32CAM

## 📚 Library yang Digunakan

Proyek ini menggunakan beberapa library Arduino untuk mendukung koneksi jaringan, pengolahan data, tampilan LCD, serta pembacaan RFID. Pastikan library berikut telah diinstal dengan versi yang sesuai:

| Library               | Pengembang         | Versi   | Deskripsi Singkat                                               |
|-----------------------|--------------------|---------|------------------------------------------------------------------|
| **WiFi101**           | Arduino             | 0.16.1  | Koneksi Wi-Fi untuk perangkat Arduino.                          |
| **ArduinoJson**       | Benoît Blanchon     | 7.3.0   | Pengolahan data JSON secara efisien.                           |
| **FirebaseClient**    | Mobizt              | 2.0.0   | Library untuk integrasi Firebase (Realtime/Firestore).         |
| **FirebaseJson**      | Mobizt              | 3.0.9   | Mendukung manipulasi JSON untuk komunikasi dengan Firebase.    |
| **LiquidCrystal I2C** | Frank de Brabander  | 1.1.2   | Mengontrol LCD 16x2 melalui koneksi I2C.                       |
| **MFRC522**           | GitHub Community    | 1.4.11  | Driver untuk pembacaan RFID menggunakan modul MFRC522.         |

> 📌 **Tips:** Instalasi library dapat dilakukan melalui **Arduino Library Manager** atau PlatformIO sesuai dengan versi yang tercantum di atas.

## Pin Perangkat IoT

Berikut adalah konfigurasi pin perangkat IoT untuk berbagai komponen:

### LCD 16x2
| Pin LCD | Pin ESP32 |
|---------|-----------|
| GND     | GND       |
| VCC     | 3V3       |
| SDA     | PIN21     |
| SCL     | PIN22     |

### ESP32 COM SERIAL
| Pin ESP32 | Pin ESP32 lain |
|-----------|----------------|
| TX        | PIN16          |
| RX        | PIN17          |

### ESP32-CAM COM SERIAL
| Pin ESP32-CAM | Pin ESP32 lain |
|---------------|----------------|
| TX            | PIN14          |
| RX            | PIN15          |

### RFID
| Pin RFID | Pin ESP32 |
|----------|-----------|
| SDA      | PIN5      |
| SCK      | PIN18     |
| MOSI     | PIN23     |
| MISO     | PIN19     |
| IRQ      | NONE      |
| GND      | GND       |
| RST      | PIN4      |
| 3.3V     | 3V3       |

### Buzzer
| Pin Buzzer | Pin ESP32 |
|------------|-----------|
| - (negatif) | GND       |
| + (positif) | PIN33     |

### LED
| Pin LED | Pin ESP32 |
|---------|-----------|
| -       | GND       |
| +       | PIN15     |

### Button Reset
| Pin Button Reset | Pin ESP32 |
|------------------|-----------|
| - (negatif)      | GND       |
| + (positif)      | PIN32     |
