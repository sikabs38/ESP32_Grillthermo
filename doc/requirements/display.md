# Requirements: Anzeige der Temperaturblöcke

## 1. Übersicht

Die Webanzeige stellt vier Temperaturblöcke dar — einen pro Grillzone. Jeder Block besteht aus zwei nebeneinander angeordneten Anzeigefeldern: Garraumtemperatur und Kerntemperatur des Grillguts. Beide Felder sind identisch aufgebaut und folgen demselben visuellen Schema. Unterhalb der vier Blöcke wird der Gasflaschen-Füllstand zentriert dargestellt (DSP-REQ-06).

Grundlage: `doc/G32_Anzeige_Beschreibung.docx`, Version 1.0 vom 29. Mai 2026.

---

## 2. Funktionale Anforderungen

### DSP-REQ-01

#### Beschreibung

Die Anzeige soll vier Temperaturblöcke darstellen, einen pro Grillzone (Index 0..3, entsprechend `burner[0..3]` und `core[0..3]` aus TMP-REQ-01). Jeder Block enthält links eine Garraumtemperatur-Anzeige (DSP-REQ-02) und rechts eine Kerntemperatur-Anzeige (DSP-REQ-03).

Die vier Blöcke werden in einem zweispaltigen Raster (2 × 2) angeordnet, da die typischen Anzeigegeräte (Tablet, Notebook, Desktop) im Querformat (Landscape) betrieben werden. Auf sehr schmalen Bildschirmen (Portrait, Breite < 600 px) fällt die Anordnung auf einspaltig zurück, damit die Inhalte lesbar bleiben.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch | Umgesetzt | `app/src/webserver.c:Webserver_BuildHtml()` Schleife über `TEMP_ZONE_COUNT`; Block-Fragmente `k_BlockOpen`/`k_BlockMid`/`k_BlockClose`; Grid-Container `k_GridOpen`/`k_GridClose` mit CSS-Klasse `.gr` (zwei Spalten, Media-Query für Portrait) |

#### Abhängigkeiten

- TMP-REQ-01 (Datenstruktur mit `burner[4]` und `core[4]`)

#### Abnahmekriterien

- Die Anzeige enthält genau vier Temperaturblöcke
- Jeder Block zeigt Garraum- und Kerntemperatur derselben Grillzone
- Die Blöcke sind eindeutig den Grillzonen 1..4 zugeordnet
- Im Landscape-Modus (Fensterbreite ≥ 600 px) sind die Blöcke in zwei Spalten (2 × 2) angeordnet
- Bei Fensterbreite < 600 px wechselt die Anordnung auf eine Spalte (vier Blöcke untereinander)

---

### DSP-REQ-02 — Garraumtemperatur

#### Beschreibung

Jede Garraumtemperatur-Anzeige zeigt die aktuelle Temperatur im Innenraum des G32-Grills für die jeweilige Zone.

**Wertebereich:**

| Eigenschaft  | Wert                |
|--------------|---------------------|
| Minimum      | 0 °C                |
| Maximum      | 450 °C              |
| Einheit      | Grad Celsius (°C)   |
| Darstellung  | Ganzzahl (gerundet) |

**Anzeigeelemente:**

- Numerischer Wert: große digitale Zahl in °C, sofort ablesbar
- Farbbalken: horizontale Skala über 0–450 °C, in drei Zonen unterteilt
- Indikator: bewegliche senkrechte Markierung auf dem Balken, zeigt die aktuelle Position
- Zonenbezeichnung: Textzeile unterhalb des Balkens mit dem Namen der aktuellen Zone

**Farbzonen:**

| Zone             | Von (°C) | Bis (°C) | Verwendung              |
|------------------|----------|----------|-------------------------|
| Niedrig (Blau)   | 0        | 149      | Aufheizen, Low & Slow   |
| Mittel (Orange)  | 150      | 299      | Direktes Grillen        |
| Hoch (Rot)       | 300      | 450      | Searing, maximale Hitze |

**Zonennamen (Textanzeige):**

| Bereich (°C) | Angezeigte Bezeichnung | Typische Anwendung           | Hinweis              |
|--------------|------------------------|------------------------------|----------------------|
| 0 – 99       | Aufheizen              | Vorheizphase                 |                      |
| 100 – 179    | Low & Slow             | Barbecue, indirektes Garen   |                      |
| 180 – 279    | Direktes Grillen       | Standardgrillen              |                      |
| 280 – 399    | Searing                | Scharfes Anbraten            |                      |
| 400 – 450    | Maximale Hitze         | Kruste, kurze Hochtemperatur | Obere Grenze des G32 |

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch | Umgesetzt | `app/src/webserver.c`: Fragmente `k_DispOpenA`..`k_DispClose`, `Html_AppendDisplay()` (group `'b'`); JS-Konstante `GR` und `up()` im `k_HtmlScript` |

#### Abhängigkeiten

- DSP-REQ-01 (Vier Temperaturblöcke)
- TMP-REQ-01 (`burner[i]`-Werte)

#### Abnahmekriterien

- Numerischer Wert wird als gerundete Ganzzahl in °C dargestellt
- Werte 0–149 °C werden im blauen, 150–299 °C im orangen, 300–450 °C im roten Bereich dargestellt
- Indikatorposition entspricht dem aktuellen Messwert auf der Skala 0..450 °C
- Zonenbezeichnung wechselt gemäß obiger Tabelle beim Überschreiten der Grenzen

---

### DSP-REQ-03 — Kerntemperatur

#### Beschreibung

Jede Kerntemperatur-Anzeige zeigt die Innentemperatur des Grillguts in der jeweiligen Zone. Wertebereich und Farbzonen sind abhängig vom gewählten Grillgut-Profil (siehe DSP-REQ-04).

**Anzeigeelemente:**

- Numerischer Wert: große digitale Zahl in °C
- Farbbalken: horizontale Skala, die den profilspezifischen Bereich in Garstufen unterteilt
- Indikator: bewegliche Markierung, positioniert relativ zum Profilbereich (Min–Max)
- Garstufe: Textzeile mit der aktuellen Garstufe gemäß aktivem Profil
- Legende: farbige Punkte mit Bezeichnungen aller Garstufen des aktiven Profils

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch | Umgesetzt | `app/src/webserver.c`: `Html_AppendDisplay()` (group `'c'`, `withLegend=true`); JS-Konstante `PR` und `up()` im `k_HtmlScript` |

#### Abhängigkeiten

- DSP-REQ-01 (Vier Temperaturblöcke)
- DSP-REQ-04 (Grillgut-Profile)
- TMP-REQ-01 (`core[i]`-Werte)

#### Abnahmekriterien

- Numerischer Wert wird als gerundete Ganzzahl in °C dargestellt
- Farbbalken bildet den Min..Max-Bereich des aktiven Profils ab
- Garstufenname entspricht der zum aktuellen Wert gehörenden Stufe des aktiven Profils
- Legende zeigt alle Garstufen des aktiven Profils mit deren Farben

---

### DSP-REQ-04 — Grillgut-Profile

#### Beschreibung

Das aktive Grillgut-Profil wird über eine Auswahl-Leiste (Chips) oberhalb der Anzeigen gesetzt. Es kann jederzeit gewechselt werden; die Skala der Kerntemperatur-Anzeigen passt sich sofort an.

**Rind (Steak):**

| Garstufe    | Von (°C) | Bis (°C) | Farbe  |
|-------------|----------|----------|--------|
| Rare        | 48       | 54       | Blau   |
| Medium Rare | 54       | 58       | Grün   |
| Medium      | 58       | 63       | Orange |
| Well Done   | 63       | 75       | Rot    |

**Schwein:**

| Garstufe   | Von (°C) | Bis (°C) | Farbe  |
|------------|----------|----------|--------|
| Rosa       | 60       | 65       | Grün   |
| Durch      | 65       | 75       | Orange |
| Sehr durch | 75       | 85       | Rot    |

**Geflügel:**

| Garstufe | Von (°C) | Bis (°C) | Farbe  |
|----------|----------|----------|--------|
| Ziel     | 74       | 82       | Grün   |
| Sicher   | 82       | 90       | Orange |

**Fisch:**

| Garstufe | Von (°C) | Bis (°C) | Farbe |
|----------|----------|----------|-------|
| Glasig   | 45       | 52       | Blau  |
| Durch    | 52       | 62       | Grün  |
| Zu durch | 62       | 72       | Rot   |

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch | Umgesetzt | `app/src/webserver.c`: `k_HtmlProfiles` (Chips); JS-Objekt `PR` mit `rind`/`schwein`/`gefluegel`/`fisch`; `setPr()` aktualisiert Skala und Legende, `cp`-Default = `'rind'` |

#### Abhängigkeiten

- DSP-REQ-03 (Kerntemperatur-Anzeige)

#### Abnahmekriterien

- Auswahl-Leiste enthält die vier Profile Rind, Schwein, Geflügel, Fisch
- Wechsel des Profils aktualisiert sofort die Skala der Kerntemperatur-Anzeigen
- Garstufengrenzen und Farben entsprechen den obigen Tabellen

---

### DSP-REQ-05 — Verhalten bei Über-/Unterschreitung

#### Beschreibung

Bei Kerntemperaturwerten außerhalb des Profilbereichs verhält sich die Anzeige wie folgt:

- Werte unterhalb des Profilminimums: Indikator steht am linken Anschlag, kein Zonenname.
- Werte oberhalb des Profilmaximums: Indikator steht am rechten Anschlag, letzte Garstufe wird angezeigt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel | Umgesetzt | `app/src/webserver.c`: `up()`-Funktion im `k_HtmlScript` (Zweige `e.v<p.mn` → `pos=0` ohne Stufe, `e.v>p.mx` → `pos=100` mit `arr[arr.length-1].n`) |

#### Abhängigkeiten

- DSP-REQ-03 (Kerntemperatur-Anzeige)
- DSP-REQ-04 (Grillgut-Profile)

#### Abnahmekriterien

- Bei Wert < Profilminimum: Indikator am linken Anschlag, keine Garstufe angezeigt
- Bei Wert > Profilmaximum: Indikator am rechten Anschlag, oberste Garstufe angezeigt

---

### DSP-REQ-06 — Gasflaschen-Füllstand

#### Beschreibung

Unterhalb der vier Temperaturblöcke (DSP-REQ-01) soll der Füllstand der Gasflasche zentriert dargestellt werden. Die Anzeige enthält:

- Numerischer Wert: große digitale Zahl in Prozent
- Farbbalken: horizontale Skala über 0–100 %, in drei Zonen unterteilt
- Indikator: bewegliche senkrechte Markierung auf dem Balken, zeigt die aktuelle Position

**Wertebereich:**

| Eigenschaft  | Wert                |
|--------------|---------------------|
| Minimum      | 0 %                 |
| Maximum      | 100 %               |
| Einheit      | Prozent (%)         |
| Darstellung  | Ganzzahl (gerundet) |

**Farbzonen:**

| Zone            | Von (%) | Bis (%) | Verwendung                  |
|-----------------|---------|---------|-----------------------------|
| Kritisch (Rot)  | 0       | 5       | Sofort tauschen             |
| Niedrig (Gelb)  | 5       | 10      | Ersatzflasche bereithalten  |
| OK (Grün)       | 10      | 100     | Ausreichend                 |

Bei `valid = false` wird `--` angezeigt (z. B. wenn noch kein Sensorwert vorliegt).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch | Umgesetzt | `app/src/temp_data.h`: `Temp_Data_t.gas`, `Temp_SetGas()`; `app/src/webserver.c`: `k_GasOpen`/`k_GasClose`, `Html_AppendGas()`, JS-Konstante `GS`, SSE-Feld `gas`; `app/src/shell.c`: `gas set`/`gas clear` (Testbefehle) |

#### Abhängigkeiten

- DSP-REQ-01 (Temperaturblöcke — die Gas-Anzeige folgt unterhalb)
- TMP-REQ-01 (Datenstruktur — Gas-Eintrag als zusätzliches Feld)

#### Abnahmekriterien

- Die Anzeige erscheint unterhalb der vier Temperaturblöcke und ist horizontal zentriert
- Der Wert wird als gerundete Ganzzahl in Prozent dargestellt (`<wert> %`)
- Werte 0–5 % liegen im roten, 5–10 % im gelben, 10–100 % im grünen Bereich des Balkens
- Indikatorposition entspricht dem aktuellen Wert auf der 0..100-%-Skala
- Bei `valid = false` wird `--` angezeigt, Indikator steht am linken Anschlag

---

## 3. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-29 |       | Erstellt: DSP-REQ-01..05 aus `doc/G32_Anzeige_Beschreibung.docx` v1.0 (ohne Simulationsmodus) |
| 1.1     | 2026-05-29 |       | DSP-REQ-01..05 im Webserver umgesetzt (`app/src/webserver.c`: vier Zonenblöcke, Profilauswahl, Farbbalken mit Indikator) |
| 1.2     | 2026-05-29 |       | DSP-REQ-01 auf zweispaltiges 2 × 2-Layout für Landscape-Geräte erweitert; Fallback auf einspaltig bei Fensterbreite < 600 px |
| 1.3     | 2026-05-29 |       | DSP-REQ-06 ergänzt und umgesetzt: Gasflaschen-Füllstand (digital + Farbbalken 0–100 %, Rot/Gelb/Grün); SSE-Feld `gas`, Shell-Befehle `gas set`/`gas clear` |
