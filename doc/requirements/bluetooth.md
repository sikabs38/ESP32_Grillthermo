# Requirements: Bluetooth-Kommunikation mit Otto Wilde G32

## 1. Übersicht

Das Bluetooth-Modul verbindet das ESP32 Grillthermo mit dem Otto Wilde G32 Gasgrill über Bluetooth Low Energy (BLE). Der ESP32-S3 arbeitet als BLE-Central und verbindet sich mit dem Grill, der als Peripheral seine Messwerte über eine GATT-Notification-Charakteristik bereitstellt.

Aus dem Grill werden ausgelesen:

- vier Brennertemperaturen (Garraumtemperaturen, °C)
- vier Kerntemperaturen aus den Fleischthermometer-Sonden (°C)
- der Füllstand der Gasflasche (%)

Die ausgelesenen Werte werden über die Funktionen `Temp_Set()` und `Temp_SetGas()` (siehe TMP-REQ-01..03, DSP-REQ-06) in die zentrale Datenstruktur `g_TempData` geschrieben.

Die Kopplung mit dem Grill wird interaktiv über die Shell ausgelöst. Da der G32 kein klassisches SMP-Pairing erzwingt (siehe Abschnitt 2), besteht die „Kopplung" funktional aus der vom Nutzer bestätigten Auswahl der Grill-MAC-Adresse. Diese MAC wird persistent im Konfigurationsspeicher abgelegt, sodass nach einem Neustart automatisch eine Verbindung zum gleichen Grill aufgebaut wird, ohne dass der Nutzer eingreifen muss.

---

## 2. Bekanntes Protokoll (Reverse-Engineering-Stand)

Otto Wilde dokumentiert das BLE-Protokoll des G32 nicht offiziell. Die folgenden Eckdaten sind aus mehreren Community-Projekten konsolidiert (siehe Abschnitt 6, Quellen) und gelten als gesichert für die Firmware ab Version 1.0.5; jüngere Firmware-Stände (getestet bis 1.4.5) bleiben kompatibel.

### 2.1 GATT-Eckdaten

| Eigenschaft                        | Wert                                       |
|------------------------------------|--------------------------------------------|
| Advertising-Name                   | `OWG-G32C`                                 |
| Service-UUID                       | `dc0f41ea-b6ae-46a8-a19e-1a3bf4342bcb`     |
| Charakteristik TX (Notify)         | `dc0f41e2-b6ae-46a8-a19e-1a3bf4342bcb`     |
| Charakteristik RX (optional)       | `dc0f41e1-b6ae-46a8-a19e-1a3bf4342bcb`     |
| Notify-Payload                     | 28 Byte Nutzdaten + 3 Byte ATT-Overhead    |
| Max. parallele BLE-Clients am Grill| 3 (Otto-Wilde-App, ESP32 Grillthermo, weitere) |
| SMP-Pairing                        | nicht erforderlich (offene GATT-Charakteristik) |

### 2.2 Byte-Layout der Notify-Payload

Hinweis: Die Indizes stammen aus der Cloud-TCP-Variante des Protokolls (51 Byte inkl. 6 Byte Header `a33a` + Meta). Laut `fschwarz86/g32` ist das BLE-Frame layout-identisch zum Cloud-Frame, jedoch ohne den 6-Byte-Header. Bis zur Verifikation per Sniffer (siehe Abschnitt 5) sind die BLE-Offsets in der Implementierung als Konstanten zu kapseln (`BLE_OFFSET_HEADER = 0` für BLE, `= 6` für TCP).

| Daten                         | Byte (TCP) | Byte (BLE-erwartet) | Formel                                |
|-------------------------------|------------|---------------------|---------------------------------------|
| Header (Magic)                | 0–1        | —                   | `a33a` (TCP-Layout)                   |
| Brennerzone 1                 | 6–7        | 0–1                 | `x[hi]*10 + x[lo]/10` → °C            |
| Brennerzone 2                 | 8–9        | 2–3                 | analog                                |
| Brennerzone 3                 | 10–11      | 4–5                 | analog                                |
| Brennerzone 4                 | 12–13      | 6–7                 | analog                                |
| Fleischsonde 1 (Kerntemp.)    | 14–15      | 8–9                 | analog                                |
| Fleischsonde 2                | 16–17      | 10–11               | analog                                |
| Fleischsonde 3                | 18–19      | 12–13               | analog                                |
| Fleischsonde 4                | 20–21      | 14–15               | analog                                |
| Gasflasche (Gewicht in Gramm) | 22–23      | 16–17               | `x[hi]*256 + x[lo]`                   |
| Deckel-/Licht-Status          | 24–25      | 18–19               | Bitfeld                               |
| Gasflasche (Füllstand %)      | 31         | 25                  | direkter Byte-Wert 0..100             |

### 2.3 Sentinel-Werte

- Eine Temperatur ≥ **1500 °C** kennzeichnet eine nicht angeschlossene oder defekte Sonde — der Wert ist als ungültig zu behandeln.
- Ein Gas-Füllstand von 0 % zusammen mit Gewicht 0 g bedeutet typischerweise „kein Gas-Sensor angeschlossen" (alte Firmware oder fehlender GasBuddy) — als `valid = false` behandeln.

### 2.4 Kopplungs-/Verbindungsverhalten

- Der G32 erfordert kein SMP-Pairing; ein Connect plus Subscribe auf die Notify-Charakteristik genügt.
- Der Grill akzeptiert bis zu drei parallele GATT-Verbindungen. Eine zusätzliche Verbindung durch das ESP32 Grillthermo stört die Otto-Wilde-App nicht.
- Auswahl des Grills erfolgt anhand des Advertising-Namens `OWG-G32C` oder per Service-UUID-Filter.

---

## 3. Funktionale Anforderungen

### BLE-REQ-01

#### Beschreibung

Das Bluetooth-Modul soll beim Systemstart den Zephyr-Bluetooth-Stack als BLE-Central initialisieren. Tritt während der Initialisierung ein Fehler auf, wird dies protokolliert und das Modul bleibt im Leerlauf, ohne das übrige System zu beeinflussen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c:Bt_Thread()` (Zeile 650): `bt_enable(NULL)`; Fehler via `LOG_ERR`; übrige Module unberührt da eigenem Thread |

#### Abhängigkeiten

- CFG-REQ-02 (Konfiguration laden — für gespeicherte Grill-MAC)

#### Abnahmekriterien

- `bt_enable()` wird beim Systemstart aufgerufen
- Die Rolle Central ist im Build aktiviert (`CONFIG_BT=y`, `CONFIG_BT_CENTRAL=y`, `CONFIG_BT_GATT_CLIENT=y`)
- Ein Fehler bei `bt_enable()` wird als `LOG_ERR` ausgegeben; das übrige System startet trotzdem vollständig durch
- Die Initialisierung blockiert keinen anderen Thread (Shell, WiFi, Webserver)

---

### BLE-REQ-02

#### Beschreibung

Das Bluetooth-Modul soll in einem eigenen Zephyr-Thread laufen. Der Thread verwaltet Scan, Verbindungsaufbau, GATT-Discovery, Datenempfang sowie Wiederverbindungsversuche bei Verbindungsverlust.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c`: `K_THREAD_DEFINE(bluetooth_thread, BT_THREAD_STACK_SIZE, Bt_Thread, …)` (Zeile 701); Stack 4096 Byte, Priorität 7 |

#### Abhängigkeiten

- BLE-REQ-01 (Initialisierung)

#### Abnahmekriterien

- Das Bluetooth-Modul läuft in einem eigenen Thread mit statisch allozierter Stack-Größe
- Es blockiert weder den Shell-Thread noch den Webserver- oder WiFi-Thread
- Die Thread-Priorität ist niedriger als die der Shell

---

### BLE-REQ-03

#### Beschreibung

Das Bluetooth-Modul soll über die Shell folgende Befehle bereitstellen, um die Kopplung mit dem Grill durchzuführen:

| Befehl                | Beschreibung                                                                                    |
|-----------------------|-------------------------------------------------------------------------------------------------|
| `bt scan`             | Startet einen zeitlich begrenzten Scan (max. 30 s) und listet Grill-Kandidaten auf — gefiltert auf den Advertising-Namen `OWG-G32C` (siehe Abschnitt 2.1) bzw. die G32-Service-UUID. Jeder Treffer wird mit MAC-Adresse, Name und RSSI ausgegeben. |
| `bt scan all`         | Wie `bt scan`, jedoch ohne Filter — listet alle erreichbaren BLE-Peripheriegeräte (zur Diagnose) |
| `bt scan stop`        | Bricht einen laufenden Scan vorzeitig ab                                                        |
| `bt pair <mac>`       | Übernimmt die MAC-Adresse (Format `AA:BB:CC:DD:EE:FF`) als Grill-MAC, speichert sie persistent und löst sofort einen Verbindungsversuch aus |
| `bt unpair`           | Entfernt die gespeicherte Grill-MAC und trennt eine bestehende Verbindung                       |
| `bt status`           | Zeigt den aktuellen Verbindungs- und Kopplungsstatus an (siehe BLE-REQ-10)                      |

Alle Befehle sind nur im eingeloggten Zustand ausführbar (SHL-REQ-06).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/shell.c:Shell_CmdBtScan()` (Zeile 1060), `Shell_CmdBtPair()` (Zeile 1098), `Shell_CmdBtUnpair()` (Zeile 1130), `Shell_CmdBtStatus()` (Zeile 1159); G32-Filter via `Bluetooth_ScanStart(true/false)` |

#### Abhängigkeiten

- SHL-REQ-01 (Shell über USB)
- SHL-REQ-06 (PIN-Schutz)
- BLE-REQ-01 (Initialisierung)
- BLE-REQ-07 (Persistente Grill-MAC)

#### Abnahmekriterien

- `bt scan` listet ausschließlich Advertising-Pakete mit Name `OWG-G32C` oder Service-UUID `dc0f41ea-b6ae-46a8-a19e-1a3bf4342bcb`; ein Treffer pro Zeile mit MAC, Name und RSSI
- `bt scan all` deaktiviert den Filter und listet alle empfangenen Advertising-Pakete
- Doppelte Treffer derselben MAC werden während eines Scans nur einmal ausgegeben (Aktualisierung des RSSI ist erlaubt)
- Ein Scan endet automatisch nach maximal 30 s mit der Meldung `Scan beendet.`
- `bt scan stop` beendet einen laufenden Scan sofort und ist ohne Wirkung, wenn kein Scan läuft
- `bt pair <mac>` weist ungültige MAC-Adressen (Format, Länge) mit einer Fehlermeldung ab, ohne einen Verbindungsversuch zu starten
- `bt pair <mac>` speichert die MAC persistent (BLE-REQ-07) und startet sofort einen Verbindungsversuch
- Erfolgreicher Verbindungsaufbau wird mit `BT: Verbunden mit <mac>.` quittiert, fehlgeschlagener mit `BT: Verbindung fehlgeschlagen: <grund>.`
- `bt unpair` löscht die gespeicherte Grill-MAC und trennt eine bestehende Verbindung; ist keine MAC gespeichert, wird `Keine Kopplung gespeichert.` ausgegeben
- Alle Befehle sind nur nach erfolgreichem Login ausführbar; ohne Login wird ein Befehlsfehler gemeldet

---

### BLE-REQ-04

#### Beschreibung

Das Bluetooth-Modul soll die vier Brennertemperaturen (Garraumtemperaturen) des Grills aus den Notify-Paketen der TX-Charakteristik (Abschnitt 2.1) parsen und über `Temp_Set(TEMP_GROUP_BURNER, zone, value, valid)` in `g_TempData.burner[0..3]` ablegen. Die Werte sind ganzzahlig in °C, gerundet. Die Aktualisierungsrate folgt der Notify-Rate des Grills (siehe Abschnitt 5) und beträgt im typischen Betrieb mindestens 1 Hz.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c:Bt_ParseAndDispatch()` (Zeile 282–315): Dekodierung via `Bt_DecodeTemp()`, Sentinel ≥ 1500 via `Bt_IsTempSentinel()`, Plausibilitätsprüfung 0..500 °C; Invalidierung bei Disconnect in `Bt_InvalidateAllValues()` |

#### Abhängigkeiten

- BLE-REQ-02 (Thread)
- BLE-REQ-08 (Automatischer Verbindungsaufbau)
- TMP-REQ-01 (Datenstruktur)
- TMP-REQ-03 (`Temp_Set()`)

#### Abnahmekriterien

- Die vier Brennertemperaturen werden aus den im Abschnitt 2.2 dokumentierten Byte-Indizes nach der Formel `x[hi]*10 + x[lo]/10` extrahiert und gerundet als `int16_t °C` abgelegt
- Bei jedem Notify-Paket wird der jeweilige Wert über `Temp_Set(TEMP_GROUP_BURNER, i, value, true)` (i = 0..3) gesetzt
- Ein Rohwert ≥ 1500 °C (Sentinel, Abschnitt 2.3) wird als ungültig interpretiert und über `Temp_Set(TEMP_GROUP_BURNER, i, 0, false)` markiert
- Bei Verbindungsverlust werden alle vier Brennertemperaturen über `Temp_Set(TEMP_GROUP_BURNER, i, 0, false)` als ungültig markiert
- Werte außerhalb des plausiblen Bereichs (< 0 °C oder > 500 °C, abzüglich Sentinel) werden verworfen und nicht in `g_TempData` geschrieben
- Auf der Anzeige (DSP-REQ-02) erscheinen die aktuellen Werte

---

### BLE-REQ-05

#### Beschreibung

Das Bluetooth-Modul soll die vier Kerntemperaturen aus den am Grill angeschlossenen Fleisch­thermometer-Sonden aus den Notify-Paketen parsen und über `Temp_Set(TEMP_GROUP_CORE, zone, value, valid)` in `g_TempData.core[0..3]` ablegen. Nicht angeschlossene Sonden liefern den Sentinel-Wert ≥ 1500 °C (Abschnitt 2.3) und werden als `valid = false` markiert. Die Werte sind ganzzahlig in °C.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c:Bt_ParseAndDispatch()` (Zeile 289–326): Sentinel → `Temp_Set(..., false)`, Plausibilitätsprüfung 0..150 °C; Invalidierung in `Bt_InvalidateAllValues()` |

#### Abhängigkeiten

- BLE-REQ-02 (Thread)
- BLE-REQ-08 (Automatischer Verbindungsaufbau)
- TMP-REQ-01 (Datenstruktur)
- TMP-REQ-03 (`Temp_Set()`)

#### Abnahmekriterien

- Die vier Kerntemperaturen werden aus den im Abschnitt 2.2 dokumentierten Byte-Indizes nach der Formel `x[hi]*10 + x[lo]/10` extrahiert
- Eine angeschlossene Sonde (Rohwert < 1500 °C) wird über `Temp_Set(TEMP_GROUP_CORE, i, value, true)` gesetzt
- Eine nicht angeschlossene Sonde (Rohwert ≥ 1500 °C) wird über `Temp_Set(TEMP_GROUP_CORE, i, 0, false)` als ungültig markiert
- Bei Verbindungsverlust werden alle vier Kerntemperaturen als ungültig markiert
- Werte außerhalb des plausiblen Bereichs (< 0 °C oder > 150 °C, abzüglich Sentinel) werden verworfen
- Auf der Anzeige (DSP-REQ-03) erscheinen die aktuellen Werte bzw. `--` bei nicht angeschlossener Sonde

---

### BLE-REQ-06

#### Beschreibung

Das Bluetooth-Modul soll den Füllstand der Gasflasche aus den Notify-Paketen parsen und über `Temp_SetGas(percent, valid)` in `g_TempData.gas` ablegen. Der Wert ist eine Ganzzahl in Prozent (0..100). Liefert der Grill keinen Gas-Messwert (alte Firmware ohne GasBuddy-Sensor oder Sensor nicht angeschlossen, siehe Abschnitt 2.3), wird der Wert als ungültig markiert.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c:Bt_ParseAndDispatch()` (Zeile 296–344): Gramm + Prozent-Byte, Plausibilitätsprüfung (Percent=0 ∧ Grams=0 → invalid), Begrenzung auf 0..100, `Temp_SetGas()` |

#### Abhängigkeiten

- BLE-REQ-02 (Thread)
- BLE-REQ-08 (Automatischer Verbindungsaufbau)
- TMP-REQ-01 (Datenstruktur)
- DSP-REQ-06 (Gasflaschen-Anzeige)

#### Abnahmekriterien

- Der Gas-Füllstand wird primär aus dem Prozent-Byte am Index aus Abschnitt 2.2 gelesen (Firmware ≥ 1.0.5)
- Zur Plausibilisierung wird zusätzlich das Gasflaschen-Gewicht aus den Bytes am Index aus Abschnitt 2.2 gelesen (`x[hi]*256 + x[lo]`, Einheit g) — Gewicht 0 g zusammen mit Prozent 0 % wird als „kein Sensor" interpretiert
- Werte 0..100 werden über `Temp_SetGas(percent, true)` gesetzt
- Werte außerhalb 0..100 werden auf den nächstliegenden Wert begrenzt (0 bzw. 100) und gesetzt
- Liefert der Grill keinen Gas-Messwert (siehe Plausibilitätsprüfung oben), wird über `Temp_SetGas(0, false)` der Eintrag als ungültig markiert
- Bei Verbindungsverlust wird der Gas-Eintrag als ungültig markiert
- Auf der Anzeige (DSP-REQ-06) erscheint der aktuelle Wert bzw. `--`

---

### BLE-REQ-07

#### Beschreibung

Nach erfolgreicher Auswahl eines Grills über `bt pair <mac>` (BLE-REQ-03) soll die MAC-Adresse des Grills persistent im Konfigurationsspeicher abgelegt werden. Da der G32 kein SMP-Pairing erzwingt (Abschnitt 2.4), ist keine Speicherung von Bond-Schlüsseln (LTK, IRK) erforderlich; die Persistenz der MAC genügt, um beim nächsten Start automatisch die Verbindung wiederherzustellen (BLE-REQ-08).

Die MAC-Adresse wird als zusätzliches Feld in `Config_Data_t` aufgenommen und folgt damit dem etablierten 3-Generationen-Speicherschema (CFG-REQ-06).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.h:Config_Data_t.grillMac`; `app/src/shell.c:Shell_CmdBtPair()` (Zeile 1114–1117): speichert via `Config_Save()`; `Shell_CmdBtUnpair()` (Zeile 1146–1147): löscht und speichert |

#### Abhängigkeiten

- BLE-REQ-01 (Initialisierung)
- BLE-REQ-03 (Shell-Pairing)
- CFG-REQ-01 (Persistente Konfiguration)
- CFG-REQ-04 (Parameterliste — Erweiterung um Grill-MAC)
- CFG-REQ-06 (3-Generationen-Speicher)

#### Abnahmekriterien

- `Config_Data_t` enthält ein neues Feld `grillMac` (6 Byte, oder 17 Zeichen + Nullterminierung als Zeichenkette)
- `bt pair <mac>` schreibt die MAC via `Config_Save()` (CFG-REQ-01) in den nichtflüchtigen Speicher; die Bestätigungsausgabe erfolgt erst nach Abschluss des Schreibvorgangs
- `bt unpair` setzt das Feld auf den Leer-Wert (Nullbytes) und speichert via `Config_Save()`
- Nach einem Neustart ist die gespeicherte MAC weiterhin vorhanden und über `config show` lesbar
- `config reset` (CFG-REQ-03) löscht auch die Grill-MAC
- Eine spätere optionale Aktivierung von SMP-Bonds (falls der G32 in zukünftigen Firmware-Ständen Pairing verlangt) erfolgt über `CONFIG_BT_SETTINGS=y`; bis dahin bleibt dieses Kconfig-Symbol deaktiviert

---

### BLE-REQ-08

#### Beschreibung

Beim Systemstart soll das Bluetooth-Modul automatisch eine Verbindung mit dem Grill aufbauen, dessen MAC im Konfigurationsspeicher abgelegt ist (BLE-REQ-07). Bei Verbindungsverlust soll das Modul selbsttätig periodische Wiederverbindungsversuche unternehmen. Ist keine MAC gespeichert, bleibt das Modul im Leerlauf, bis über `bt pair` eine Kopplung erfolgt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/bluetooth.c:Bt_Thread()` (Zeile 669–698): liest `grillMac`, ruft `Bt_DoConnect()`; `g_ReconnectSem` für Reconnect; Intervall 10 s; wartet auf WiFi via `Wifi_WaitConnected()` (Zeile 662); `Bt_DisconnectedCb()` gibt Semaphor frei |

#### Abhängigkeiten

- BLE-REQ-01 (Initialisierung)
- BLE-REQ-02 (Thread)
- BLE-REQ-07 (Persistente Grill-MAC)

#### Abnahmekriterien

- Nach dem Start liest das Modul die gespeicherte Grill-MAC aus `Config_Data_t`; bei nicht-leerem Wert startet es einen Verbindungsversuch über `bt_conn_le_create()` mit dieser MAC
- Ist keine MAC gespeichert, erscheint die Meldung: `BT: Keine Kopplung gespeichert.`
- Bei Verbindungsverlust startet das Modul nach maximal 10 s einen Wiederverbindungsversuch
- Wiederverbindungsversuche laufen periodisch mit einem Intervall von 10 s, bis eine Verbindung zustande kommt
- Beim Start eines Wiederverbindungsversuchs werden alle Burner-, Core- und Gas-Einträge in `g_TempData` als ungültig markiert (BLE-REQ-04..06)
- Ein erfolgreicher Wiederverbindungsversuch erfolgt ohne erneute Nutzerinteraktion (kein erneutes Pairing, keine SMP-Schritte)

---

### BLE-REQ-09

#### Beschreibung

Das Bluetooth-Modul soll Statusmeldungen über den Verbindungs- und Kopplungszustand auf der seriellen Schnittstelle ausgeben. Meldungen erscheinen beim Scan-Start/-Ende, beim Verbindungsaufbau, bei erfolgreicher Verbindung und bei Verbindungsverlust.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/bluetooth.c`: `printk("BT: Scan gestartet...")` (Zeile 736), `"BT: Scan beendet."` (Zeile 755), `"BT: Verbinde mit %s..."` (Zeile 615), `"BT: Verbunden mit %s."` (Zeile 426), `"BT: Verbindung getrennt..."` (Zeile 481), `"BT: Verbindung fehlgeschlagen..."` (Zeile 455) |

#### Abhängigkeiten

- BLE-REQ-02 (Thread)

#### Abnahmekriterien

- Beim Start eines Scans erscheint: `BT: Scan gestartet (max. 30 s) ...`
- Beim Ende eines Scans erscheint: `BT: Scan beendet.`
- Beim Start eines Verbindungsversuchs erscheint: `BT: Verbinde mit <mac> ...`
- Bei erfolgreicher Verbindung erscheint: `BT: Verbunden mit <mac>.`
- Bei Verbindungsverlust erscheint: `BT: Verbindung getrennt.`
- Bei fehlgeschlagenem Verbindungsversuch erscheint: `BT: Verbindung fehlgeschlagen: <grund>.`
- Meldungen erscheinen ohne Log-Präfix auf der seriellen Schnittstelle

---

### BLE-REQ-10

#### Beschreibung

Das Bluetooth-Modul soll eine API bereitstellen, über die andere Module (Shell, LED, Webserver) den aktuellen Bluetooth-Status abfragen können. Die API liefert den Verbindungszustand, die MAC-Adresse des gekoppelten Grills (falls vorhanden) und einen Zeitstempel des letzten empfangenen Notify-Pakets.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/bluetooth.h:Bluetooth_Status_t` (`connected`, `paired`, `peerMac`, `lastPacketUptimeMs`); `app/src/bluetooth.c:Bluetooth_GetStatus()` (Zeile 786), thread-sicher via `g_StateMutex`; `Shell_CmdBtStatus()` in `shell.c` |

#### Abhängigkeiten

- BLE-REQ-02 (Thread)
- BLE-REQ-07 (Persistente Grill-MAC)
- BLE-REQ-08 (Automatischer Verbindungsaufbau)

#### Abnahmekriterien

- `Bluetooth_GetStatus()` befüllt ein `Bluetooth_Status_t`-Struct mit `connected` (bool), `paired` (bool), `peerMac` (Zeichenkette) und `lastPacketUptimeMs` (uint32_t)
- `connected = true`, solange eine GATT-Verbindung mit aktiver Notify-Subscription besteht; `false` nach Verbindungsverlust
- `paired = true`, sobald eine nicht-leere Grill-MAC im Konfigurationsspeicher vorliegt; `false` nach `bt unpair`
- `peerMac` enthält die MAC-Adresse des gekoppelten Grills oder ist leer, wenn keine MAC gespeichert ist
- Die Funktion ist thread-sicher lesbar
- `bt status` in der Shell gibt die Felder lesbar formatiert aus

---

## 4. Nicht-funktionale Anforderungen

### BLE-NFR-01

#### Beschreibung

Die Bluetooth-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### BLE-NFR-02

#### Beschreibung

Das Bluetooth-Modul soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Umgesetzt | `app/src/bluetooth.c` — kein `malloc`/`free`; `K_THREAD_DEFINE` mit `BT_THREAD_STACK_SIZE = 4096` (Zeile 23, 701); statische Puffer `g_ScanSeen[16]`, `g_DiscoverParams`, `g_SubscribeParams` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im Bluetooth-Quellcode
- Stack-Größe des Bluetooth-Threads ist als Compile-Zeit-Konstante definiert
- Scan- und GATT-Puffer sind statisch alloziert

---

### BLE-NFR-03

#### Beschreibung

Die Bluetooth-Implementierung soll ausschließlich die Zephyr-BLE-API (`zephyr/bluetooth/*`) verwenden. Direkter Zugriff auf den ESP32-Bluetooth-Controller (z. B. ESP-IDF-API) ist nicht erlaubt. Die Persistenz der Grill-MAC erfolgt über den projekteigenen Konfigurationsspeicher (CFG-REQ-01..06); das Zephyr-Settings-Subsystem (`CONFIG_BT_SETTINGS`) wird nur dann aktiviert, wenn zukünftig SMP-Bonds nötig werden (siehe BLE-REQ-07).

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Umgesetzt | `app/src/bluetooth.c` — ausschließlich `zephyr/bluetooth/{bluetooth,hci,conn,uuid,gatt}.h`; kein ESP-IDF-Zugriff; MAC-Persistenz über `Config_Save()` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Ausschließliche Nutzung der Zephyr-BLE-API (`bt_enable`, `bt_le_scan_start`, `bt_conn_le_create`, `bt_gatt_*`, …)
- Kein direkter Aufruf von ESP-IDF-Bluetooth-Funktionen im Anwendungscode
- `CONFIG_BT_SETTINGS=n` und `CONFIG_BT_SMP=n`, solange der G32 kein Pairing verlangt

---

### BLE-NFR-04

#### Beschreibung

Ein Verbindungsverlust oder ein fehlerhaftes Notify-Paket darf nicht zum Absturz des Systems führen. Fehler sollen geloggt und durch automatischen Wiederverbindungsversuch (BLE-REQ-08) überbrückt werden.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Umgesetzt | `app/src/bluetooth.c:Bt_ParseAndDispatch()`: Längenprüfung `length < BT_NOTIFY_MIN_LEN` (Zeile 277), Plausibilitätsprüfungen ohne System-Reset; `Bt_DisconnectedCb()`: graceful disconnect + Reconnect via Semaphor |

#### Abhängigkeiten

- BLE-REQ-08

#### Abnahmekriterien

- Ein Verbindungsverlust führt nicht zu einem System-Reset
- Notify-Pakete mit unerwarteter Länge oder offensichtlich korrupten Werten werden verworfen und nicht in `g_TempData` geschrieben
- Fehler werden über das Zephyr-Logging-Subsystem als `LOG_WRN` oder `LOG_ERR` ausgegeben

---

## 5. Offene Punkte / Annahmen

- [x] **GATT-Protokoll des Otto Wilde G32**: Aus Community-Reverse-Engineering bekannt — siehe Abschnitt 2 für UUIDs, Byte-Layout und Sentinel.
- [x] **Pairing-Variante**: Der G32 erzwingt kein SMP-Pairing — Connect + Notify-Subscribe genügen (Abschnitt 2.4). `CONFIG_BT_SMP` bleibt deaktiviert.
- [ ] **BLE-Byte-Offset gegenüber TCP-Layout**: Die in Abschnitt 2.2 angegebenen BLE-Indizes sind aus dem TCP-Layout abgeleitet (TCP-Header `a33a` + 4 Byte Meta entfernt). Vor Inbetriebnahme mit nRF Connect / Wireshark verifizieren; bei Abweichung die Offset-Konstanten im Code anpassen.
- [ ] **Optionale RX-Charakteristik** (`dc0f41e1-…`): Schreibrichtung (z. B. zum Setzen von Alarmen am Grill) ist für diese Firmware nicht erforderlich. Sollte sie später benötigt werden, ist das Schreibformat ebenfalls zu reverse-engineeren.
- [x] **Koexistenz BT + WiFi**: Gelöst via `app/src/coex_printf.c` (Workaround für fehlende `coexist_printf`-Funktion im HAL); BLE wartet auf WiFi-Verbindung via `Wifi_WaitConnected()` und nutzt reduziertes Scan-Tastverhältnis (15 %, `BT_SCAN_INTERVAL_UNITS=160`/`BT_SCAN_WINDOW_UNITS=24`).
- [ ] **Update-Rate des Grills**: Die typische Notify-Rate des G32 (vermutet 1 Hz) ist nicht offiziell dokumentiert. Beobachten und ggf. die Mindest-Aktualisierungsraten in BLE-REQ-04..06 anpassen.
- [ ] **Maximale Anzahl Bonds**: Vorerst wird nur ein Grill pro Gerät unterstützt. Da kein SMP-Bond verwendet wird, ist die Beschränkung allein durch das Feld `grillMac` in `Config_Data_t` (BLE-REQ-07) gegeben. Eine spätere Erweiterung auf mehrere Grills ist offen.

---

## 6. Quellen (Reverse-Engineering)

Die Angaben in Abschnitt 2 stammen aus folgenden Community-Projekten:

- [sagdusmir/G32-Grill-Display-480x320-BTpref](https://github.com/sagdusmir/G32-Grill-Display-480x320-BTpref) — ESPHome-Projekt mit direkter BLE-Verbindung zum G32 ohne Cloud (UUIDs, Sentinel, Filter auf `OWG-G32C`); getestet mit G32-Firmware 1.4.5.
- [JBecker32/G32-Display-480x320-HACS](https://github.com/JBecker32/G32-Display-480x320-HACS) — ESPHome-Display in Verbindung mit der HACS-Integration von zaubii.
- [zaubii/owg-g32-ha-integration](https://github.com/zaubii/owg-g32-ha-integration) — Home-Assistant-Custom-Component, dokumentiert das 51-Byte-Cloud-TCP-Layout (Brennerzonen, Probes, Gas-Gewicht, Lid, Gas-%).
- [fschwarz86/g32](https://github.com/fschwarz86/g32) — Bestätigt, dass das TCP-Binärformat layout-identisch zur BLE-Payload ist.
- [Grillsportverein.de: „Otto Wilde G32 | Smarthome"](https://www.grillsportverein.de/forum/threads/otto-wilde-g32-smarthome.369079/) — Forum-Thread mit Sniffer-Logs und Diskussion (insb. Seite 6).
- [Home-Assistant-Community: BLE-Client mehrere Werte aus G32](https://community.home-assistant.io/t/ble-client-reading-multiple-values-from-the-same-service-uuid-in-a-decent-way/926346) — YAML-Beispiele mit Byte-Indizes für Brennerzonen.

---

## 7. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-30 |       | Erstellt: BLE-REQ-01..10, BLE-NFR-01..04 — Bluetooth-Kommunikation mit Otto Wilde G32 (Pairing über Shell, Brenner-/Kerntemperaturen, Gasfüllstand, persistente Kopplung mit Auto-Connect) |
| 1.1     | 2026-05-30 |       | Abschnitt 2 „Bekanntes Protokoll" ergänzt (GATT-UUIDs, Byte-Layout, Sentinel) auf Basis Community-Reverse-Engineering; BLE-REQ-03 mit Service-/Name-Filter präzisiert; BLE-REQ-04..06 mit konkreten Byte-Indizes und Sentinel-Behandlung ergänzt; BLE-REQ-07 von SMP-Bond-Persistenz auf MAC-Binding in `Config_Data_t` umgestellt; BLE-NFR-03 entsprechend angepasst (`CONFIG_BT_SETTINGS=n`, `CONFIG_BT_SMP=n`); Offene Punkte und Quellen aktualisiert |
| 1.2     | 2026-06-15 |       | Status-Update: BLE-REQ-01..10, BLE-NFR-02..04 → Umgesetzt; Koexistenz BT+WiFi als gelöst markiert |
