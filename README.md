## IoT-Absensi-RFIDdanESP32CAM

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
