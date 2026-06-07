# Matrix LED 64×32 P3 HUB75 + ESP32

**Panel RGB HUB75 P3 de 64×32 píxeles controlado por ESP32 con MQTT para Home Assistant.**

## Características

- ✅ Conducción I2S DMA — sin parpadeo, color 24-bit real
- ✅ **4 pantallas automáticas** que ciclan cada 8 segundos
- ✅ Datos en vivo desde HA vía MQTT (temp oficina, exterior, rack, consumo UPS, solar)
- ✅ Reloj NTP sincronizado
- ✅ Brillo ajustable desde MQTT
- ✅ Comando JSON para texto scroll, modo manual o volver a auto-cycle
- ✅ Doble buffer para transiciones limpias
- ✅ Preparado para OTA

## Hardware

**Componentes:**
- ESP32 (DevKit)
- Matriz LED HUB75 P3 64×32 píxeles
- Fuente 5V (según consumo del panel)

### Pines (configurables en `include/config.h`)

| Señal | GPIO | Señal | GPIO |
|---|---|---|---|
| R1 | 25 | B2 | 13 |
| G1 | 26 | A | 23 |
| B1 | 27 | B | 22 |
| R2 | 14 | C | 5 |
| G2 | 12 | D | 17 |

| Señal | GPIO |
|---|---|
| LAT / STB | 4 |
| OE | 15 |
| CLK | 16 |

**⚠️ Pines a evitar:** GPIO 0, 2, 4, 5, 6–11, 12, 15 (strapping o flash).

> Cuando me digas el pinout que tienes, lo actualizamos.

## Software

### Requisitos

- [PlatformIO](https://platformio.org/)

### Compilar y flashear

```bash
git clone https://github.com/jcadt/matrix-led-64x32
cd matrix-led-64x32

# Editar configuración (WiFi, MQTT, pines)
nano include/config.h

# Compilar y subir
pio run -t upload

# Monitor serie
pio device monitor
```

### Primera vez

1. Edita `include/config.h` con SSID, contraseña WiFi e IP del broker MQTT.
2. Configura los pines según tu conexión física.
3. Compila y flashea.

## Pantallas automáticas (auto-cycle)

La matriz cambia de pantalla cada **8 segundos** automáticamente:

| # | Pantalla | Muestra | Colores |
|---|---|---|---|
| 0 | 🕐 **Reloj** | Hora grande + fecha | Verde / gris |
| 1 | 🌡 **Oficina** | Temp despacho (izq) + exterior (der) | Naranja / azul |
| 2 | 🖥 **Rack** | Temp rack (izq, color según calor) + consumo UPS (der) | Verde→naranja→rojo / cian |
| 3 | ☀️ **Solar** | Producción solar en W o kW + barra gráfica | Amarillo |

## MQTT

### Topics de sensores (HA publica, ESP32 recibe)

Para que la matriz muestre datos en vivo, configura automatizaciones en HA que publiquen en estos topics:

| Topic | Ejemplo payload | Descripción |
|---|---|---|
| `matrix/led/temp_despacho` | `22.5` | Temperatura oficina |
| `matrix/led/temp_exterior` | `18.3` | Temperatura exterior |
| `matrix/led/temp_rack` | `35.1` | Temperatura rack servidores |
| `matrix/led/potencia_ups` | `245` | Consumo UPS en vatios |
| `matrix/led/solar` | `4320` | Producción solar en vatios |

**Automation de ejemplo en HA:**

```yaml
automation:
  - alias: "Matrix LED - Publicar sensores"
    trigger:
      - platform: time_pattern
        seconds: "/30"
    action:
      - service: mqtt.publish
        data:
          topic: "matrix/led/temp_despacho"
          payload: "{{ states('sensor.temperatura_oficina') | round(1) }}"
      - service: mqtt.publish
        data:
          topic: "matrix/led/temp_exterior"
          payload: "{{ states('sensor.temperatura_exterior') | round(1) }}"
      - service: mqtt.publish
        data:
          topic: "matrix/led/temp_rack"
          payload: "{{ states('sensor.temperatura_rack') | round(1) }}"
      - service: mqtt.publish
        data:
          topic: "matrix/led/potencia_ups"
          payload: "{{ state_attr('sensor.ups', 'ups_load') | int }}"
      - service: mqtt.publish
        data:
          topic: "matrix/led/solar"
          payload: "{{ states('sensor.solar_production') | int }}"
```

### Topics de control (HA publica, ESP32 ejecuta)

| Topic | Payload | Efecto |
|---|---|---|
| `matrix/led/comando` | `{"tipo":"texto","texto":"Hola","color":"#FF0000"}` | Texto scroll (pausa auto-cycle) |
| `matrix/led/comando` | `{"tipo":"reloj","color":"#00FF00"}` | Solo reloj (pausa auto-cycle) |
| `matrix/led/comando` | `{"tipo":"pagina","pagina":0}` | Fuerza página 0-3 (pausa auto-cycle) |
| `matrix/led/comando` | `{"tipo":"auto"}` | Reanuda auto-cycle |
| `matrix/led/comando` | `{"tipo":"clear"}` | Apaga pantalla (pausa auto-cycle) |
| `matrix/led/comando` | `{"tipo":"brillo","valor":80}` | Brillo 0-255 |
| `matrix/led/texto` | `Texto plano` | Texto scroll (color por defecto) |
| `matrix/led/brillo` | `180` | Brillo 0-255 |

### Publicaciones (ESP32 envía)

| Topic | Payload | Descripción |
|---|---|---|
| `matrix/led/estado` | `{"modo":1,"pagina":2,"auto":true,"brillo":128,"sensores":{...}}` | Estado + todos los sensores (LWT: `offline`) |

## Home Assistant — MQTT Sensor

```yaml
mqtt:
  sensor:
    - name: "Matrix LED Estado"
      state_topic: "matrix/led/estado"
      value_template: "{{ value_json.texto }}"
      json_attributes_topic: "matrix/led/estado"
```

## Modos de visualización

- **Auto-cycle**: 4 páginas que rotan cada 8s (reloj → oficina → rack → solar)
- **Texto**: scroll horizontal si es largo (>8 chars), centrado si es corto
- **Reloj fijo**: solo reloj NTP
- **Página fija**: fuerza una página concreta
- **Vacío**: pantalla apagada

## Licencia

MIT
