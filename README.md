# Matrix LED 64×32 P3 HUB75 + ESP32

**Panel RGB HUB75 P3 de 64×32 píxeles controlado por ESP32 con MQTT para Home Assistant.**

## Características

- ✅ Conducción I2S DMA — sin parpadeo, color 24-bit real
- ✅ Control remoto vía MQTT (compatible con HA)
- ✅ Modos de visualización: texto scroll, reloj NTP, datos de sensor, iconos
- ✅ Brillo ajustable desde MQTT
- ✅ OTA para actualizaciones inalámbricas
- ✅ Doble buffer para transiciones limpias

## Hardware

| Componente | Modelo |
|---|---|
| Microcontrolador | ESP32 (cualquier placa DevKit) |
| Matriz LED | HUB75 P3 64×32 píxeles |
| Fuente | 5V — depende del panel (consultar consumo) |

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

- [PlatformIO](https://platformio.org/) (recomendado) o Arduino IDE

### Compilar y flashear

```bash
# Clonar el repo
git clone <url-del-repo>
cd matrix-led

# Editar configuración
nano include/config.h

# Compilar y subir
pio run -t upload

# Monitor serie
pio device monitor
```

### Primera vez

1. Edita `include/config.h` con tu SSID, contraseña WiFi e IP del broker MQTT.
2. Configura los pines según tu conexión física.
3. Compila y flashea.

## MQTT

### Suscripciones (el ESP32 escucha)

| Topic | Payload | Efecto |
|---|---|---|
| `matrix/led/comando` | `{"tipo":"texto","texto":"Hola","color":"#FF0000"}` | Muestra texto scroll |
| `matrix/led/comando` | `{"tipo":"reloj","color":"#00FF00"}` | Reloj NTP |
| `matrix/led/comando` | `{"tipo":"sensor","label":"Temp","valor":"23.5","color":"#FFAA00"}` | Sensor label+valor |
| `matrix/led/comando` | `{"tipo":"brillo","valor":80}` | Brillo 0-255 |
| `matrix/led/comando` | `{"tipo":"clear"}` | Apaga todo |
| `matrix/led/comando` | `{"tipo":"icono","icono":"corazon","color":"#FF0000"}` | Icono predefinido |
| `matrix/led/texto` | Texto plano | Muestra texto scroll (color por defecto) |
| `matrix/led/brillo` | `180` | Brillo 0-255 |

### Publicaciones (el ESP32 envía)

| Topic | Payload | Descripción |
|---|---|---|
| `matrix/led/estado` | `{"modo":2,"texto":"Temp 23.5","brillo":128,"ip":"192.168.1.50"}` | Estado actual (LWT: `offline`) |

## Home Assistant

### Configuración vía MQTT Discovery (próximamente)

De momento puedes añadirlo manualmente en `configuration.yaml`:

```yaml
mqtt:
  sensor:
    - name: "Matrix LED Estado"
      state_topic: "matrix/led/estado"
      value_template: "{{ value_json.texto }}"
      json_attributes_topic: "matrix/led/estado"
```

O usar automatizaciones para enviar comandos:

```yaml
automation:
  - alias: "Mostrar temperatura en matriz"
    trigger:
      - platform: time_pattern
        minutes: "/5"
    action:
      - service: mqtt.publish
        data:
          topic: "matrix/led/comando"
          payload: '{"tipo":"sensor","label":"Temp","valor":"{{ states('sensor.temperatura_exterior') | round(1) }}","color":"#00FF00"}'
```

## Modos de visualización

- **Texto**: scroll horizontal si es largo (>8 caracteres), centrado si es corto
- **Reloj**: hora actual vía NTP con fecha en gris tenue
- **Sensor**: label en línea 1, valor grande en línea 2
- **Icono**: dibujos predefinidos (TODO)
- **Vacío**: pantalla apagada

## Licencia

MIT
