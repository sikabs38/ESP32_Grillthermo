---
name: style
description: Explizite Prüfung von C-Code auf Coding-Standard-Verstöße mit Korrekturvorschlägen (Standards gelten immer, dieser Skill ist optional)
trigger: /style
args: "[Dateipfad oder leer für alle geänderten Dateien]"
examples:
  - /style
  - /style app/src/sensor.c
  - /style app/src/
---

# Skill: style

## Beschreibung

Prüft C-Quellcode auf Einhaltung der Projekt-Coding-Standards und behebt Verstöße.

## Aufruf

```
/style [Dateipfad]
```

**Beispiele:**
```
/style                    # alle geänderten Dateien prüfen
/style app/src/sensor.c   # einzelne Datei prüfen
/style app/src/           # ganzes Verzeichnis prüfen
```

## Coding Standards (aus CLAUDE.md)

### Sprache
- **C11** — kein C++, kein GNU-spezifisches C außer bei expliziter Notwendigkeit

### Formatierung
- **4 Leerzeichen** Einrückung (keine Tabs)
- **Maximal 100 Zeichen** pro Zeile

### Benennung

| Kategorie       | Schema                    | Beispiel                   |
|----------------|---------------------------|----------------------------|
| Funktionen      | `ModuleName_FunctionName` | `Sensor_ReadCurrent()`     |
| Lokale Variablen | camelCase                | `rawValue`, `busAddress`   |
| Globale Variablen | `g_PascalCase`          | `g_SensorHandle`           |
| Makros          | `ALL_CAPS_WITH_UNDERSCORES` | `MAX_RETRY_COUNT`        |

### MISRA C 2023
- Alle Regeln müssen eingehalten werden
- Jede Abweichung braucht einen Kommentar mit Begründung direkt über der betroffenen Zeile:
  ```c
  /* MISRA C 2023, Rule X.Y deviate: <Begründung> */
  ```
- Prüfung mit: `cppcheck --addon=misra <datei>`

## Wann verwenden

**Die Coding Standards gelten immer** — Claude wendet sie bei jedem geschriebenen oder geänderten C-Code automatisch an.

Dieser Skill wird **optional** aufgerufen für eine explizite Prüfung mit strukturiertem Bericht:

TRIGGER: Nutzer ruft `/style` auf oder bittet explizit darum, bestehenden Code systematisch zu prüfen und Verstöße aufzulisten.

SKIP: Normales Schreiben oder Ändern von Code — dort gelten die Standards direkt, ohne Skill-Aufruf.

## Ablauf

1. **Zieldateien bestimmen**
   - Mit Argument: angegebene Datei(en) prüfen
   - Ohne Argument: `git diff --name-only` für geänderte `.c`- und `.h`-Dateien

2. **Dateien lesen** und systematisch auf folgende Punkte prüfen:

   a. **Einrückung**: 4 Leerzeichen, keine Tabs  
   b. **Zeilenlänge**: keine Zeile > 100 Zeichen  
   c. **Funktionsnamen**: Schema `ModuleName_FunctionName`  
   d. **Variablennamen**: lokal camelCase, global `g_PascalCase`  
   e. **Makronamen**: `ALL_CAPS_WITH_UNDERSCORES`  
   f. **MISRA-Abweichungen**: Kommentar vorhanden und begründet?

3. **Verstöße auflisten** mit Datei, Zeilennummer und Beschreibung

4. **Korrekturen anbieten**: Für jeden Verstoß eine konkrete Korrektur vorschlagen

5. **Auf Bestätigung warten** und dann Korrekturen anwenden


## Ausgabeformat

```
## Style-Prüfung: <datei>

### Verstöße

| Zeile | Regel            | Problem                          | Vorschlag                        |
|-------|------------------|----------------------------------|----------------------------------|
| 12    | Zeilenlänge      | 112 Zeichen (max. 100)           | Ausdruck umbrechen               |
| 24    | Funktionsname    | `read_sensor()` → falsche Form   | Umbenennen in `Sensor_Read()`    |
| 31    | Globale Variable | `sensorHandle` ohne `g_`-Präfix  | Umbenennen in `g_SensorHandle`   |

### Zusammenfassung

X Verstöße gefunden, Y behoben, Z offen.
```

## Hinweise

- MISRA-Abweichungen niemals stillschweigend entfernen — immer Begründung prüfen
- Bei globalen Variablen: nur `g_`-Präfix, kein `m_` oder andere Konventionen
- `static`-Funktionen innerhalb einer Übersetzungseinheit sind modulintern und folgen dennoch dem `ModuleName_FunctionName`-Schema
- Header-Guards nach Schema: `APP_<DATEINAME_GROSS>_H` (z.B. `APP_SENSOR_H`)
