# Requirements: Temperaturdaten

## 1. Übersicht

Das Temperaturdaten-Modul stellt eine zentrale, thread-sichere Datenstruktur bereit, in der die aktuellen Messwerte und Sollwerte des Grills gespeichert werden. Die Daten werden vom Bluetooth-Modul geschrieben und vom Webserver gelesen.

---

## 2. Funktionale Anforderungen

### TMP-REQ-01

#### Beschreibung

Die Temperaturdaten sollen in einer Struct `Temp_Data_t` gespeichert werden. Jeder Einzelwert besteht aus einem Zahlenwert (`int16_t`) und einem Gültigkeitsflag (`bool valid`). Bei Temperaturen ist die Einheit °C, bei Füllständen %. Ein Wert von 0 ist ein gültiger Messwert und kein Sonderwert. Nicht gesetzte oder nicht verfügbare Messwerte werden durch `valid = false` gekennzeichnet und im Webserver als `--` angezeigt. Die Struct enthält:

- 4 Brennertemperaturen (`burner[4]`, °C): Initialisierungswert 20, `valid = true`
- 4 Kerntemperaturen (`core[4]`, °C): Initialisierungswert 0, `valid = false`
- 1 Gasflaschen-Füllstand (`gas`, %, DSP-REQ-06): Initialisierungswert 0, `valid = false`

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/temp_data.h:Temp_Entry_t`, `Temp_Data_t`; `app/src/temp_data.c:g_TempData` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- `Temp_Entry_t` enthält genau die Felder `value` (int16_t) und `valid` (bool)
- `Temp_Data_t` enthält `burner[4]`, `core[4]` und `gas` vom Typ `Temp_Entry_t`
- Nach dem Start zeigen alle vier Brenner 20 °C an (`valid = true`)
- Nach dem Start zeigen alle Kerntemperaturen sowie der Gasfüllstand `--` an (`valid = false`)
- Ein gesetzter Wert von 0 wird als `0 °C` bzw. `0 %` angezeigt, nicht als `--`

---

### TMP-REQ-02

#### Beschreibung

Die Temperaturdaten-Struct soll als globale Variable `g_TempData` verfügbar sein und durch einen Zephyr-Mutex `g_TempMutex` vor gleichzeitigem Zugriff aus mehreren Threads geschützt werden. Vor jedem Lese- oder Schreibzugriff ist der Mutex zu sperren und danach freizugeben.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/temp_data.c:g_TempMutex` (`K_MUTEX_DEFINE`), `g_TempData`; Zugriff in `app/src/webserver.c:Webserver_IndexCb()` unter Mutex |

#### Abhängigkeiten

- TMP-REQ-01 (Datenstruktur)

#### Abnahmekriterien

- `g_TempMutex` ist statisch mit `K_MUTEX_DEFINE` initialisiert
- `g_TempData` ist statisch initialisiert (kein `Temp_Init()`-Aufruf nötig)
- Jeder Lesezugriff im Webserver erfolgt zwischen `k_mutex_lock` und `k_mutex_unlock`
- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free`

---

### TMP-REQ-03

#### Beschreibung

Das Modul soll ein Änderungssignal bereitstellen, über das wartende Verbraucher (insbesondere der SSE-Handler des Webservers, WEB-REQ-06) benachrichtigt werden, sobald neue Messwerte vorliegen. Dazu wird ein Generationszähler `g_TempGen` (`uint32_t`) geführt, der bei jeder Wertänderung unter `g_TempMutex` erhöht wird, sowie eine Bedingungsvariable `g_TempCondvar`, auf die alle Warter geweckt werden (`k_condvar_broadcast`).

Zum Setzen einzelner Werte stellt das Modul die Funktion `Temp_Set(group, zone, value, valid)` bereit, die Wert und Gültigkeit atomar unter Mutex setzt, `g_TempGen` erhöht und die Warter weckt. Für Erzeuger, die `g_TempData` selbst unter Mutex aktualisieren und danach genau eine Sammel-Benachrichtigung auslösen möchten, gibt es `Temp_NotifyChanged()`.

Ein Verbraucher erkennt eine Änderung, indem er sich den zuletzt gesehenen Wert von `g_TempGen` merkt und nur dann sendet, wenn dieser sich vom aktuellen Stand unterscheidet (verhindert verpasste oder doppelte Benachrichtigungen).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/temp_data.h`/`.c`: `g_TempGen`, `g_TempCondvar` (`K_CONDVAR_DEFINE`), `Temp_Set()`, `Temp_NotifyChanged()`; Testbefehl `temp set`/`temp clear` in `app/src/shell.c` |

#### Abhängigkeiten

- TMP-REQ-02 (Mutex-geschützte Datenstruktur)

#### Abnahmekriterien

- `g_TempGen` wird ausschließlich unter `g_TempMutex` erhöht
- `g_TempCondvar` ist statisch mit `K_CONDVAR_DEFINE` initialisiert
- `Temp_Set()` setzt Wert + Gültigkeit, erhöht `g_TempGen` und ruft `k_condvar_broadcast` — alles unter Mutex
- `Temp_Set()` liefert `-EINVAL` bei ungültiger Gruppe oder Zone (≥ `TEMP_ZONE_COUNT`)
- `Temp_NotifyChanged()` erhöht `g_TempGen` und weckt Warter, ohne einen Wert zu ändern
- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free`

---

## 3. Nicht-funktionale Anforderungen

### TMP-NFR-01

#### Beschreibung

Die Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen und ausschließlich statischen Speicher verwenden.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free`
- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`

---

## 4. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-28 |       | Erstellt: TMP-REQ-01 (Struct), TMP-REQ-02 (Mutex) |
| 1.1     | 2026-05-29 |       | TMP-REQ-03 ergänzt und umgesetzt: Änderungssignal (`g_TempGen`, `g_TempCondvar`, `Temp_Set`, `Temp_NotifyChanged`) |
| 1.2     | 2026-05-29 |       | `target[4]` und `TEMP_GROUP_TARGET` entfernt — die Zieltemperatur wird durch die Grillgut-Profile aus DSP-REQ-04 abgelöst |
| 1.3     | 2026-05-29 |       | Feld `gas` (Gasflaschen-Füllstand, %) und `Temp_SetGas()` ergänzt für DSP-REQ-06 |
