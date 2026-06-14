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

Die Shell soll die Konfiguration der WiFi-Zugangsdaten (SSID und Passwort) ermöglichen. Die SSID wird als Argument übergeben; das Passwort wird anschließend interaktiv abgefragt. Als Prompt erscheint `Passwort:`, die Eingabe wird mit `*` verdeckt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | In Bearbeitung | `app/src/shell.c:Shell_CmdWifiSet()`, `Shell_WifiPasswordBypass()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz und Authentifizierung)

#### Abnahmekriterien

- Der Befehl lautet `wifi set <ssid>` (ohne Passwort-Argument)
- Nach Eingabe des Befehls erscheint der Prompt `Passwort:` und erwartet die Passworteingabe
- Jedes eingegebene Zeichen wird als `*` angezeigt (kein Klartext-Echo)
- Korrekturtaste (Backspace/DEL) löscht das zuletzt eingegebene Zeichen und das zugehörige `*`
- Eine leere Passworteingabe (nur Enter) wird abgewiesen; der Prompt erscheint erneut
- Ein zu langes Passwort (mehr als 64 Zeichen) wird abgewiesen; der Prompt erscheint erneut
- Nach erfolgreicher Eingabe werden SSID und Passwort gespeichert und eine Bestätigung ausgegeben
- Ungültige Argumente (fehlende oder zu lange SSID) werden mit einer Fehlermeldung quittiert, ohne den Passwort-Prompt zu starten
- Die gespeicherten Zugangsdaten werden beim nächsten Start verwendet

---

### SHL-REQ-03

> **Abgelöst durch MQT-REQ-02** (siehe `doc/requirements/mqtt.md`). Der Befehl `mqtt set <broker> <port>` entfällt; an seine Stelle tritt `mqtt set <broker>` mit interaktivem Passwort-Prompt und festem Port 1883.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Abgelöst | — (ersetzt durch MQT-REQ-02) |

---

### SHL-REQ-04

#### Beschreibung

Die Shell soll die aktuelle Systemkonfiguration lesbar ausgeben. Passwörter dürfen dabei niemals im Klartext angezeigt werden — weder bei `config show` noch als Echo bei der Eingabe.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/shell.c:Shell_CmdConfigShow()` |

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

Der Zugang zur Shell soll durch eine PIN abgesichert sein. Nach dem Verbindungsaufbau erscheint der Prompt `login:` und das System wartet ausschließlich auf die PIN-Eingabe — ohne Befehlsschlüsselwort. Jedes eingegebene Zeichen wird als `*` angezeigt. Bei korrekter PIN wechselt der Prompt zu `Grillthermo:` und alle Befehle stehen zur Verfügung. Der Initialisierungswert der PIN lautet `000000`. Solange die PIN nicht geändert wurde, soll bei jedem Login ein Hinweis erscheinen, die PIN zu ändern. Die PIN besteht ausschließlich aus Ziffern und hat eine Länge von 4 bis 6 Stellen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | In Bearbeitung | `app/src/shell.c:Shell_LoginBypass()`, `Shell_LoginSetup()`, `Shell_CmdLogout()`, `Shell_CmdConfigPin()`, `Shell_PinIsValid()`, `Shell_PinStore()`; `app/prj.conf:CONFIG_SHELL_START_OBSCURED` (NVS-Persistenz ausstehend: CFG-REQ-05) |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- CFG-REQ-04 (Persistente Speicherung der PIN, siehe `config_storage.md`)

#### Abnahmekriterien

- Nach dem Verbindungsaufbau erscheint der Prompt `login:` und erwartet ausschließlich die PIN (kein Befehl)
- Jedes eingegebene Zeichen der PIN wird als `*` angezeigt (kein Klartext-Echo)
- Korrekturtaste (Backspace/DEL) löscht das zuletzt eingegebene Zeichen und das zugehörige `*`
- Bei korrekter PIN wechselt der Prompt zu `Grillthermo:` und alle Befehle sind verfügbar
- Bei falscher PIN wird eine Fehlermeldung ausgegeben und der `login:`-Prompt erneut angezeigt
- Solange die PIN dem Initialisierungswert `000000` entspricht, erscheint nach dem Login der Hinweis: `Warnung: Standard-PIN aktiv. Bitte mit "config pin 000000 <neue-pin>" ändern.`
- Der Befehl `logout` setzt den Prompt auf `login:` zurück und erfordert erneute PIN-Eingabe
- Die PIN wird mit `config pin <alte-pin> <neue-pin>` geändert
- Eine neue PIN wird nur akzeptiert, wenn sie 4–6 Zeichen lang ist und ausschließlich Ziffern enthält
- Eine falsche alte PIN beim Änderungsversuch wird abgewiesen

---

### SHL-REQ-07

#### Beschreibung

Die Bootmeldung soll in folgenden Situationen über die serielle Schnittstelle ausgegeben werden:

1. **Verbindungsaufbau** — sobald ein Terminal die serielle Schnittstelle öffnet (DTR-Signal)
2. **Nach Logout** — unmittelbar nach der Abmeldung, vor dem erneuten `login:`-Prompt

Die Meldung enthält einen ASCII-Rahmen mit Produktname und -bezeichnung sowie die Systeminformationen Zephyr-Version, CPU-Bezeichnung und Taktfrequenz. Das Format ist fest vorgegeben:

```
=========================
=== ESP32 Grillthermo ===
=== Temperaturmonitor ===
=========================
Zephyr OS: <Version>
CPU:       <CPU-Bezeichnung>
Takt:      <MHz> MHz
-------------------------
```

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Niedrig   | Umgesetzt | `app/src/shell.c:Shell_PrintBanner()`, `Shell_PrintBannerShell()`, `Shell_DtrWork()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (Logout-Befehl)

#### Abnahmekriterien

- Die Bootmeldung erscheint beim Öffnen der seriellen Verbindung vor dem `login:`-Prompt
- Die Bootmeldung erscheint nach jedem `logout`-Befehl vor dem erneuten `login:`-Prompt
- Der ASCII-Rahmen ist exakt 25 Zeichen breit
- Die Zeile `Zephyr OS:` zeigt die zur Build-Zeit ermittelte Kernel-Version (`KERNEL_VERSION_STRING`)
- Die Zeile `CPU:` zeigt die korrekte CPU-Bezeichnung des verbauten Prozessors
- Die Zeile `Takt:` zeigt die konfigurierte CPU-Taktfrequenz in MHz (`CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1 000 000`)
- Die Ausgabe erfolgt ohne Log-Präfix

---

### SHL-REQ-08

#### Beschreibung

Die Shell soll einen Befehl `bootloader` bereitstellen, der das System in den ROM-Bootloader des ESP32-S3 versetzt (Download-Modus). Damit kann eine neue Firmware ohne manuelle Betätigung des BOOT-Tasters aufgespielt werden. Da der Befehl einen kritischen Eingriff darstellt, wird nach Eingabe des Befehls die aktuelle PIN interaktiv abgefragt. Als Prompt erscheint `Pin:`, die Eingabe wird mit `*` verdeckt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | In Bearbeitung | `app/src/shell.c:Shell_CmdBootloader()`, `Shell_BootloaderPinBypass()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz und Authentifizierung)

#### Abnahmekriterien

- Der Befehl lautet `bootloader` (ohne Argument)
- Nach Eingabe des Befehls erscheint der Prompt `Pin:` und erwartet die PIN-Eingabe
- Jedes eingegebene Zeichen der PIN wird als `*` angezeigt (kein Klartext-Echo)
- Korrekturtaste (Backspace/DEL) löscht das zuletzt eingegebene Zeichen und das zugehörige `*`
- Bei falscher PIN wird eine Fehlermeldung ausgegeben, der Bypass wird beendet und die Shell ist weiterhin normal bedienbar; das System bleibt in Betrieb
- Bei korrekter PIN gibt das System eine Bestätigungsmeldung aus und wechselt unmittelbar in den ROM-Bootloader (Download-Modus)
- Im Bootloader-Modus ist das Gerät über `esptool` flashbar, ohne den BOOT-Taster zu betätigen
- Der Befehl ist nur im eingeloggten Zustand ausführbar (SHL-REQ-06)

---

### SHL-REQ-09

#### Beschreibung

Die Shell soll einen Befehl `wifi status` bereitstellen, der den aktuellen WiFi-Verbindungsstatus anzeigt. Die Ausgabe enthält den Verbindungszustand (`Verbunden` / `Getrennt`), bei bestehender Verbindung zusätzlich die konfigurierte SSID und die zugewiesene IP-Adresse.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/shell.c:Shell_CmdWifiStatus()` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz und Authentifizierung)
- WIF-REQ-05 (WiFi-Status-API)

#### Abnahmekriterien

- Der Befehl lautet `wifi status` (ohne Argument)
- Ist das Gerät verbunden, erscheint:
  ```
  WiFi Status  : Verbunden
  SSID         : <ssid>
  IP-Adresse   : <ip>
  ```
- Ist das Gerät nicht verbunden, erscheint:
  ```
  WiFi Status  : Getrennt
  SSID         : <ssid>   (nur wenn eine SSID konfiguriert ist)
  ```
- Der Befehl ist nur im eingeloggten Zustand ausführbar (SHL-REQ-06)

---

### SHL-REQ-10

#### Beschreibung

Die Bootmeldung (SHL-REQ-07) soll eine zusätzliche Zeile mit der Softwareversionsnummer enthalten. Die Versionsnummer wird zur Build-Zeit aus `app/src/version.h` (`ESP32_GRILLTHERMO_VERSION_STRING`) eingebunden. Das erweiterte Format lautet:

```
=========================
=== ESP32 Grillthermo ===
=== Temperaturmonitor ===
=========================
Zephyr OS: <Version>
CPU:       <CPU-Bezeichnung>
Takt:      <MHz> MHz
Version:   X.Y
-------------------------
```

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Niedrig   | Umgesetzt | `app/src/shell.c:Shell_PrintBanner()`, `Shell_PrintBannerShell()` |

#### Abhängigkeiten

- SHL-REQ-07 (Bootmeldung mit ASCII-Rahmen und Systeminformationen)

#### Abnahmekriterien

- Die Zeile `Version:   X.Y` erscheint in der Bootmeldung unmittelbar nach der Takt-Zeile
- Der angezeigte Wert entspricht `ESP32_GRILLTHERMO_VERSION_STRING` aus `version.h`
- Das Format `Version:   ` (drei Leerzeichen nach dem Doppelpunkt) ist konsistent mit den übrigen Zeilen des Banners

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

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
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
| 1.3     | 2026-05-27 |       | SHL-REQ-06 überarbeitet: Bypass-Login mit `login:`-Prompt und `Grillthermo:`-Prompt nach Anmeldung |
| 1.4     | 2026-05-27 |       | SHL-REQ-07 ergänzt: Bootmeldung mit ASCII-Rahmen und Systeminformationen |
| 1.5     | 2026-05-27 |       | Skill-Alignment: SHL-REQ-04 Status „Offen" → „Umgesetzt", MISRA C → MISRA C 2023 |
| 1.6     | 2026-05-27 |       | SHL-REQ-07 erweitert: Bootmeldung bei Verbindungsaufbau (DTR) und nach Logout |
| 1.7     | 2026-05-27 |       | SHL-REQ-08 ergänzt: `bootloader`-Befehl mit PIN-Bestätigung für Download-Modus |
| 1.8     | 2026-05-27 |       | SHL-REQ-07 Status auf „Umgesetzt" gesetzt; SHL-REQ-08 überarbeitet: interaktiver `Pin:`-Prompt statt Argument, Status auf „In Bearbeitung" |
| 1.9     | 2026-05-27 |       | SHL-REQ-02 überarbeitet: Passwort wird interaktiv mit `Passwort:`-Prompt und `*`-Verdeckung abgefragt statt als Argument übergeben |
| 2.0     | 2026-05-27 |       | SHL-REQ-09 ergänzt: `wifi status`-Befehl zur Anzeige des WiFi-Verbindungsstatus |
| 2.1     | 2026-06-14 |       | SHL-REQ-10 ergänzt: Softwareversionsnummer in der Bootmeldung |
| 2.2     | 2026-06-14 |       | SHL-REQ-03 als abgelöst markiert: ersetzt durch MQT-REQ-02 (mqtt.md) |
