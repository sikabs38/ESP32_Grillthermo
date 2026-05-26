# Grill Buddy

Firmware für die Anzeige der Temperaturen eines Otto Wilde Grills G32. Das Projekt ist ein Ersatz für den nicht mehr erhältlichen Original-Grill-Buddy der Firma.

## Hardware

- **Board:** ESP32-S3 DevKitC
- **Grill:** Otto Wilde G32

## Funktionen

- Bluetooth-Kommunikation mit dem Grill
- Anzeige von 4 Brennerbereichen und 4 Thermometern
- Bereitstellung der Daten über einen Webserver
- Bereitstellung der Daten über MQTT
- Konfigurationsshell über USB (CDC-ACM)

## Entwicklungsumgebung

- **RTOS:** Zephyr OS 4.4
- **Buildsystem:** west

## Dateistruktur

```
GrillBuddy/
├── west.yml                          # West-Manifest, referenziert Zephyr 4.4.0
├── .gitignore
├── README.md
└── app/                              # Firmware-Quellcode
    ├── CMakeLists.txt                # CMake Build-Definition
    ├── prj.conf                      # Kconfig-Projektkonfiguration
    ├── boards/
    │   ├── esp32s3_devkitc_procpu.conf     # Board-spezifische Kconfig-Optionen
    │   └── esp32s3_devkitc_procpu.overlay  # DTS-Overlay (USB CDC-ACM)
    └── src/
        ├── main.c                          # Einstiegspunkt der Firmware
        └── shell.c                         # USB-Shell-Kommandos
```

### Beschreibung der Dateien

| Datei | Beschreibung |
|---|---|
| `west.yml` | West-Workspace-Manifest. Definiert Zephyr als Abhängigkeit und dessen Version. |
| `app/CMakeLists.txt` | CMake-Konfiguration für das Zephyr-Build-System. Listet alle zu kompilierenden Quelldateien. |
| `app/prj.conf` | Kconfig-Konfiguration. Aktiviert Zephyr-Module wie Bluetooth, WiFi, MQTT und HTTP-Server. |
| `app/boards/esp32s3_devkitc_procpu.conf` | Board-spezifische Kconfig-Optionen für das ESP32-S3 DevKitC. |
| `app/boards/esp32s3_devkitc_procpu.overlay` | DTS-Overlay: leitet Shell und Console auf USB CDC-ACM um. |
| `app/src/main.c` | Einstiegspunkt der Firmware, initialisiert den USB-Stack. |
| `app/src/shell.c` | Shell-Kommandos zur Konfiguration von WiFi, MQTT und System. |

## Erste Schritte

### Voraussetzungen

West und das Zephyr SDK müssen installiert sein. Siehe [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/develop/getting_started/index.html).

Zephyr OS liegt unter `~/zephyrproject`. Die Umgebungsvariable `ZEPHYR_BASE` muss gesetzt sein:

```bash
export ZEPHYR_BASE=~/zephyrproject/zephyr
source ~/zephyrproject/zephyr/zephyr-env.sh
```

### Repository initialisieren

```bash
west init -m <repo-url> grill-buddy-workspace
cd grill-buddy-workspace
west update
```

### Firmware bauen

```bash
cd app
west build -b esp32s3_devkitc/esp32s3/procpu --cmake-only
west build
```

### Firmware flashen

```bash
west flash
```

### Shell-Zugriff via USB

Nach dem Flashen erscheint das Gerät als virtueller serieller Port (USB CDC-ACM). Verbindung herstellen mit:

```bash
screen /dev/ttyACM0 115200
# oder
minicom -D /dev/ttyACM0 -b 115200
```

Verfügbare Kommandos:

| Kommando | Beschreibung |
|---|---|
| `wifi set <ssid> <password>` | WiFi-Zugangsdaten konfigurieren |
| `mqtt set <broker> <port>` | MQTT-Broker konfigurieren |
| `config show` | Aktuelle Konfiguration anzeigen |
| `help` | Alle verfügbaren Kommandos anzeigen |

## Coding Guidelines

Der Code folgt dem MISRA-C-Standard.
