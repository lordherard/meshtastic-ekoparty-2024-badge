#pragma once

#define HAS_GPS 0
#define HAS_BUTTON 0
#define HAS_SENSOR 0
// The published PCB has no external telemetry sensors or battery gauge.
#define MESHTASTIC_EXCLUDE_POWER_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR 1
#define MESHTASTIC_EXCLUDE_HEALTH_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_ACCELEROMETER 1
#define MESHTASTIC_EXCLUDE_MAGNETOMETER 1
#define I2C_SDA 6
#define I2C_SCL 7
#define SERIAL_PRINT_PORT 1

#define USE_RF95
#define LORA_SCK 21
#define LORA_MISO 20
#define LORA_MOSI 19
#define LORA_CS 18
#define LORA_RESET 8
#define LORA_DIO0 5
#define LORA_DIO1 2
#define LORA_DIO2 4
#define RF95_MAX_POWER 10

#define EKO_UP 22
#define EKO_DOWN 23
#define EKO_BACK 1
#define EKO_SELECT 15
#define EKO_MENU 9
#define EKO_PIXELS 10
#define EKO_AUDIO_PWM 11
// U1 pad 8 is GPIO0 in Espressif's datasheet; the PCB symbol says IO40.
#define EKO_AUDIO_SD 0
// TPA321 routing and L4 were physically tested; electrical characterization remains pending.
#define EKO_AUDIO_VERIFIED 1
