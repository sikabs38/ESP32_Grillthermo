# Requirements: Temperaturdaten

## 1. Übersicht

Das Temperaturdaten-Modul stellt eine zentrale, thread-sichere Datenstruktur bereit, in der die aktuellen Messwerte und Sollwerte des Grills gespeichert werden. Die Daten werden vom Bluetooth-Modul geschrieben und vom Webserver gelesen.

---

## 2. Funktionale Anforderungen

### TMP-REQ-01

#### Beschreibung

Die Temperaturdaten sollen in einer Struct `Temp_Data_t` gespeichert werden. Jeder Einzelwert besteht aus dem Temperaturwert (`int16_t`, Einheit °C) und einem Gültigkeitsflag (`bool valid`). Ein Wert von 0 °C ist ein gültiger Messwert und kein Sonderwert. Nicht gesetzte oder nicht verfügbare Messwerte werden durch `valid = false` gekennzeichnet und im Webserver als `--` angezeigt. Die Struct enthält:

- 4 Brennertemperaturen (`burner[4]`): Initialisierungswert 20 °C, `valid = true`
- 4 Kerntemperaturen (`core[4]`): Initialisierungswert 0 °C, `valid = false`
- 4 Zieltemperaturen (`target[4]`): Initialisierungswert 0 °C, `valid = false`

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/temp_data.h:Temp_Entry_t`, `Temp_Data_t`; `app/src/temp_data.c:g_TempData` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- `Temp_Entry_t` enthält genau die Felder `value` (int16_t) und `valid` (bool)
- `Temp_Data_t` enthält `burner[4]`, `core[4]` und `target[4]` vom Typ `Temp_Entry_t`
- Nach dem Start zeigen alle vier Brenner 20 °C an (`valid = true`)
- Nach dem Start zeigen alle Kern- und Zieltemperaturen `--` an (`valid = false`)
- Ein gesetzter Wert von 0 °C wird als `0 °C` angezeigt, nicht als `--`

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
