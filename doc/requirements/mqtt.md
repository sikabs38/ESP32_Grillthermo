# Requirements: MQTT

## 1. Übersicht

Das System soll Messdaten (Brenner- und Kerntemperaturen, Gasfüllstand) über MQTT in einem lokalen Netzwerk bereitstellen. Der MQTT-Broker wird über die Shell konfiguriert. Die Verbindung wird beim Start automatisch aufgebaut und bei Unterbrechung selbsttätig wiederhergestellt.

---

## 2. Funktionale Anforderungen

### MQT-REQ-01

#### Beschreibung

Das System soll nach erfolgreicher WiFi-Verbindung automatisch eine Verbindung zum konfigurierten MQTT-Broker aufbauen. Die Verbindungsparameter (Broker-Hostname, Port, Passwort) werden aus dem Konfigurationsspeicher gelesen. Als Client-ID wird der konfigurierte WiFi-Hostname des Geräts verwendet.

Ist kein Broker konfiguriert (leerer Hostname), unterbleibt der Verbindungsversuch. Bei Verbindungsabbruch soll die Verbindung nach einem Warteintervall von 30 Sekunden automatisch neu aufgebaut werden.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/mqtt.c:Mqtt_Thread()`, `app/src/mqtt.c:Mqtt_DoConnect()`, `app/src/mqtt.c:Mqtt_RunEventLoop()`; Reconnect-Wartezeit 30 s via `g_ReconnectSem`; Client-ID aus WiFi-Hostname (`g_ClientId`) |

#### Abhängigkeiten

- WIF-REQ-01 (WiFi-Verbindung als Voraussetzung)
- MQT-REQ-02 (Konfiguration von Broker und Passwort)
- CFG-REQ-01 (Persistente Konfiguration)

#### Abnahmekriterien

- Nach WiFi-Verbindungsaufbau wird automatisch eine MQTT-Verbindung zum konfigurierten Broker hergestellt
- Als Client-ID wird der WiFi-Hostname des Geräts verwendet
- Ist kein Broker konfiguriert, bleibt die MQTT-Verbindung inaktiv — kein Fehler
- Bei Verbindungsabbruch wird nach 30 Sekunden automatisch ein erneuter Verbindungsversuch gestartet
- Der Verbindungsstatus ist über `mqtt status` in der Shell abfragbar (MQT-REQ-04)

---

### MQT-REQ-02

#### Beschreibung

Die Shell soll die Konfiguration des MQTT-Brokers um ein Passwort erweitern. Der Befehl lautet `mqtt set <broker>` — ohne Port-Argument. Anschließend wird das Passwort interaktiv abgefragt: Als Prompt erscheint `Passwort:`, die Eingabe wird mit `*` verdeckt (analog zu SHL-REQ-02). Eine leere Passworteingabe (nur Enter) ist zulässig und bedeutet „keine Authentifizierung". Der Port ist fest auf 1883 voreingestellt und nicht über die Shell konfigurierbar.

Das Passwort wird im Konfigurationsspeicher abgelegt (neues Feld `mqttPassword` in `Config_Data_t`). Der Broker-Hostname bleibt im bestehenden Feld `mqttBroker`.

SHL-REQ-03 (bisheriger Befehl `mqtt set <broker> <port>`) wird durch dieses Requirement ersetzt: der Port-Parameter entfällt, das Passwort tritt hinzu.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/shell.c:Shell_CmdMqttSet()` (Zeile 491); Passwort-Eingabe verdeckt mit `*`-Echo; `Config_Data_t.mqttPassword[CFG_MQTT_PASS_MAX_LEN + 1U]` in `app/src/config.h` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz und Authentifizierung)
- CFG-REQ-01 (Persistente Konfiguration)

#### Abnahmekriterien

- Der Befehl lautet `mqtt set <broker>` (kein Port-Argument)
- Nach Eingabe des Befehls erscheint der Prompt `Passwort:` und erwartet die Passworteingabe
- Jedes eingegebene Zeichen wird als `*` angezeigt (kein Klartext-Echo)
- Korrekturtaste (Backspace/DEL) löscht das zuletzt eingegebene Zeichen und das zugehörige `*`
- Eine leere Passworteingabe (nur Enter) wird akzeptiert und bedeutet anonyme Verbindung (kein Passwort)
- Ein zu langes Passwort (mehr als 64 Zeichen) wird abgewiesen; der Prompt erscheint erneut
- Broker-Hostname und Passwort werden nach erfolgreicher Eingabe dauerhaft gespeichert
- `config show` zeigt den Broker-Hostnamen an; das Passwort wird als `********` maskiert (analog zu WiFi-Passwort)
- Der Befehl ist nur im eingeloggten Zustand ausführbar (SHL-REQ-06)
- `Config_Data_t` enthält ein neues Feld `mqttPassword[CFG_MQTT_PASS_MAX_LEN + 1U]`

---

### MQT-REQ-03

#### Beschreibung

Alle gültigen Messwerte sollen nach Empfang per MQTT veröffentlicht werden. Der Topic-Aufbau lautet:

```
ESP_Grillthermo/<gruppe>/<zone>/
```

| Platzhalter | Werte | Bedeutung |
|-------------|-------|-----------|
| `<gruppe>`  | `burner`, `core`, `gas` | Messgruppe: Brenner, Kern, Gasfüllstand |
| `<zone>`    | `1`, `2`, `3`, `4` | Zonenindex (nur bei `burner` und `core`) |
| `<zone>`    | `level` | Fester Bezeichner für den Gasfüllstand |

Beispiele:
- `ESP_Grillthermo/burner/1/` → Brennertemperatur Zone 1 (°C, ganzzahlig)
- `ESP_Grillthermo/core/3/` → Kerntemperatur Zone 3 (°C, ganzzahlig)
- `ESP_Grillthermo/gas/level/` → Gasfüllstand (%, ganzzahlig, 0–100)

Der Payload ist ein ganzzahliger ASCII-Dezimalwert ohne Einheit (z.B. `"237"` oder `"74"`). Ungültige Werte (`valid = false`) werden nicht veröffentlicht.

Das Publish-Intervall beträgt 10 Sekunden: alle 10 Sekunden werden alle aktuell gültigen Messwerte veröffentlicht, unabhängig davon ob sich Werte geändert haben.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | In Bearbeitung | `app/src/mqtt.c:Mqtt_PublishThread()`, `app/src/mqtt.c:Mqtt_PublishSnapshot()`, `app/src/mqtt.c:Mqtt_PublishValue()`; Publish-Intervall noch auf Änderungsgetrieben — Umstellung auf 10-Sekunden-Takt ausstehend |

#### Abhängigkeiten

- MQT-REQ-01 (MQTT-Verbindung muss aufgebaut sein)
- TMP-REQ-03 (Temperaturdaten-Modul als Datenquelle)

#### Abnahmekriterien

- Alle gültigen Messwerte werden alle 10 Sekunden veröffentlicht
- Der Payload enthält den Messwert als ASCII-Dezimalzahl ohne Einheit und ohne Leerzeichen
- Ungültige Werte (`valid = false`) werden nicht veröffentlicht — der Topic bleibt stumm
- Topics für alle vier Brennerzonen (`burner/1/` bis `burner/4/`) werden unabhängig voneinander veröffentlicht
- Topics für alle vier Kerntemperaturzonen (`core/1/` bis `core/4/`) werden unabhängig voneinander veröffentlicht
- Der Gasfüllstand wird unter `gas/level/` veröffentlicht, Wert 0–100
- QoS-Level: 0 (at most once) — keine Bestätigung erforderlich
- Retain-Flag: gesetzt, damit neu verbundene Subscriber den letzten Wert sofort erhalten

---

### MQT-REQ-04

#### Beschreibung

Die Shell soll einen Befehl `mqtt status` bereitstellen, der den aktuellen MQTT-Verbindungsstatus anzeigt. Die Ausgabe enthält den Verbindungszustand (`Verbunden` / `Getrennt`), bei bestehender Verbindung zusätzlich den konfigurierten Broker-Hostnamen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/shell.c:Shell_CmdMqttStatus()` (Zeile 522); `app/src/mqtt.h:Mqtt_GetStatus()`, `app/src/mqtt.c:Mqtt_GetStatus()` (Zeile 214) |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz und Authentifizierung)
- MQT-REQ-01 (MQTT-Verbindungsaufbau)

#### Abnahmekriterien

- Der Befehl lautet `mqtt status` (ohne Argument)
- Ist das Gerät verbunden, erscheint:
  ```
  MQTT Status  : Verbunden
  Broker       : <hostname>
  ```
- Ist das Gerät nicht verbunden, erscheint:
  ```
  MQTT Status  : Getrennt
  Broker       : <hostname>   (nur wenn ein Broker konfiguriert ist)
  ```
- Der Befehl ist nur im eingeloggten Zustand ausführbar (SHL-REQ-06)

---

## 3. Nicht-funktionale Anforderungen

### MQT-NFR-01

#### Beschreibung

Die MQTT-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### MQT-NFR-02

#### Beschreibung

Die MQTT-Implementierung soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Umgesetzt | `app/src/mqtt.c` — statische Puffer `g_RxBuf`/`g_TxBuf` (Zeile 24–25); kein `malloc`/`free`; `zsock_freeaddrinfo()` ist Zephyr-intern und kein `free()`-Äquivalent |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im MQTT-Quellcode
- Topic- und Payload-Puffer sind als Compile-Zeit-Konstanten dimensioniert

---

## 4. Offene Punkte / Annahmen

- [ ] Benutzername für MQTT-Authentifizierung: aktuell nur Passwort vorgesehen — reicht das für den Ziel-Broker?
- [ ] QoS-Level 0 bewusst gewählt (Einfachheit, kein Zephyr-MQTT-State für Re-Delivery); bei Bedarf auf QoS 1 erweiterbar
- [ ] SHL-REQ-03 wird durch MQT-REQ-02 abgelöst: Port-Argument entfällt, Passwort tritt hinzu — SHL-REQ-03 in shell.md als ersetzt markieren
- [ ] `Config_Data_t` muss um `mqttPassword[CFG_MQTT_PASS_MAX_LEN + 1U]` erweitert werden; `CFG_MQTT_PASS_MAX_LEN = 64` analog zu WiFi

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-06-14 |       | Erstellt: MQT-REQ-01..04 (Verbindungsaufbau, Shell-Konfiguration, Topic-Format, Status) |
| 1.1     | 2026-06-15 |       | Status-Update: MQT-REQ-01, MQT-REQ-02, MQT-REQ-04, MQT-NFR-02 → Umgesetzt (Commits 2bcd354, 9269aec) |
| 1.2     | 2026-06-15 |       | MQT-REQ-03: Publish-Intervall 10 Sekunden ergänzt; Status → In Bearbeitung |
