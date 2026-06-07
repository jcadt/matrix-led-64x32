// =============================================================================
//  CONFIGURACIÓN — Edita estos valores antes de flashear
// =============================================================================
#ifndef CONFIG_H
#define CONFIG_H

// ── WiFi ────────────────────────────────────────────────────────────────────
const char* WIFI_SSID              = "TU_SSID";
const char* WIFI_PASSWORD    = "TU_PASSWORD";
const unsigned long WIFI_TIMEOUT   = 30000;   // ms

// ── MQTT (Home Assistant) ───────────────────────────────────────────────────
const char* MQTT_BROKER            = "192.168.1.100";
const int   MQTT_PORT              = 1883;
const char* MQTT_USER              = "";
const char* MQTT_PASS              = "";
const char* MQTT_CLIENT_ID         = "matrix-led-01";

// Prefijo de topics MQTT (todos los topics se cuelgan de aquí)
//   <prefijo>/comando   — JSON de control (ver main.cpp para formatos)
//   <prefijo>/texto     — texto plano en scroll
//   <prefijo>/brillo    — valor 0-255
const char* MQTT_TOPIC_PREFIX      = "matrix/led";

// ── NTP (para modo reloj) ───────────────────────────────────────────────────
const char* NTP_SERVER             = "pool.ntp.org";
const int   TZ_OFFSET              = 3600;   // UTC+1 (Europa Madrid)
const int   DST_OFFSET             = 3600;   // +1h verano

// ── PINES HUB75 (MATRIZ LED 64x32 P3) ───────────────────────────────────────
// Edita según tu conexión. No uses GPIO 0,2,4,5,6-11,12,15.
const uint8_t PIN_R1  = 25;
const uint8_t PIN_G1  = 26;
const uint8_t PIN_B1  = 27;
const uint8_t PIN_R2  = 14;
const uint8_t PIN_G2  = 12;
const uint8_t PIN_B2  = 13;
const uint8_t PIN_A   = 23;
const uint8_t PIN_B   = 22;
const uint8_t PIN_C   = 5;
const uint8_t PIN_D   = 17;
const uint8_t PIN_LAT = 4;
const uint8_t PIN_OE  = 15;
const uint8_t PIN_CLK = 16;

// ── DIMENSIONES ─────────────────────────────────────────────────────────────
const int MATRIX_WIDTH  = 64;
const int MATRIX_HEIGHT = 32;

// ── COLORES POR DEFECTO ─────────────────────────────────────────────────────
const uint32_t COLOR_PRIMARY   = 0x00FF00;   // verde
const uint32_t COLOR_SECONDARY = 0xCCCCCC;   // gris claro

#endif // CONFIG_H
