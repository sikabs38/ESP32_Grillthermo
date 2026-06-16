# ESP32 Grillthermo

# Beschreibung

Das Projekt ist eine Firmware für die Anzeige der Temperaturen von einem Otto Wilde Grill G32. Sie ersetzt funktional das nicht mehr erhältliche Original-Anzeigegerät der Firma.

# Hardware

Basis ist ein ESP32-S3 DevKitC Board. Die Daten werden von einem Webserver bereit gestellt. Zusätzlich werden die Daten über einem MQTT Server zur Verfügung gestellt.

Die Kommunikation mit dem Grill erfolgt über Bluetooth.

Angezeigt werden die vier Bereiche der Brenner sowie die vier Thermometer.

# Entwicklungsumgebung

+ Zephyr OS Version 4.4, installiert unter `~/zephyrproject`
+ Buildsystem: west

# Coding Guidelines

+ MISRA C gilt für den Code

# ADRs

| Nr.      | Titel                                              | Status   |
|----------|----------------------------------------------------|----------|
| ADR-0001 | [Webserver und MQTT-Client gegenseitig ausschließen](doc/adr/0001-netzwerk-modus.md) | Accepted |