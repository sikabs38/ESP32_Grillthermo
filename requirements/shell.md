# Requirements: Shell

## 1. Übersicht

Die Shell stellt eine interaktive Konfigurationsschnittstelle bereit, die über USB (CDC-ACM) erreichbar ist. Sie ermöglicht die Konfiguration von WiFi, MQTT und Systemparametern ohne erneutes Flashen der Firmware.

---

## 2. Funktionale Anforderungen

### SHL-REQ-01

#### Beschreibung

Das System soll eine interaktive Shell über USB bereitstellen. Die Verbindung erfolgt über das CDC-ACM-Protokoll (virtueller serieller Port). Die Shell soll nach dem Einschalten des Geräts automatisch verfügbar sein.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/main.c:main()`, `app/boards/esp32s3_devkitc_procpu.overlay`, `app/prj.conf` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Das Gerät erscheint nach dem Einschalten als `/dev/ttyACM*` (Linux) bzw. COM-Port (Windows)
- Eine Verbindung mit einem seriellen Terminal (115200 Baud) zeigt den Shell-Prompt
- Die Shell ist innerhalb von 5 Sekunden nach dem Einschalten erreichbar

---

### SHL-REQ-02

#### Beschreibung

Die Shell soll die Konfiguration der WiFi-Zugangsdaten (SSID und Passwort) ermöglichen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/shell.c:Shell_CmdWifiSet()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)

#### Abnahmekriterien

- Der Befehl `wifi set <ssid> <password>` wird akzeptiert
- Ungültige Argumente werden mit einer Fehlermeldung quittiert
- Die gespeicherten Zugangsdaten werden beim nächsten Start verwendet

---

### SHL-REQ-03

#### Beschreibung

Die Shell soll die Konfiguration des MQTT-Brokers (Hostname und Port) ermöglichen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/shell.c:Shell_CmdMqttSet()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)

#### Abnahmekriterien

- Der Befehl `mqtt set <broker> <port>` wird akzeptiert
- Ungültige Port-Angaben (nicht numerisch, außerhalb 1–65535) werden abgewiesen
- Die gespeicherten Broker-Daten werden beim nächsten Start verwendet

---

### SHL-REQ-04

#### Beschreibung

Die Shell soll die aktuelle Systemkonfiguration lesbar ausgeben. Passwörter dürfen dabei niemals im Klartext angezeigt werden — weder bei `config show` noch als Echo bei der Eingabe.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Offen  | `app/src/shell.c:Shell_CmdConfigShow()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)

#### Abnahmekriterien

- Der Befehl `config show` gibt alle konfigurierten Parameter aus
- Passwortfelder werden ausschließlich als `********` ausgegeben
- Die Eingabe eines Passworts wird nicht als Echo auf der Shell ausgegeben
- Nicht konfigurierte Felder werden als `[nicht gesetzt]` angezeigt

---

### SHL-REQ-05

#### Beschreibung

Die Shell soll eine Hilfeübersicht aller verfügbaren Befehle bereitstellen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | Zephyr Shell (built-in `help`) |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)

#### Abnahmekriterien

- Der Befehl `help` listet alle verfügbaren Befehle mit Kurzbeschreibung auf
- Jeder Unterbefehl ist über `<befehl> help` erreichbar

---

### SHL-REQ-06

#### Beschreibung

Der Zugang zur Shell soll durch eine PIN abgesichert sein. Bei jedem Verbindungsaufbau wird die PIN abgefragt, bevor Befehle ausgeführt werden können. Der Initialisierungswert der PIN lautet `000000`. Solange die PIN nicht geändert wurde, soll bei jedem Login ein Hinweis erscheinen, die PIN zu ändern. Die PIN besteht ausschließlich aus Ziffern und hat eine Länge von 4 bis 6 Stellen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | In Bearbeitung | `app/src/shell.c:Shell_CmdLogin()`, `Shell_CmdLogout()`, `Shell_CmdConfigPin()`, `Shell_PinIsValid()`, `Shell_PinStore()`, `g_Pin`; `app/prj.conf:CONFIG_SHELL_START_OBSCURED` (NVS-Persistenz ausstehend: CFG-REQ-05) |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- CFG-REQ-04 (Persistente Speicherung der PIN, siehe `config_storage.md`)

#### Abnahmekriterien

- Nach dem Verbindungsaufbau wird vor jeder Befehlseingabe eine PIN abgefragt
- Die PIN-Eingabe wird nicht als Echo ausgegeben
- Bei korrekter PIN wird der Shell-Prompt freigegeben
- Bei falscher PIN wird der Zugang verweigert und eine Fehlermeldung ausgegeben
- Solange die PIN dem Initialisierungswert `000000` entspricht, erscheint nach dem Login der Hinweis: `Warnung: Standard-PIN aktiv. Bitte mit "config pin 000000 <neue-pin>" ändern.`
- Die PIN wird mit `config pin <alte-pin> <neue-pin>` geändert
- Eine neue PIN wird nur akzeptiert, wenn sie 4–6 Zeichen lang ist und ausschließlich Ziffern enthält
- Eine falsche alte PIN beim Änderungsversuch wird abgewiesen

---

## 3. Nicht-funktionale Anforderungen

### SHL-NFR-01

#### Beschreibung

Die Shell-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### SHL-NFR-02

#### Beschreibung

Die Shell soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie    | Status | Implementierung |
|-----------|--------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Offen |                |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` in Shell-Quellcode
- Puffergrößen sind als Compile-Zeit-Konstanten definiert

---

### SHL-NFR-03

#### Beschreibung

Eine ungültige Eingabe in der Shell darf nicht zum Absturz oder undefinierten Verhalten des Systems führen.

| Priorität | Kategorie    | Status | Implementierung |
|-----------|--------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Offen |                |

#### Abhängigkeiten

- SHL-REQ-01

#### Abnahmekriterien

- Beliebige Zeicheneingaben führen zu einer Fehlermeldung, nicht zu einem Reset
- Zu lange Eingaben werden abgeschnitten oder abgewiesen

---

## 4. Offene Punkte / Annahmen

- [x] Persistenz der Konfiguration: Zephyr NVS, siehe `config_storage.md`
- [x] Passwörter nicht im Klartext ausgeben: SHL-REQ-04 präzisiert, Verschlüsselung im Flash siehe `config_storage.md` CFG-REQ-05
- [x] Zugriffsschutz: PIN-Schutz spezifiziert in SHL-REQ-06, Timeout nicht spezifiziert

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-26 |       | Erstellt |
| 1.1     | 2026-05-26 |       | SHL-REQ-04 präzisiert: Passwortausgabe und Echo unterdrückt |
| 1.2     | 2026-05-26 |       | SHL-REQ-06 ergänzt: PIN-Schutz für Shell-Zugang |
