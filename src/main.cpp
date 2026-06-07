/**
 * Matrix LED 64x32 P3 HUB75 — ESP32 + MQTT + Home Assistant
 *
 * Firmware para matriz LED RGB HUB75 P3 de 64x32 píxeles controlada por ESP32.
 * - Conducción por I2S DMA (sin parpadeo, 24-bit color)
 * - Múltiples pantallas con auto-cycle: reloj, temperaturas, rack, solar
 * - Datos recibidos por MQTT desde Home Assistant
 * - Comandos JSON para control manual (texto, brillo, clear)
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

// Topics de sensores (se construyen con prefijo)
static const char* TOPIC_TEMP_DESPACHO;
static const char* TOPIC_TEMP_EXTERIOR;
static const char* TOPIC_TEMP_RACK;
static const char* TOPIC_POTENCIA_UPS;
static const char* TOPIC_SOLAR;

// =============================================================================
//  GLOBALES
// =============================================================================
MatrixPanel_I2S_DMA* matrix = nullptr;
WiFiClient           wifiClient;
PubSubClient         mqtt(wifiClient);

// ── Sensores (valores recibidos por MQTT) ─────────────────────
struct {
  char tempDespacho[16]  = "--.-";
  char tempExterior[16]  = "--.-";
  char tempRack[16]      = "--.-";
  char potenciaUPS[16]   = "---";
  char solar[16]         = "---";
} sensores;

// ── Estado de la matriz ───────────────────────────────────────
enum PageType {
  PAGE_CLOCK,        // Reloj NTP
  PAGE_OFICINA,      // Temp despacho + exterior
  PAGE_RACK,         // Temp rack + consumo UPS
  PAGE_SOLAR,        // Producción solar
  PAGE_TEXT,         // Texto manual (scroll)
  PAGE_BLANK,        // Apagada
  PAGE_COUNT         // número de páginas automáticas
};

struct {
  PageType modo      = PAGE_CLOCK;
  char     texto[128]= "";
  uint32_t color     = COLOR_PRIMARY;
  uint8_t  brillo    = 128;
  bool     conectado = false;
} estado;

// Timer de auto-cycle
unsigned long cambioPaginaEn = 0;     // millis del último cambio
int           paginaActual   = 0;     // índice 0..(PAGE_COUNT-1)
bool          autoCycle      = true;

// Animaciones
unsigned long ultimoTickAnim = 0;
int           scrollOffset   = 0;

// =============================================================================
//  PROTOTIPOS
// =============================================================================
void conectarWiFi();
void conectarMQTT();
void mqttCallback(char* topic, byte* payload, unsigned int len);
void autoCycleLoop();
void dibujarPagina(int idx);

void mostrarReloj();
void mostrarOficina();
void mostrarRack();
void mostrarSolar();
void mostrarTexto(const char* txt, uint32_t color);
void mostrarTituloValor(const char* titulo, const char* valor,
                        const char* unidad, uint32_t colorTitulo);
void publicarEstado();

// =============================================================================
//  SETUP
// =============================================================================
void setup() {
  Serial.begin(115200);
  delay(100);
  Serial.println(F("\n[INIT] Matrix LED 64x32 P3 HUB75 — Multi-pantalla"));

  // ── Inicializar matriz ──────────────────────────────────
  HUB75_I2S_CFG::i2s_pins pines = {
    .r1   = PIN_R1, .g1   = PIN_G1, .b1   = PIN_B1,
    .r2   = PIN_R2, .g2   = PIN_G2, .b2   = PIN_B2,
    .a    = PIN_A,  .b    = PIN_B,  .c    = PIN_C,
    .d    = PIN_D,  .e    = -1,     .lat  = PIN_LAT,
    .oe   = PIN_OE, .clk  = PIN_CLK
  };

  HUB75_I2S_CFG cfg(MATRIX_WIDTH, MATRIX_HEIGHT, 1, pines);
  cfg.double_buffer = true;
  cfg.i2sspeed = HUB75_I2S_CFG::HZ_10M;

  matrix = new MatrixPanel_I2S_DMA(cfg);
  if (!matrix->begin()) {
    Serial.println(F("[ERROR] Matriz LED no iniciada"));
    while (1) delay(1000);
  }
  matrix->setBrightness8(estado.brillo);
  matrix->fillScreen(0);
  Serial.println(F("[OK] Matriz lista"));

  // ── Construir topics MQTT ────────────────────────────────
  static char b_cmd[64], b_txt[64], b_bri[64], b_est[64];
  static char b_td[64], b_te[64], b_tr[64], b_pu[64], b_so[64];
  snprintf(b_cmd, sizeof(b_cmd), "%s/comando",        MQTT_TOPIC_PREFIX);
  snprintf(b_txt, sizeof(b_txt), "%s/texto",          MQTT_TOPIC_PREFIX);
  snprintf(b_bri, sizeof(b_bri), "%s/brillo",         MQTT_TOPIC_PREFIX);
  snprintf(b_est, sizeof(b_est), "%s/estado",         MQTT_TOPIC_PREFIX);
  snprintf(b_td,  sizeof(b_td),  "%s/temp_despacho",  MQTT_TOPIC_PREFIX);
  snprintf(b_te,  sizeof(b_te),  "%s/temp_exterior",  MQTT_TOPIC_PREFIX);
  snprintf(b_tr,  sizeof(b_tr),  "%s/temp_rack",      MQTT_TOPIC_PREFIX);
  snprintf(b_pu,  sizeof(b_pu),  "%s/potencia_ups",   MQTT_TOPIC_PREFIX);
  snprintf(b_so,  sizeof(b_so),  "%s/solar",          MQTT_TOPIC_PREFIX);

  TOPIC_COMANDO      = b_cmd;
  TOPIC_TEXTO        = b_txt;
  TOPIC_BRILLO       = b_bri;
  TOPIC_ESTADO       = b_est;
  TOPIC_TEMP_DESPACHO= b_td;
  TOPIC_TEMP_EXTERIOR= b_te;
  TOPIC_TEMP_RACK    = b_tr;
  TOPIC_POTENCIA_UPS = b_pu;
  TOPIC_SOLAR        = b_so;

  // ── WiFi + MQTT ──────────────────────────────────────────
  mqtt.setServer(MQTT_BROKER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  mqtt.setKeepAlive(30);
  conectarWiFi();
  conectarMQTT();

  // ── NTP ──────────────────────────────────────────────────
  configTime(TZ_OFFSET, DST_OFFSET, NTP_SERVER);

  // Bienvenida — mostrar logo rápido
  estado.modo = PAGE_CLOCK;
  Serial.println(F("[READY] Sistema listo"));
}

// =============================================================================
//  LOOP
// =============================================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) conectarWiFi();
  if (!mqtt.connected()) conectarMQTT();
  mqtt.loop();

  // Auto-cycle de páginas
  if (autoCycle) autoCycleLoop();

  // Animaciones internas
  unsigned long now = millis();

  if (estado.modo == PAGE_CLOCK && now - ultimoTickAnim > 500) {
    ultimoTickAnim = now;
    mostrarReloj();
  }

  if (estado.modo == PAGE_TEXT) {
    int len = strlen(estado.texto);
    if (len * 8 > MATRIX_WIDTH && now - ultimoTickAnim > 100) {
      ultimoTickAnim = now;
      scrollOffset++;
      int totalW = len * 8;
      if (scrollOffset > totalW + MATRIX_WIDTH) scrollOffset = 0;
      matrix->fillScreen(0);
      matrix->setCursor(MATRIX_WIDTH - scrollOffset, 4);
      matrix->setTextColor(estado.color);
      matrix->print(estado.texto);
      matrix->flipDMABuffer();
    }
  }

  delay(10);
}

// =============================================================================
//  WIFI
// =============================================================================
void conectarWiFi() {
  Serial.print(F("[WiFi] Conectando a ")); Serial.println(WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);  // variable fija

  unsigned long inicio = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print('.');
    if (millis() - inicio > WIFI_TIMEOUT) {
      Serial.println(F("\n[WiFi] TIMEOUT — reiniciando..."));
      delay(1000); ESP.restart();
    }
  }
  Serial.print(F("\n[WiFi] IP: ")); Serial.println(WiFi.localIP());
}

// =============================================================================
//  MQTT
// =============================================================================
void conectarMQTT() {
  Serial.print(F("[MQTT] Conectando a ")); Serial.print(MQTT_BROKER);
  Serial.print(':'); Serial.println(MQTT_PORT);

  while (!mqtt.connected()) {
    if (mqtt.connect(MQTT_CLIENT_ID, MQTT_USER, MQTT_PASS,
                     TOPIC_ESTADO, 1, true, "offline")) {
      Serial.println(F("[MQTT] Conectado"));
      estado.conectado = true;

      // Comandos
      mqtt.subscribe(TOPIC_COMANDO);
      mqtt.subscribe(TOPIC_TEXTO);
      mqtt.subscribe(TOPIC_BRILLO);

      // Sensores
      mqtt.subscribe(TOPIC_TEMP_DESPACHO);
      mqtt.subscribe(TOPIC_TEMP_EXTERIOR);
      mqtt.subscribe(TOPIC_TEMP_RACK);
      mqtt.subscribe(TOPIC_POTENCIA_UPS);
      mqtt.subscribe(TOPIC_SOLAR);

      mqtt.publish(TOPIC_ESTADO, "online", true);
      publicarEstado();
    } else {
      Serial.print('.'); delay(2000);
    }
  }
}

// =============================================================================
//  CALLBACK MQTT
// =============================================================================
void mqttCallback(char* topic, byte* payload, unsigned int len) {
  if (strcmp(topic, TOPIC_ESTADO) == 0) return;

  char buf[len + 1];
  memcpy(buf, payload, len);
  buf[len] = '\0';

  Serial.printf("[MQTT] << %s: %s\n", topic, buf);

  // ── Comando JSON ────────────────────────────────────────
  if (strcmp(topic, TOPIC_COMANDO) == 0) {
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, buf);
    if (err) { Serial.printf("[JSON] Error: %s\n", err.c_str()); return; }

    const char* tipo = doc["tipo"];
    if (!tipo) return;

    if (strcmp(tipo, "texto") == 0) {
      autoCycle = false;
      estado.modo = PAGE_TEXT;
      const char* txt = doc["texto"] | " ";
      strncpy(estado.texto, txt, sizeof(estado.texto) - 1);
      const char* hex = doc["color"] | nullptr;
      if (hex && hex[0] == '#') estado.color = strtoul(hex + 1, nullptr, 16);
      mostrarTexto(estado.texto, estado.color);

    } else if (strcmp(tipo, "brillo") == 0) {
      int b = doc["valor"] | 128;
      estado.brillo = (uint8_t)constrain(b, 0, 255);
      matrix->setBrightness8(estado.brillo);

    } else if (strcmp(tipo, "clear") == 0) {
      autoCycle = false;
      estado.modo = PAGE_BLANK;
      matrix->fillScreen(0);
      matrix->flipDMABuffer();

    } else if (strcmp(tipo, "auto") == 0) {
      // Reanudar auto-cycle
      autoCycle = true;
      paginaActual = 0;
      cambioPaginaEn = 0;  // forzar refresco inmediato

    } else if (strcmp(tipo, "pagina") == 0) {
      // Ir a una página específica (0-3)
      int p = doc["pagina"] | -1;
      if (p >= 0 && p < PAGE_COUNT) {
        autoCycle = false;
        estado.modo = (PageType)p;
        paginaActual = p;
        dibujarPagina(p);
      }

    } else if (strcmp(tipo, "reloj") == 0) {
      const char* hex = doc["color"] | nullptr;
      if (hex && hex[0] == '#') estado.color = strtoul(hex + 1, nullptr, 16);
      autoCycle = false;
      estado.modo = PAGE_CLOCK;
      mostrarReloj();
    }

    publicarEstado();
    return;
  }

  // ── Texto plano ─────────────────────────────────────────
  if (strcmp(topic, TOPIC_TEXTO) == 0) {
    autoCycle = false;
    estado.modo = PAGE_TEXT;
    strncpy(estado.texto, buf, sizeof(estado.texto) - 1);
    mostrarTexto(estado.texto, estado.color);
    return;
  }

  // ── Brillo ───────────────────────────────────────────────
  if (strcmp(topic, TOPIC_BRILLO) == 0) {
    int b = atoi(buf);
    estado.brillo = (uint8_t)constrain(b, 0, 255);
    matrix->setBrightness8(estado.brillo);
    return;
  }

  // ── Sensores ─────────────────────────────────────────────
  if (strcmp(topic, TOPIC_TEMP_DESPACHO) == 0) {
    snprintf(sensores.tempDespacho, sizeof(sensores.tempDespacho), "%.1f", atof(buf));
    return;
  }
  if (strcmp(topic, TOPIC_TEMP_EXTERIOR) == 0) {
    snprintf(sensores.tempExterior, sizeof(sensores.tempExterior), "%.1f", atof(buf));
    return;
  }
  if (strcmp(topic, TOPIC_TEMP_RACK) == 0) {
    snprintf(sensores.tempRack, sizeof(sensores.tempRack), "%.1f", atof(buf));
    return;
  }
  if (strcmp(topic, TOPIC_POTENCIA_UPS) == 0) {
    snprintf(sensores.potenciaUPS, sizeof(sensores.potenciaUPS), "%d", atoi(buf));
    return;
  }
  if (strcmp(topic, TOPIC_SOLAR) == 0) {
    snprintf(sensores.solar, sizeof(sensores.solar), "%d", atoi(buf));
    return;
  }
}

// =============================================================================
//  AUTO-CYCLE
// =============================================================================
const unsigned long TIEMPO_PAGINA_MS = 8000;  // 8 segundos por página
const int TOTAL_PAGINAS = 4;  // CLOCK, OFICINA, RACK, SOLAR

void autoCycleLoop() {
  if (estado.modo >= PAGE_TEXT) {
    // Si está en modo texto o blank, no forzar ciclo, pero
    // si cambiamos a página válida, retomar
    return;
  }

  unsigned long now = millis();
  if (cambioPaginaEn == 0 || now - cambioPaginaEn >= TIEMPO_PAGINA_MS) {
    cambioPaginaEn = now;

    // Si estaba en una página automática, avanzar
    if (estado.modo >= 0 && estado.modo < TOTAL_PAGINAS) {
      paginaActual = (paginaActual + 1) % TOTAL_PAGINAS;
    } else {
      paginaActual = 0;  // reset
    }

    estado.modo = (PageType)paginaActual;
    dibujarPagina(paginaActual);
  }
}

// =============================================================================
//  DIBUJAR PÁGINA POR ÍNDICE
// =============================================================================
void dibujarPagina(int idx) {
  switch (idx) {
    case 0: estado.modo = PAGE_CLOCK;  mostrarReloj();   break;
    case 1: estado.modo = PAGE_OFICINA; mostrarOficina(); break;
    case 2: estado.modo = PAGE_RACK;   mostrarRack();    break;
    case 3: estado.modo = PAGE_SOLAR;  mostrarSolar();   break;
  }
}

// =============================================================================
//  PÁGINA 0: RELOJ
// =============================================================================
void mostrarReloj() {
  struct tm t;
  if (!getLocalTime(&t, 500)) {
    mostrarTituloValor("NTP", "sinc...", "", 0x444444);
    estado.modo = PAGE_CLOCK;
    return;
  }

  char buf[16];
  matrix->fillScreen(0);

  // Hora grande (aprox 32px = 4 chars a 8x8)
  snprintf(buf, sizeof(buf), "%02d:%02d", t.tm_hour, t.tm_min);
  matrix->setCursor(6, 2);
  matrix->setTextColor(estado.color);
  matrix->print(buf);

  // Segundos parpadeando (punto a la derecha cada 2s)
  if (t.tm_sec % 2 == 0) {
    matrix->setCursor(52, 6);
    matrix->setTextColor(estado.color);
    matrix->print(":");
  }

  // Fecha abajo
  snprintf(buf, sizeof(buf), "%02d/%02d/%04d", t.tm_mday, t.tm_mon + 1, t.tm_year + 1900);
  matrix->setCursor(8, 22);
  matrix->setTextColor(0x555555);
  matrix->print(buf);

  matrix->flipDMABuffer();
}

// =============================================================================
//  PÁGINA 1: OFICINA (temp despacho + exterior)
// =============================================================================
void mostrarOficina() {
  matrix->fillScreen(0);

  // ── Icono/indicador despacho (izquierda) ──
  matrix->setCursor(2, 0);
  matrix->setTextColor(0xFF8800);   // naranja
  matrix->print("Ofi");

  matrix->setCursor(2, 14);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(sensores.tempDespacho);
  // Símbolo °
  matrix->setCursor(2 + strlen(sensores.tempDespacho) * 8, 14);
  matrix->print((char)248);  // °

  // ── Exterior (derecha) ──
  matrix->setCursor(34, 0);
  matrix->setTextColor(0x44AAFF);   // azul claro
  matrix->print("Ext");

  matrix->setCursor(34, 14);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(sensores.tempExterior);
  matrix->setCursor(34 + strlen(sensores.tempExterior) * 8, 14);
  matrix->print((char)248);

  matrix->flipDMABuffer();
}

// =============================================================================
//  PÁGINA 2: RACK (temp rack + consumo UPS)
// =============================================================================
void mostrarRack() {
  matrix->fillScreen(0);

  // ── Temp rack (izquierda) ──
  float t = atof(sensores.tempRack);
  uint32_t colorTemp = 0x00CC00;  // verde
  if (t > 35) colorTemp = 0xFFAA00;  // naranja
  if (t > 45) colorTemp = 0xFF0000;  // rojo

  matrix->setCursor(2, 0);
  matrix->setTextColor(colorTemp);
  matrix->print("Rack");

  matrix->setCursor(2, 14);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(sensores.tempRack);
  int x1 = 2 + strlen(sensores.tempRack) * 8;
  matrix->setCursor(x1, 14);
  matrix->setTextColor(colorTemp);
  matrix->print((char)248);

  // ── UPS (derecha) ──
  matrix->setCursor(34, 0);
  matrix->setTextColor(0x00DDFF);   // cian
  matrix->print("UPS");

  matrix->setCursor(34, 14);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(sensores.potenciaUPS);
  int x2 = 34 + strlen(sensores.potenciaUPS) * 8;
  matrix->setCursor(x2, 14);
  matrix->setTextColor(0x888888);
  matrix->print("W");

  matrix->flipDMABuffer();
}

// =============================================================================
//  PÁGINA 3: SOLAR
// =============================================================================
void mostrarSolar() {
  matrix->fillScreen(0);

  // ── Icono solar ☀ ──
  matrix->setCursor(2, 0);
  matrix->setTextColor(0xFFDD00);   // amarillo
  matrix->print("Sol");

  // ── Valor grande centrado ──
  int valor = atoi(sensores.solar);
  char buf[16];

  if (valor >= 1000) {
    snprintf(buf, sizeof(buf), "%.1fkW", valor / 1000.0);
  } else {
    snprintf(buf, sizeof(buf), "%dW", valor);
  }

  int x = (MATRIX_WIDTH - strlen(buf) * 8) / 2;
  if (x < 0) x = 0;
  matrix->setCursor(x, 14);
  matrix->setTextColor(0xFFDD00);
  matrix->print(buf);

  // Barra de progreso visual (ancho proporcional)
  int ancho = constrain(valor / 30, 2, MATRIX_WIDTH - 4);
  // Si hay espacio, dibujar barra
  if (ancho > 4) {
    matrix->fillRect(2, 28, ancho, 3, 0xFFDD00);
  }

  matrix->flipDMABuffer();
}

// =============================================================================
//  UTILIDAD: MOSTRAR TÍTULO + VALOR
// =============================================================================
void mostrarTituloValor(const char* titulo, const char* valor,
                        const char* unidad, uint32_t colorTitulo) {
  matrix->fillScreen(0);
  matrix->setCursor(2, 0);
  matrix->setTextColor(colorTitulo);
  matrix->print(titulo);

  matrix->setCursor(2, 16);
  matrix->setTextColor(0xFFFFFF);
  matrix->print(valor);
  if (strlen(unidad) > 0) {
    matrix->setCursor(2 + strlen(valor) * 8, 16);
    matrix->setTextColor(0x888888);
    matrix->print(unidad);
  }
  matrix->flipDMABuffer();
}

// =============================================================================
//  MOSTRAR TEXTO (scroll si es largo)
// =============================================================================
void mostrarTexto(const char* txt, uint32_t color) {
  matrix->fillScreen(0);
  int len = strlen(txt);
  if (len <= 0) { matrix->flipDMABuffer(); return; }

  if (len * 8 <= MATRIX_WIDTH) {
    int x = (MATRIX_WIDTH - len * 8) / 2;
    matrix->setCursor(x, 4);
    matrix->setTextColor(color);
    matrix->print(txt);
    scrollOffset = 0;
  } else {
    scrollOffset = 0;
    matrix->setCursor(MATRIX_WIDTH, 4);
    matrix->setTextColor(color);
    matrix->print(txt);
  }
  matrix->flipDMABuffer();

  estado.modo = PAGE_TEXT;
  strncpy(estado.texto, txt, sizeof(estado.texto) - 1);
  estado.texto[sizeof(estado.texto) - 1] = '\0';
  estado.color = color;
}

// =============================================================================
//  PUBLICAR ESTADO
// =============================================================================
void publicarEstado() {
  if (!mqtt.connected()) return;
  JsonDocument doc;
  doc["modo"]    = (int)estado.modo;
  doc["pagina"]  = paginaActual;
  doc["auto"]    = autoCycle;
  doc["brillo"]  = estado.brillo;
  doc["ip"]      = WiFi.localIP().toString();
  doc["texto"]   = estado.texto;

  JsonObject s = doc["sensores"].to<JsonObject>();
  s["temp_despacho"] = sensores.tempDespacho;
  s["temp_exterior"] = sensores.tempExterior;
  s["temp_rack"]     = sensores.tempRack;
  s["potencia_ups"]  = sensores.potenciaUPS;
  s["solar"]         = sensores.solar;

  char buf[384];
  serializeJson(doc, buf);
  mqtt.publish(TOPIC_ESTADO, buf);
}

// =============================================================================
//  OTA — COMPATIBLE
//  (Añadir #include <ArduinoOTA.h> en includes y llamar ArduinoOTA.begin()
//   en setup si se desea OTA. Por ahora, flasheo por USB.)
// =============================================================================
