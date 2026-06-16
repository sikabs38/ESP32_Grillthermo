# Requirements: Konfigurationsspeicher

## 1. Übersicht

Die Systemkonfiguration (WiFi, MQTT, Systemparameter) soll dauerhaft in einem nichtflüchtigen Speicher abgelegt werden, sodass sie nach einem Neustart oder Stromausfall erhalten bleibt. Als Speichermedium wird der interne Flash-Speicher des ESP32-S3 über das Zephyr NVS-Subsystem (Non-Volatile Storage) genutzt.

---

## 2. Funktionale Anforderungen

### CFG-REQ-01

#### Beschreibung

Das System soll die Konfiguration beim Schreiben eines Parameters automatisch im nichtflüchtigen Speicher sichern. Ein expliziter Speicherbefehl durch den Nutzer ist nicht erforderlich.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.c:Config_Save()` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Nach dem Setzen eines Parameters via Shell ist dieser nach einem Neustart weiterhin vorhanden
- Der Schreibvorgang wird vor der Bestätigungsausgabe an den Nutzer abgeschlossen

---

### CFG-REQ-02

#### Beschreibung

Das System soll beim Start die gespeicherte Konfiguration automatisch aus dem nichtflüchtigen Speicher laden und anwenden.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.c:Config_Load()`, `app/src/shell.c:Shell_LoadConfig()` (SYS_INIT APPLICATION) |

#### Abhängigkeiten

- CFG-REQ-01

#### Abnahmekriterien

- Nach einem Neustart sind alle zuvor gespeicherten Parameter ohne Nutzerinteraktion aktiv
- Fehlt ein Parameter im Speicher, wird ein definierter Standardwert verwendet
- Der Ladevorgang erfolgt vor der Initialisierung der abhängigen Subsysteme (WiFi, MQTT)

---

### CFG-REQ-03

#### Beschreibung

Das System soll die Konfiguration über die Shell auf die Werkseinstellungen zurücksetzen können. Dabei werden alle gespeicherten Parameter gelöscht und auf ihre Standardwerte zurückgesetzt. Die PIN wird auf den Initialisierungswert `000000` zurückgesetzt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/config.c:Config_InvalidateAll()`, `app/src/shell.c:Shell_CmdConfigReset()` |

#### Abhängigkeiten

- CFG-REQ-01
- CFG-REQ-04 (Parameterliste mit Standardwerten)
- SHL-REQ-01 (Shell über USB, siehe `shell.md`)
- SHL-REQ-06 (PIN-Schutz, siehe `shell.md`)

#### Abnahmekriterien

- Der Befehl `config reset` setzt alle Parameter aus CFG-REQ-04 auf ihre definierten Standardwerte
- Die PIN wird auf `000000` zurückgesetzt
- Nach einem anschließenden Neustart sind ausschließlich Standardwerte aktiv
- Beim nächsten Login erscheint der Hinweis zur PIN-Änderung gemäß SHL-REQ-06
- Der Nutzer wird vor dem Reset zur Bestätigung aufgefordert (`config reset confirm` erforderlich)

---

### CFG-REQ-04

#### Beschreibung

Das System soll folgende Konfigurationsparameter persistent speichern:

| Parameter       | Typ     | Max. Länge  | Standardwert  | Verschlüsselt |
|-----------------|---------|-------------|---------------|---------------|
| WiFi SSID       | String  | 32 Zeichen  | `[leer]`      | Nein          |
| WiFi Passwort   | String  | 64 Zeichen  | `[leer]`      | Ja (AES)      |
| WiFi Hostname   | String  | 63 Zeichen  | `[leer]`      | Nein          |
| MQTT Broker     | String  | 128 Zeichen | `[leer]`      | Nein          |
| MQTT Port       | uint16  | —           | `1883`        | Nein          |
| Shell-PIN       | String  | 4–6 Ziffern | `000000`      | Ja (AES)      |
| Grill-MAC       | String  | 17 Zeichen (`AA:BB:CC:DD:EE:FF`) | `[leer]` | Nein |
| Netzwerkmodus   | Enum    | —           | `webserver`   | Nein          |

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.h:Config_Data_t` (inkl. `grillMac`, BLE-REQ-07), `app/src/config.c:Config_GetDefaults()`, `app/src/shell.c:g_Config` |

#### Abhängigkeiten

- CFG-REQ-01

#### Abnahmekriterien

- Alle aufgelisteten Parameter werden korrekt gespeichert und geladen
- Werte außerhalb der definierten Grenzen werden abgewiesen

---

### CFG-REQ-05

#### Beschreibung

Passwörter (WiFi, zukünftige Zugangsdaten) sollen vor dem Schreiben in den nichtflüchtigen Speicher mit AES verschlüsselt werden. Im Klartext darf ein Passwort ausschließlich im flüchtigen RAM während der Laufzeit vorliegen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.c:Config_Encrypt()`, `Config_Decrypt()` — AES-128-CBC via PSA (`psa_cipher_encrypt`/`psa_cipher_decrypt`); IV zufällig durch PSA, Ausgabe IV\|\|Chiffrat in `Config_Stored_t.encData`; `psa_crypto_init()` in `Config_Init()`; prj.conf: `CONFIG_MBEDTLS_PSA_CRYPTO_C`, `CONFIG_PSA_WANT_KEY_TYPE_AES`, `CONFIG_PSA_WANT_ALG_CBC_NO_PADDING` |

#### Abhängigkeiten

- CFG-REQ-01 (Speichern der Konfiguration)
- CFG-REQ-02 (Laden der Konfiguration)

#### Abnahmekriterien

- Im NVS gespeicherte Passwörter sind AES-verschlüsselt und nicht im Klartext auslesbar
- Der AES-Schlüssel ist nicht im Flash gespeichert (z.B. aus dem ESP32-S3 eFuse oder einem festen Compile-Zeit-Secret abgeleitet)
- Nach dem Laden aus dem NVS wird das Passwort im RAM entschlüsselt und nach Verwendung gelöscht (`memset`)
- Die AES-Implementierung nutzt Zephyr MbedTLS (`CONFIG_MBEDTLS=y`)

---

### CFG-REQ-06

#### Beschreibung

Die Konfiguration soll in drei Generationen im nichtflüchtigen Speicher gespeichert werden. Jeder Datensatz wird mit einer CRC32-Prüfsumme abgesichert, die alle Felder des Datensatzes außer dem CRC-Feld selbst umfasst. Zusätzlich enthält jeder Datensatz ein Gültigkeitsflag. Ein ungültiger Datensatz hat `valid = false` und eine absichtlich falsche CRC.

Beim Laden wird der neueste gültige Datensatz ausgewählt (höchste Generationsnummer mit korrekter CRC und `valid = true`). Die Generationsnummer ist ein `uint8_t` mit Überlaufbehandlung.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/config.c:Config_Record_t`, `Config_ComputeCrc()`, `Config_IsRecordValid()`, `Config_FindNewest()` |

#### Abhängigkeiten

- CFG-REQ-01
- CFG-REQ-02

#### Abnahmekriterien

- Es existieren genau drei NVS-Slots (IDs 1, 2, 3); Slot-Zuweisung per `generation % 3`
- Jeder Datensatz enthält: `generation` (uint8_t), `valid` (bool), `data` (Config_Data_t), `crc` (uint32_t, letztes Feld)
- CRC32 wird über alle Bytes von Datensatzbeginn bis ausschließlich des CRC-Feldes berechnet (`crc32_ieee`)
- Beim Laden wird der Datensatz mit der höchsten gültigen Generationsnummer verwendet
- Ein ungültiger Datensatz hat `valid = false` UND eine falsche CRC (korrekte CRC XOR 0xFFFFFFFF)
- Die Generationsauswahl verwendet modularen `uint8_t`-Vergleich für Überlaufbehandlung

---

### CFG-REQ-07

#### Beschreibung

Das System soll einen Netzwerkmodus als Konfigurationsparameter speichern. Der Modus legt fest, welcher der beiden Netzwerkdienste — Webserver oder MQTT-Client — beim Systemstart aktiviert wird. Nur einer der beiden Dienste kann gleichzeitig aktiv sein (siehe ADR-001).

Gültige Werte: `webserver` (Webserver aktiv, MQTT-Client inaktiv) und `mqtt` (MQTT-Client aktiv, Webserver inaktiv). Der Standardwert nach Erstinbetriebnahme und nach Factory Reset (CFG-REQ-03) ist `webserver`.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

- CFG-REQ-01 (Persistentes Speichern)
- CFG-REQ-02 (Laden beim Start)
- CFG-REQ-03 (Factory Reset setzt Standardwert `webserver`)
- ADR-001 (Designentscheidung: gegenseitiger Ausschluss)

#### Abnahmekriterien

- `Config_Data_t` enthält ein Feld `networkMode` vom Typ `Config_NetworkMode_t` (Enum: `CFG_NETWORK_WEBSERVER`, `CFG_NETWORK_MQTT`)
- Der Standardwert ist `CFG_NETWORK_WEBSERVER`
- Der Wert wird korrekt in den NVS geschrieben und beim Start geladen
- `config show` zeigt den aktuellen Netzwerkmodus an (`Webserver` oder `MQTT`)
- `config reset` setzt den Netzwerkmodus auf `webserver` zurück
- Ungültige Werte im NVS werden durch den Standardwert `webserver` ersetzt

---

## 3. Nicht-funktionale Anforderungen

### CFG-NFR-01

#### Beschreibung

Die Implementierung des Konfigurationsspeichers soll das Zephyr-NVS-Subsystem (Non-Volatile Storage) verwenden. Es darf kein direkter Zugriff auf Flash-Register erfolgen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Umgesetzt | `app/src/config.c`: ausschließlich `nvs_mount`, `nvs_read`, `nvs_write` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Ausschließliche Nutzung der Zephyr-NVS-API (`nvs_write`, `nvs_read`, `nvs_delete`)
- Kein direkter Zugriff auf Flash-Adressen oder ESP32-spezifische Register

---

### CFG-NFR-02

#### Beschreibung

Der Konfigurationsspeicher soll MISRA-C-konform implementiert sein. Dynamische Speicherverwaltung ist nicht erlaubt.

| Priorität | Kategorie    | Status | Implementierung |
|-----------|--------------|--------|-----------------|
| Hoch      | Wartbarkeit  | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Alle Puffer sind statisch alloziert

---

### CFG-NFR-03

#### Beschreibung

Ein Lese- oder Schreibfehler im nichtflüchtigen Speicher darf nicht zum Absturz des Systems führen. Fehler sollen geloggt und mit Standardwerten überbrückt werden.

| Priorität | Kategorie        | Status | Implementierung |
|-----------|------------------|--------|-----------------|
| Hoch      | Zuverlässigkeit  | Umgesetzt | `app/src/config.c:Config_Init()`, `Config_Load()`, `Config_Save()` — Fehler werden als `LOG_ERR` geloggt, Standardwerte verwendet |

#### Abhängigkeiten

- CFG-REQ-02

#### Abnahmekriterien

- Bei Lesefehler wird der Standardwert des betroffenen Parameters verwendet
- Der Fehler wird über das Zephyr-Logging-Subsystem als `LOG_ERR` ausgegeben
- Das System startet trotz Speicherfehler vollständig durch

---

## 4. Offene Punkte / Annahmen

- [x] NVS-Partition: `storage_partition` bereits in Board-DTS (`partitions_0x0_amp_4M.dtsi`) bei 0x3B0000, 192 KB — kein DTS-Overlay erforderlich
- [x] Verschlüsselung des WiFi-Passworts im Flash: AES, siehe CFG-REQ-05
- [ ] Flash-Verschleiß (Write Cycles) bei häufigen Schreibzugriffen nicht bewertet
- [x] CFG-REQ-05 (AES-Verschlüsselung) umgesetzt: PSA AES-128-CBC in `config.c`, kein TODO mehr in `config.h` / `shell.c`

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-26 |       | Erstellt |
| 1.1     | 2026-05-26 |       | CFG-REQ-05 ergänzt: AES-Verschlüsselung für Passwörter |
| 1.2     | 2026-05-26 |       | CFG-REQ-03 präzisiert: Reset setzt alle Parameter inkl. PIN auf Standardwerte; CFG-REQ-04: Shell-PIN als Parameter ergänzt |
| 1.3     | 2026-05-27 |       | CFG-REQ-06 ergänzt: 3-Generationen-Speicher mit CRC32 und Gültigkeitsflag; Implementierungsstatus aktualisiert |
| 1.4     | 2026-05-27 |       | Skill-Alignment: Status „Implementiert" → „Umgesetzt", MISRA C → MISRA C 2023, Bestätigungstext CFG-REQ-03 korrigiert |
| 1.5     | 2026-05-30 |       | CFG-REQ-04 um `Grill-MAC` (BLE-REQ-07) und den bereits umgesetzten `WiFi Hostname` (WIF-REQ-06) ergänzt |
| 1.6     | 2026-06-16 |       | CFG-REQ-04 um `Netzwerkmodus` erweitert; CFG-REQ-07 ergänzt: Netzwerkmodus als persistenter Parameter (ADR-001) |
| 1.7     | 2026-06-16 |       | CFG-REQ-05: Status → Umgesetzt; PSA AES-128-CBC in `config.c` implementiert |
