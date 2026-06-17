# Requirements: Statusanzeige

## 1. Übersicht

Die Statusanzeige nutzt die RGB-LED (WS2812, GPIO 48) auf dem ESP32-S3 DevKitC, um den aktuellen Verbindungszustand (WLAN und Bluetooth) per Farbe anzuzeigen. Die Farbwahl berücksichtigt Rot-Grün-Farbsehschwäche: Die Kombination Rot/Grün wird vermieden. Stattdessen wird ein Orange/Blau/Cyan/Weiß-Schema verwendet, das auch bei Deuteranopie und Protanopie eindeutig unterscheidbar ist.

**Farbschema:**

| Zustand | Farbe | Verhalten |
|---------|-------|-----------|
| Keine Verbindung (weder WLAN noch BT) | Orange | Dauerhaft |
| Nur Bluetooth verbunden | Blau | Dauerhaft |
| Nur WLAN verbunden | Cyan | Dauerhaft |
| WLAN und Bluetooth verbunden | Weiß | Dauerhaft |

---

## 2. Funktionale Anforderungen

### STA-REQ-01

#### Beschreibung

Das Statusanzeige-Modul soll beim Systemstart die RGB-LED initialisieren. Die LED wird über den WS2812-Treiber (Zephyr LED-Strip-API) angesteuert. Nach der Initialisierung wird sofort der initiale Verbindungszustand (keine Verbindung → Orange) angezeigt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Nach dem Start leuchtet die RGB-LED orange
- Die Initialisierung schlägt bei fehlendem Device-Node im Devicetree mit einer Fehlermeldung fehl und beendet das Modul sauber
- Die Initialisierung blockiert keine anderen Module

---

### STA-REQ-02

#### Beschreibung

Solange weder WLAN noch Bluetooth verbunden sind, soll die RGB-LED dauerhaft orange leuchten.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Ohne aktive WLAN- und Bluetooth-Verbindung leuchtet die LED dauerhaft orange (R: 255, G: 80, B: 0)
- Kein Blinken

---

### STA-REQ-03

#### Beschreibung

Ist Bluetooth verbunden, aber kein WLAN, soll die RGB-LED dauerhaft blau leuchten.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Bei aktiver Bluetooth-Verbindung ohne WLAN leuchtet die LED dauerhaft blau (R: 0, G: 0, B: 255)
- Kein Blinken

---

### STA-REQ-04

#### Beschreibung

Ist WLAN verbunden, aber kein Bluetooth, soll die RGB-LED dauerhaft cyan leuchten.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Bei aktiver WLAN-Verbindung ohne Bluetooth leuchtet die LED dauerhaft cyan (R: 0, G: 255, B: 255)
- Kein Blinken

---

### STA-REQ-05

#### Beschreibung

Sind sowohl WLAN als auch Bluetooth verbunden, soll die RGB-LED dauerhaft weiß leuchten.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Bei aktiver WLAN- und Bluetooth-Verbindung leuchtet die LED dauerhaft weiß (R: 255, G: 255, B: 255)
- Kein Blinken

---

### STA-REQ-06

#### Beschreibung

Das Statusanzeige-Modul soll eine API bereitstellen, über die andere Module den aktuellen Verbindungsstatus melden können. Bei jeder Statusänderung wird die LED sofort aktualisiert. Die API ist thread-sicher.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-01 (Initialisierung)
- WIF-REQ-05 (WLAN-Status-API)

#### Abnahmekriterien

- Funktion `Status_SetConnectionState(bool wifiConnected, bool btConnected)` aktualisiert die LED sofort
- Die Funktion ist von beliebigen Threads aufrufbar (Mutex oder atomare Zustandsvariable)
- Ein Zustandswechsel wird innerhalb von 500 ms in der LED sichtbar

---

## 3. Nicht-funktionale Anforderungen

### STA-NFR-01

#### Beschreibung

Die Statusanzeige-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### STA-NFR-02

#### Beschreibung

Das Statusanzeige-Modul soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im Quellcode des Moduls

---

### STA-NFR-03

#### Beschreibung

Die Statusanzeige soll auf einen Zustandswechsel innerhalb von 500 ms reagieren.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Mittel    | Performance | Offen  |                 |

#### Abhängigkeiten

- STA-REQ-06 (Update-API)

#### Abnahmekriterien

- Zwischen Aufruf von `Status_SetConnectionState()` und sichtbarer LED-Änderung vergehen maximal 500 ms
- Gilt für alle vier Zustandsübergänge

---

## 4. Offene Punkte / Annahmen

- [ ] RGB-Werte (insbesondere für Orange und Weiß) müssen bei der Implementierung auf der Hardware auf gute Erkennbarkeit bei direktem Licht abgestimmt werden (Helligkeit ggf. reduzieren)
- [ ] Bluetooth-Verbindungsstatus-API noch nicht definiert — Abhängigkeit zu einem zukünftigen BT-Modul

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-06-17 |       | Erstellt |
| 1.1     | 2026-06-17 |       | STA-REQ-04 geändert: Nur WLAN → Cyan dauerhaft (statt blinkend) |
