# Requirements: WiFi

## 1. Übersicht

Das WiFi-Modul verwaltet die WLAN-Verbindung des ESP32 Grillthermo. Es lädt beim Start die gespeicherte Konfiguration, baut bei gültiger SSID automatisch eine Verbindung auf und läuft in einem dedizierten Thread. Statusmeldungen informieren über Verbindungsauf- und -abbau. Eine neue Konfiguration aus der Shell löst sofort einen neuen Verbindungsversuch aus.

---

## 2. Funktionale Anforderungen

### WIF-REQ-01

#### Beschreibung

Das WiFi-Modul soll beim Systemstart initialisiert werden. Es liest die gespeicherte Konfiguration (SSID und Passwort) aus dem Konfigurationsspeicher. Liegt eine gültige SSID vor (nicht leer), wird automatisch ein Verbindungsversuch gestartet. Ist keine SSID konfiguriert, bleibt das Modul im Leerlauf und gibt eine entsprechende Meldung aus.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- CFG-REQ-02 (Konfiguration laden)

#### Abnahmekriterien

- Nach dem Start wird die gespeicherte Konfiguration gelesen
- Ist eine SSID konfiguriert, wird automatisch ein Verbindungsversuch gestartet
- Ist keine SSID konfiguriert, erscheint die Meldung: `WiFi: Keine SSID konfiguriert.`
- Die Initialisierung erfolgt ohne Blockieren anderer Module

---

### WIF-REQ-02

#### Beschreibung

Das WiFi-Modul soll in einem eigenen Zephyr-Thread laufen. Der Thread verwaltet den Verbindungsaufbau, die Verbindungsüberwachung und Wiederverbindungsversuche.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- WIF-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Das WiFi-Modul blockiert nicht den Shell-Thread oder andere Threads
- Der Thread ist mit einer statisch allozierten Stack-Größe definiert

---

### WIF-REQ-03

#### Beschreibung

Das WiFi-Modul soll Statusmeldungen über den Verbindungszustand ausgeben. Meldungen erscheinen beim Verbindungsaufbau, bei erfolgreicher Verbindung (inkl. zugewiesener IP-Adresse), bei Verbindungsverlust und bei fehlgeschlagenem Verbindungsversuch.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Offen  |                 |

#### Abhängigkeiten

- WIF-REQ-02 (Thread)

#### Abnahmekriterien

- Beim Start eines Verbindungsversuchs erscheint: `WiFi: Verbinde mit "<ssid>" ...`
- Bei erfolgreicher Verbindung erscheint: `WiFi: Verbunden. IP: <adresse>`
- Bei Verbindungsverlust erscheint: `WiFi: Verbindung getrennt.`
- Bei fehlgeschlagenem Verbindungsversuch erscheint: `WiFi: Verbindung fehlgeschlagen.`
- Meldungen erscheinen ohne Log-Präfix auf der seriellen Schnittstelle

---

### WIF-REQ-04

#### Beschreibung

Nach dem Speichern einer neuen WLAN-Konfiguration über die Shell soll das WiFi-Modul unverzüglich einen neuen Verbindungsversuch starten. Eine laufende Verbindung wird zuvor getrennt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- WIF-REQ-02 (Thread)
- SHL-REQ-02 (WiFi-Konfiguration über Shell)

#### Abnahmekriterien

- Nach `wifi set <ssid>` + Passworteingabe startet sofort ein neuer Verbindungsversuch
- Eine zuvor bestehende Verbindung wird sauber getrennt, bevor der neue Versuch beginnt
- Der Verbindungsversuch verwendet die neu gespeicherte SSID und das neue Passwort

---

### WIF-REQ-05

#### Beschreibung

Das WiFi-Modul soll eine API bereitstellen, über die andere Module den aktuellen Verbindungsstatus abfragen können. Die API liefert den Verbindungszustand, die zuletzt verwendete SSID sowie die zugewiesene IP-Adresse als Zeichenkette.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/wifi.c:Wifi_GetStatus()`, `app/src/wifi.h:Wifi_Status_t` |

#### Abhängigkeiten

- WIF-REQ-02 (Thread)
- WIF-REQ-03 (Statusmeldungen)

#### Abnahmekriterien

- `Wifi_GetStatus()` befüllt ein `Wifi_Status_t`-Struct mit `connected`, `ssid` und `ip`
- `connected` ist `true`, sobald eine DHCP-Adresse zugewiesen wurde, und `false` nach Verbindungsverlust
- `ip` enthält die zugewiesene IPv4-Adresse als Zeichenkette oder ist leer, wenn keine Verbindung besteht
- `ssid` enthält die zuletzt verwendete SSID
- Die Funktion ist thread-sicher lesbar (nur einfache bool/char-Felder, keine Locks erforderlich)

---

### WIF-REQ-06

#### Beschreibung

Das WiFi-Modul soll nach erfolgreichem Verbindungsaufbau, sofern der Netzwerk-Stack dies unterstützt, einen konfigurierbaren Hostnamen am Netzwerk-Interface setzen. Der Hostname wird im Konfigurationsspeicher abgelegt und ist über die Shell änderbar. Ist kein Hostname konfiguriert, wird der Plattform-Standardname verwendet.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Niedrig   | Umgesetzt | `app/src/wifi.c:Wifi_Thread()` (`net_hostname_set`), `app/src/shell.c:Shell_CmdWifiHostname()`, `app/src/config.h:Config_Data_t.wifiHostname` |

#### Abhängigkeiten

- WIF-REQ-01 (Initialisierung)
- CFG-REQ-01 (Konfigurationsspeicher)
- CFG-REQ-02 (Konfiguration laden)
- CFG-REQ-03 (Konfiguration speichern)

#### Abnahmekriterien

- Der Hostname ist als eigener Konfigurationseintrag im NVS gespeichert (eigener NVS-Key)
- Nach dem Verbindungsaufbau wird der Hostname via `net_hostname_set()` am Interface gesetzt, sofern `CONFIG_NET_HOSTNAME_ENABLE=y` im Build aktiv ist
- Ist kein Hostname konfiguriert (leerer Eintrag), bleibt der Zephyr-Standardname unverändert
- Der Hostname ist über einen Shell-Befehl (`wifi hostname <name>`) setzbar und sofort persistent gespeichert
- Der Hostname wird beim nächsten Verbindungsaufbau ohne Neustart übernommen
- Maximal `NET_HOSTNAME_MAX_LEN` Zeichen (Zephyr-Konstante)

---

## 3. Nicht-funktionale Anforderungen

### WIF-NFR-01

#### Beschreibung

Die WiFi-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### WIF-NFR-02

#### Beschreibung

Das WiFi-Modul soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie    | Status | Implementierung |
|-----------|--------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Offen |                |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im WiFi-Quellcode
- Stack-Größe des WiFi-Threads ist als Compile-Zeit-Konstante definiert

---

### WIF-REQ-07

#### Beschreibung

Das WiFi-Modul soll nach einem fehlgeschlagenen Verbindungsversuch oder nach Verbindungsverlust automatisch erneut versuchen, eine Verbindung aufzubauen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/wifi.c:Wifi_Thread()`, `app/src/wifi.c:Wifi_EventCallback()` |

#### Abhängigkeiten

- WIF-REQ-02 (Thread)

#### Abnahmekriterien

- Schlägt der Verbindungsaufbau fehl (Fehlercode oder Timeout nach 60 s), wird nach 30 s automatisch ein neuer Versuch gestartet
- Eine Log-Meldung informiert über den bevorstehenden Neuversuch mit dem konfigurierten Intervall
- Bei Verbindungsverlust (Disconnect-Event) wird sofort ein neuer Verbindungsversuch eingeleitet
- Manuelle Wiederverbindung via Shell (`Wifi_Reconnect()`) ist weiterhin möglich und hat sofortige Wirkung

---

## 4. Offene Punkte / Annahmen

Keine.

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-27 |       | Erstellt |
| 1.1     | 2026-05-27 |       | WIF-REQ-05 ergänzt: Status-API `Wifi_GetStatus()` |
| 1.2     | 2026-05-28 |       | WIF-REQ-06 ergänzt: Konfigurierbarer Hostname |
| 1.3     | 2026-05-28 |       | WIF-REQ-06 umgesetzt |
| 1.4     | 2026-06-14 |       | WIF-REQ-07 ergänzt und umgesetzt: automatische Wiederverbindung |
