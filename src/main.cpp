/**
 * Matrix LED 64x32 P3 HUB75 — ESP32 + MQTT + Home Assistant
 *
 * Firmware para matriz LED RGB HUB75 P3 de 64x32 píxeles controlada por ESP32.
 * - Conducción por I2S DMA (sin parpadeo, 24-bit color)
 * - Control remoto vía MQTT (compatible con Home Assistant)
 * - Modos: texto scroll, reloj NTP, datos de sensor, iconos simples
 * - OTA para actualizaciones inalámbricas
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "config.h"

// =============================================================================
//  CONSTANTES
// =============================================================================
static const char* TOPIC_COMANDO;
static const char* TOPIC_TEXTO;
static const char* TOPIC_BRILLO;
static const char* TOPIC_ESTADO;

// =============================================================================
//  GLOBALES
// =============================================================================
MatrixPanel_I2S_DMA* matrix = nullptr;
WiFiClient           wifiClient;
PubSubClient         mqtt(wifiClient);

// Estado interno
enum ModoDisplay {
  MODO_TEXTO,       // texto estático / scrolling
  MODO_RELOJ,       // reloj NTP
  MODO_SENSOR,      // etiqueta + valor (ej: "Temp 23.5°")
  MODO_ICONO,       // icono predefinido
  MODO_VACIO        // pantalla en negro
};

struct EstadoMatriz {
  ModoDisplay modo       = MODO_VACIO;
  char        texto[128] = "";
  uint32_t    color      = COLOR_PRIMARY;
  uint8_t     brillo     = 128;
  bool        conectado  = false;
} estado;

unsigned long ultimoTickAnim = 0;
int           scrollOffset   = 0;

// =============================================================================
//  PROTOTIPOS
// =============================================================================
void conectarWiFi();
void conectarMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void procesarComando(const char* json);
void mostrarTexto(const char* txt, uint32_t color);
void mostrarReloj();
void mostrarSensor(const char* label, const char* valor, uint32_t color);
void loopDisplay();
void publicarEstado();

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[INIT] Matrix LED 64x32 P3 HUB75"));

  // ── Inicializar matriz ──────────────────────────────────
  HUB75_I2S_CFG::i2s_pins pines = {
    .r1   = PIN_R1,
    .g1   = PIN_G1,
    .b1   = PIN_B1,
    .r2   = PIN_R2,
    .g2   = PIN_G2,
    .b2   = PIN_B2,
    .a    = PIN_A,
    .b    = PIN_B,
    .c    = PIN_C,
    .d    = PIN_D,
    .e    = -1,          // no usado en 32 filas
    .lat  = PIN_LAT,
    .oe   = PIN_OE,
    .clk  = PIN_CLK
  };

  HUB75_I2S_CFG cfg(MATRIX_WIDTH, MATRIX_HEIGHT, 1, pines);
  cfg.double_buffer = true;   // evita parpadeo al redibujar
  cfg.i2sspeed = HUB75_I2S_CFG::HZ_10M;  // velocidad estable

  matrix = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix->begin()) {
    Serial.println(F("[ERROR] No se pudo iniciar la matriz LED"));
    while (1) delay(1000);  // bloqueado
  }
  matrix->setBrightness8(estado.brillo);
  matrix->fillScreen(0);  // todo apagado
  Serial.println(F("[OK] Matriz iniciada"));

  // ── Construir topics MQTT ────────────────────────────────
  static char buf_cmd[64], buf_txt[64], buf_bri[64], buf_est[64];
  snprintf(buf_cmd, sizeof(buf_cmd), "%s/comando",   MQTT_TOPIC_PREFIX);
  snprintf(buf_txt, sizeof(buf_txt), "%s/texto",     MQTT_TOPIC_PREFIX);
  snprintf(buf_bri, sizeof(buf_bri), "%s/brillo",    MQTT_TOPIC_PREFIX);
  snprintf(buf_est, sizeof(buf_est), "%s/estado",    MQTT_TOPIC_PREFIX);
  TOPIC_COMANDO = buf_cmd;
  TOPIC_TEXTO   = buf_txt;
  TOPIC_BRILLO  = buf_bri;
  TOPIC_ESTADO  = buf_est;

  // ── WiFi + MQTT ──────────────────────────────────────────
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  conectarWiFi();
  conectarMQTT();

  // ── NTP ──────────────────────────────────────────────────
  configTime(TZ_OFFSET, DST_OFFSET, NTP_SERVER);

  // Pantalla de bienvenida
  matrix->setBrightness8(estado.brillo);
  mostrarTexto("Matrix OK", 0x00FF00);
  Serial.println(F("[READY] Sistema listo"));
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    conectarWiFi();
  }
  if (!mqtt.connected()) {
    conectarMQTT();
  }
  mqtt.loop();

  loopDisplay();
  delay(10);  // estabilidad
}

// =============================================================================
//  WIFI
// =============================================================================
void conectarWiFi() {
  Serial.print(F("[WiFi] Conectando a "));
  Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // variable fija

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print('.');
    if (millis() - inicio > WIFI_TIMEOUT) {
      Serial.println(F("\n[WiFi] TIMEOUT — reiniciando..."));
      delay(1000);
      ESP.restart();
    }
  }
  Serial.print(F("\n[WiFi] Conectado: "));
  Serial.println(WiFi.localIP());
}

// =============================================================================
//  MQTT
// =============================================================================
void conectarMQTT() {
  Serial.print(F("[MQTT] Conectando a "));
  Serial.print(MQTT_BROKER);
  Serial.print(':');
  Serial.println(MQTT_PORT);

  while (!mqtt.connected()) {
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS, TOPIC_ESTADO, 1, true, "offline")) {
      Serial.println(F("[MQTT] Conectado"));
      estado.conectado = true;

      // Suscribirse a comandos
      mqtt.subscribe(TOPIC_COMANDO);
      mqtt.subscribe(TOPIC_TEXTO);
      mqtt.subscribe(TOPIC_BRILLO);

      // Publicar birth message (LWT)
      mqtt.publish(TOPIC_ESTADO, "online", true);
      publicarEstado();
    } else {
      Serial.print('.');
      delay(2000);
    }
  }
}

// =============================================================================
//  CALLBACK MQTT
// =============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  // Ignorar mensajes nuestros
  if (strcmp(topic, TOPIC_ESTADO) == 0) return;

  // Asegurar terminación nula
  char buf[len + 1];
  memcpy(buf, payload, len);
  buf[len] = '\0';

  Serial.printf("[MQTT] << %s: %s\n", topic, buf);

  if (strcmp(topic, TOPIC_COMANDO) == 0) {
    procesarComando(buf);
  } else if (strcmp(topic, TOPIC_TEXTO) == 0) {
    estado.modo = MODO_TEXTO;
    strncpy(estado.texto, buf, sizeof(estado.texto) - 1);
    estado.texto[sizeof(estado.texto) - 1] = '\0';
    mostrarTexto(estado.texto, estado.color);
  } else if (strcmp(topic, TOPIC_BRILLO) == 0) {
    int b = atoi(buf);
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    estado.brillo = (uint8_t)b;
    matrix->setBrightness8(estado.brillo);
    Serial.printf("[MATRIX] Brillo: %d\n", estado.brillo);
  }
}

// =============================================================================
//  PROCESAR COMANDO JSON
// =============================================================================
void procesarComando(const char* json) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  if (err) {
    Serial.printf("[JSON] Error parseando: %s\n", err.c_str());
    return;
  }

  const char* tipo = doc["tipo"];

  if (!tipo) return;

  if (strcmp(tipo, "texto") == 0) {
    estado.modo = MODO_TEXTO;
    const char* txt = doc["texto"] | " ";
    strncpy(estado.texto, txt, sizeof(estado.texto) - 1);
    estado.texto[sizeof(estado.texto) - 1] = '\0';
    const char* hex = doc["color"] | nullptr;
    if (hex && hex[0] == '#') {
      estado.color = strtoul(hex + 1, nullptr, 16);
    }
    mostrarTexto(estado.texto, estado.color);

  } else if (strcmp(tipo, "reloj") == 0) {
    estado.modo = MODO_RELOJ;
    const char* hex = doc["color"] | nullptr;
    if (hex && hex[0] == '#') {
      estado.color = strtoul(hex + 1, nullptr, 16);
    }

  } else if (strcmp(tipo, "sensor") == 0) {
    estado.modo = MODO_SENSOR;
    const char* label = doc["label"] | "";
    const char* valor = doc["valor"] | "--";
    const char* hex   = doc["color"] | nullptr;
    uint32_t c = estado.color;
    if (hex && hex[0] == '#') {
      c = strtoul(hex + 1, nullptr, 16);
    }
    snprintf(estado.texto, sizeof(estado.texto), "%s %s", label, valor);
    estado.color = c;
    mostrarSensor(label, valor, c);

  } else if (strcmp(tipo, "brillo") == 0) {
    int b = doc["valor"] | 128;
    if (b < 0) b = 0;
    if (b > 255) b = 255;
    estado.brillo = (uint8_t)b;
    matrix->setBrightness8(estado.brillo);

  } else if (strcmp(tipo, "clear") == 0) {
    estado.modo = MODO_VACIO;
    matrix->fillScreen(0);
    matrix->flipDMABuffer();

  } else if (strcmp(tipo, "icono") == 0) {
    estado.modo = MODO_ICONO;
    const char* icono = doc["icono"] | "corazon";
    const char* hex   = doc["color"] | nullptr;
    uint32_t c = estado.color;
    if (hex && hex[0] == '#') {
      c = strtoul(hex + 1, nullptr, 16);
    }
    estado.color = c;
    // dibujarIcono(icono, c);  // TODO: implementar iconos bitmap
    mostrarTexto(icono, c);  // fallback: mostrar nombre
  }

  publicarEstado();
}

// =============================================================================
//  MOSTRAR TEXTO (scroll horizontal si es largo)
// =============================================================================
void mostrarTexto(const char* txt, uint32_t color) {
  matrix->fillScreen(0);

  int len = strlen(txt);
  if (len <= 0) {
    matrix->flipDMABuffer();
    return;
  }

  // A 8x8 px por carácter: caben ~8 caracteres en 64px.
  // Si es corto, centrado estático.
  if (len * 8 <= MATRIX_WIDTH) {
    int x = (MATRIX_WIDTH - len * 8) / 2;
    matrix->setCursor(x, 4);
    matrix->setTextColor(color);
    matrix->print(txt);
    scrollOffset = 0;
  } else {
    // Largo: scroll horizontal
    scrollOffset = 0;
    matrix->setCursor(MATRIX_WIDTH, 4);
    matrix->setTextColor(color);
    matrix->print(txt);
  }

  matrix->flipDMABuffer();

  // Guardar en estado
  estado.modo = MODO_TEXTO;
  strncpy(estado.texto, txt, sizeof(estado.texto) - 1);
  estado.texto[sizeof(estado.texto) - 1] = '\0';
  estado.color = color;
}

// =============================================================================
//  MOSTRAR RELOJ
// =============================================================================
void mostrarReloj() {
  struct tm t;
  if (!getLocalTime(&t, 500)) {
    // NTP no sincronizado aún
    mostrarSensor("NTP", "sinc...", estado.color);
    estado.modo = MODO_RELOJ;
    return;
  }

  char hora[9];
  snprintf(hora, sizeof(hora), "%02d:%02d:%02d", t.tm_hour, t.tm_min, t.tm_sec);

  matrix->fillScreen(0);
  matrix->setCursor(4, 4);
  matrix->setTextColor(estado.color);
  matrix->print(hora);

  // Segundos parpadeantes abajo a la derecha
  char fecha[12];
  snprintf(fecha, sizeof(fecha), "%02d-%02d-%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  matrix->setCursor(4, 20);
  matrix->setTextColor(0x444444);
  matrix->print(fecha);

  matrix->flipDMABuffer();
}

// =============================================================================
//  MOSTRAR SENSOR
// =============================================================================
void mostrarSensor(const char* label, const char* valor, uint32_t color) {
  matrix->fillScreen(0);

  // Label línea 1
  matrix->setCursor(2, 0);
  matrix->setTextColor(color);
  matrix->print(label);

  // Valor línea 2 (más grande si es número)
  matrix->setCursor(2, 16);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(valor);

  matrix->flipDMABuffer();
}

// =============================================================================
//  LOOP DE DISPLAY (se ejecuta en cada iteración del loop principal)
// =============================================================================
void loopDisplay() {
  unsigned long now = millis();

  switch (estado.modo) {
    case MODO_RELOJ:
      // Actualizar reloj cada 500ms
      if (now - ultimoTickAnim > 500) {
        ultimoTickAnim = now;
        mostrarReloj();
      }
      break;

    case MODO_TEXTO:
      // Scroll horizontal si el texto es largo
      if ((int)strlen(estado.texto) * 8 > MATRIX_WIDTH) {
        if (now - ultimoTickAnim > 100) {  // 100ms por frame de scroll
          ultimoTickAnim = now;
          scrollOffset++;
          int totalW = strlen(estado.texto) * 8;
          int resetAt = totalW + MATRIX_WIDTH;
          if (scrollOffset > resetAt) scrollOffset = 0;

          matrix->fillScreen(0);
          matrix->setCursor(MATRIX_WIDTH - scrollOffset, 4);
          matrix->setTextColor(estado.color);
          matrix->print(estado.texto);
          matrix->flipDMABuffer();
        }
      }
      break;

    case MODO_SENSOR:
    case MODO_VACIO:
    case MODO_ICONO:
      // Estáticos, no necesitan refresco continuo
      break;
  }
}

// =============================================================================
//  PUBLICAR ESTADO
// =============================================================================
void publicarEstado() {
  if (!mqtt.connected()) return;

  JsonDocument doc;
  doc["modo"]   = (int)estado.modo;
  doc["texto"]  = estado.texto;
  doc["brillo"] = estado.brillo;
  doc["ip"]     = WiFi.localIP().toString();

  char buf[256];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_ESTADO, buf);
}
