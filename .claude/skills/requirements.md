---
name: req
description: Requirements für eine Firmware-Komponente anlegen oder ergänzen
trigger: /req
args: "<Komponentenname>: <Beschreibung der Anforderungen>"
examples:
  - /req Sensor: Der Sensor soll Temperatur messen und bei Überschreitung von 80°C einen Alarm auslösen.
  - /req Display: Das Display soll den aktuellen Messwert anzeigen und bei Alarm rot aufleuchten.
---

# Skill: req

## Beschreibung

Erstelle oder erweitere eine Requirements-Datei für eine Komponente des Firmware-Projekts.

## Aufruf

```
/req <Komponentenname>: <Beschreibung der Anforderungen>
```

**Beispiele:**
```
/req Sensor: Der Sensor soll Temperatur messen und bei Überschreitung von 80°C einen Alarm auslösen.
/req Display: Das Display soll den aktuellen Messwert anzeigen und bei Alarm rot aufleuchten.
```

## Wann verwenden

TRIGGER: Nutzer ruft `/req` auf oder möchte Anforderungen für eine Komponente erfassen bzw. ergänzen.

SKIP: Allgemeine Fragen, Code-Implementierung ohne explizite Anforderungserfassung.

## Ablauf

1. Komponentenname aus dem Aufruf übernehmen
2. Präfix aus den ersten drei Buchstaben ableiten (z.B. `Sensor` → `SEN`)
3. Datei unter `requirements/<komponente>.md` anlegen oder öffnen
4. Nächste freie ID vergeben nach Schema `<PRÄFIX>-REQ-<NR>` für funktionale (z.B. `SEN-REQ-01`) und `<PRÄFIX>-NFR-<NR>` für nicht-funktionale Anforderungen (z.B. `SEN-NFR-01`) — jeweils eigener, zweistelliger Zähler
5. Anforderungen in funktionale und nicht-funktionale aufteilen
6. Abnahmekriterien für jede Anforderung formulieren
7. Datei speichern und Pfad bestätigen

## Ausgabe-Dateistruktur

```markdown
# Requirements: <Komponentenname>

## 1. Übersicht

Kurze Beschreibung der Komponente und ihres Zwecks.

## 2. Funktionale Anforderungen

### <XXX-REQ-01>

#### Beschreibung

Erstelle hier die Beschreibung des Requirements.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Offen  |                 |

#### Abhängigkeiten

Wenn dieses Requirement von anderen abhängt, trage hier einen Verweis auf dieses Requirement ein.

#### Abnahmekriterien

Beschreibe hier die Abnahmekriterien.

## 3. Nicht-funktionale Anforderungen

### <XXX-NFR-01>

#### Beschreibung

Erstelle hier die Beschreibung des Requirements.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Mittel    | Performance | Offen  |                 |

#### Abhängigkeiten

Wenn dieses Requirement von anderen abhängt, trage hier einen Verweis auf dieses Requirement ein.

#### Abnahmekriterien

Beschreibe hier die Abnahmekriterien.

## 4. Offene Punkte / Annahmen

- [ ] ...

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | YYYY-MM-DD |       | Erstellt |
```

## Referenzwerte

**Prioritäten:**
- **Hoch** – muss erfüllt sein (Must-have)
- **Mittel** – sollte erfüllt sein (Should-have)
- **Niedrig** – kann erfüllt sein (Nice-to-have)

**Status-Werte:**
- **Offen** – noch nicht begonnen
- **In Bearbeitung** – Implementierung läuft
- **Umgesetzt** – im Code implementiert, noch nicht verifiziert
- **Verifiziert** – getestet und abgenommen

**NFR-Kategorien:** Performance, Genauigkeit, Zuverlässigkeit, Umgebung, Sicherheit, Wartbarkeit

## Hinweise

- Jede Anforderung bekommt eine eindeutige ID – diese wird später im Code und in Tests referenziert
- Nicht-funktionale Anforderungen erhalten das Kürzel `NFR` statt `REQ`
- Existiert die Datei bereits, werden neue Anforderungen an die bestehende Tabelle **angehängt**
- Offene Punkte und Annahmen immer explizit festhalten
- In der Änderungshistorie das aktuelle Datum (ISO 8601, z.B. `2026-05-20`) eintragen, nicht den Platzhalter `YYYY-MM-DD`
