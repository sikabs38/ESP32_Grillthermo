# Requirements: Anzeige der Temperaturblöcke

## 1. Übersicht

Die Webanzeige stellt vier Temperaturblöcke dar — einen pro Grillzone. Jeder Block besteht aus zwei nebeneinander angeordneten Anzeigefeldern: Garraumtemperatur und Kerntemperatur des Grillguts. Beide Felder sind identisch aufgebaut und folgen demselben visuellen Schema. Das Grillgut-Profil ist pro Zone unabhängig einstellbar (DSP-REQ-07), sodass z. B. in Zone 1 Rind und in Zone 2 Geflügel gleichzeitig überwacht werden können. Unterhalb der vier Blöcke wird der Gasflaschen-Füllstand zentriert dargestellt (DSP-REQ-06).

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

Jede Kerntemperatur-Anzeige zeigt die Innentemperatur des Grillguts in der jeweiligen Zone. Wertebereich und Farbzonen sind abhängig vom für diese Zone gewählten Grillgut-Profil (siehe DSP-REQ-04 für den Profil-Katalog und DSP-REQ-07 für die zonenweise Auswahl).

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
- DSP-REQ-04 (Grillgut-Profile als Datenkatalog)
- DSP-REQ-07 (zonenweise Profilauswahl)
- TMP-REQ-01 (`core[i]`-Werte)

#### Abnahmekriterien

- Numerischer Wert wird als gerundete Ganzzahl in °C dargestellt
- Farbbalken bildet den Min..Max-Bereich des für diese Zone gewählten Profils ab
- Garstufenname entspricht der zum aktuellen Wert gehörenden Stufe des für diese Zone gewählten Profils
- Legende zeigt alle Garstufen des für diese Zone gewählten Profils mit deren Farben

---

### DSP-REQ-04 — Grillgut-Profile (Katalog)

#### Beschreibung

Das System stellt vier Grillgut-Profile als Datenkatalog bereit. Jedes Profil definiert seinen Wertebereich (Min..Max) sowie eine Liste von Garstufen mit zugehörigen Temperaturgrenzen und Farben für die Darstellung in der Kerntemperatur-Anzeige (DSP-REQ-03). Die Auswahl, welches Profil in welcher Zone aktiv ist, regelt DSP-REQ-07.

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
| Hoch | Umgesetzt | `app/src/webserver.c`: JS-Objekt `PR` mit `rind`/`schwein`/`gefluegel`/`fisch` (Datenkatalog). Die globale Chip-Leiste `k_HtmlProfiles` und `setPr()` für eine zonenübergreifende Auswahl entfallen mit der Umsetzung von DSP-REQ-07. |

#### Abhängigkeiten

- DSP-REQ-03 (Kerntemperatur-Anzeige)

#### Abnahmekriterien

- Es existieren genau die vier Profile Rind, Schwein, Geflügel und Fisch mit den oben tabellierten Garstufen, Temperaturgrenzen und Farben
- Die Profil-Daten sind im Frontend als unveränderlicher Datenkatalog verfügbar (z. B. als JavaScript-Konstante), getrennt von der Auswahl-Logik (DSP-REQ-07)
- Garstufengrenzen und Farben entsprechen exakt den obigen Tabellen

---

### DSP-REQ-05 — Verhalten bei Über-/Unterschreitung

#### Beschreibung

Bei Kerntemperaturwerten außerhalb des Bereichs des für die jeweilige Zone gewählten Profils (DSP-REQ-07) verhält sich die Anzeige wie folgt:

- Werte unterhalb des Profilminimums: Indikator steht am linken Anschlag, kein Zonenname.
- Werte oberhalb des Profilmaximums: Indikator steht am rechten Anschlag, letzte Garstufe wird angezeigt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel | Umgesetzt | `app/src/webserver.c`: `up()`-Funktion im `k_HtmlScript` (Zweige `e.v<p.mn` → `pos=0` ohne Stufe, `e.v>p.mx` → `pos=100` mit `arr[arr.length-1].n`) |

#### Abhängigkeiten

- DSP-REQ-03 (Kerntemperatur-Anzeige)
- DSP-REQ-04 (Grillgut-Profile als Datenkatalog)
- DSP-REQ-07 (zonenweise Profilauswahl)

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

### DSP-REQ-07 — Zonenspezifische Profilauswahl

#### Beschreibung

Die Auswahl des Grillgut-Profils für die Kerntemperatur-Anzeige soll für jede der vier Zonen unabhängig erfolgen. Damit lassen sich gleichzeitig unterschiedliche Grillgüter überwachen — z. B. Zone 1 „Rind", Zone 2 „Schwein", Zone 3 „Geflügel", Zone 4 „Fisch".

Die Auswahl erfolgt durch ein kompaktes Bedienelement direkt im jeweiligen Zonenblock (z. B. eine kleine Chip-Leiste oder ein Auswahl-Dropdown). Wechselt der Nutzer das Profil einer Zone, aktualisieren sich Skala, Garstufen-Legende, Indikatorposition und Garstufen-Text **nur dieser einen Zone** sofort; die übrigen drei Zonen bleiben unverändert.

Die getroffene Auswahl wird im Browser persistiert (HTML5 `localStorage`), sodass sie nach einem Reload oder einem späteren Aufruf von einem anderen Tab desselben Browsers weiterhin aktiv ist. Bei erstem Aufruf — also wenn für einen Schlüssel kein gespeicherter Wert vorliegt — wird die betreffende Zone auf das Standardprofil „Rind" gesetzt. Die Persistenz ist bewusst rein client-seitig; es findet keine server-seitige Speicherung in `Config_Data_t` und kein Sync zwischen verschiedenen Browsern statt.

Mit der Umsetzung dieser Anforderung entfällt die ursprünglich in DSP-REQ-04 beschriebene globale Chip-Leiste oberhalb der Blöcke.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/webserver.c`: Fragmente `k_ZoneProfA`/`k_ZoneProfB` und `k_DsClose` (Per-Zone-Chip-Bar pro Block); JS `cp[4]`, `applyPr(z,n)` (rein visuell), `setPr(z,n)` (mit `localStorage.setItem('gt_profile_z'+z, …)`), Init-Loop liest `localStorage` mit Default `'rind'`, Per-Chip-Click via `closest('.pr').dataset.z`; SSE wendet `PR[cp[i]]` pro Zone an |

#### Abhängigkeiten

- DSP-REQ-01 (Vier Temperaturblöcke)
- DSP-REQ-03 (Kerntemperatur-Anzeige)
- DSP-REQ-04 (Grillgut-Profile als Datenkatalog)

#### Abnahmekriterien

- Jede der vier Zonen besitzt im eigenen Block ein eigenes Bedienelement zur Profilauswahl mit den vier Optionen Rind, Schwein, Geflügel und Fisch
- Die globale Auswahl-Leiste oberhalb der Blöcke aus der ursprünglichen DSP-REQ-04 ist entfernt
- Wechsel des Profils einer Zone aktualisiert ausschließlich diese Zone (Skala, Garstufe, Legende, Indikator); die anderen drei Zonen bleiben in Anzeige und gewähltem Profil unverändert
- Die Auswahl pro Zone wird im `localStorage` des Browsers unter eindeutigen Schlüsseln pro Zone gespeichert (z. B. `gt_profile_z0`..`gt_profile_z3`)
- Beim Laden der Seite werden die gespeicherten Profile aus `localStorage` gelesen und angewendet; fehlende oder ungültige Einträge werden auf das Standardprofil „Rind" gesetzt
- Die Profilauswahl bleibt eine reine Browser-Einstellung — keine Änderung an `Config_Data_t`, kein NVS-Schreibzugriff, kein Cross-Browser-Sync
- SSE-Updates (WEB-REQ-06/07) bleiben unverändert: der Server liefert weiterhin nur die Messwerte, das Frontend mappt sie auf das jeweils zonenspezifisch gewählte Profil

---

## 3. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-29 |       | Erstellt: DSP-REQ-01..05 aus `doc/G32_Anzeige_Beschreibung.docx` v1.0 (ohne Simulationsmodus) |
| 1.1     | 2026-05-29 |       | DSP-REQ-01..05 im Webserver umgesetzt (`app/src/webserver.c`: vier Zonenblöcke, Profilauswahl, Farbbalken mit Indikator) |
| 1.2     | 2026-05-29 |       | DSP-REQ-01 auf zweispaltiges 2 × 2-Layout für Landscape-Geräte erweitert; Fallback auf einspaltig bei Fensterbreite < 600 px |
| 1.3     | 2026-05-29 |       | DSP-REQ-06 ergänzt und umgesetzt: Gasflaschen-Füllstand (digital + Farbbalken 0–100 %, Rot/Gelb/Grün); SSE-Feld `gas`, Shell-Befehle `gas set`/`gas clear` |
| 1.4     | 2026-05-30 |       | DSP-REQ-07 ergänzt: zonenspezifische Profilauswahl (per-Zone-Chip/Dropdown im Block, localStorage-Persistenz, Default „Rind"). DSP-REQ-04 auf Datenkatalog reduziert (globale Chip-Leiste entfällt mit DSP-REQ-07). DSP-REQ-03 und DSP-REQ-05 entsprechend referenziert. Übersicht aktualisiert. |
| 1.5     | 2026-05-30 |       | DSP-REQ-07 umgesetzt: Per-Zone-Chip-Bar im Block (`k_ZoneProfA/B`), JS `cp[4]`, `applyPr/setPr`, `localStorage`-Persistenz mit Default „Rind", SSE wendet pro Zone `PR[cp[i]]` an |
