---
name: adr
description: Architecture Decision Record anlegen oder aktualisieren
trigger: /adr
args: "<Titel der Entscheidung>"
examples:
  - /adr RTOS-Auswahl: Zephyr OS statt FreeRTOS
  - /adr USB-Stack: Verzicht auf Zephyr UCPD-Treiber zugunsten direkter STM32 LL-API
---

# Skill: adr

## Beschreibung

Erstelle ein neues Architecture Decision Record (ADR) für das Projekt.
ADRs dokumentieren wichtige Architektur- und Designentscheidungen dauerhaft und nachvollziehbar.

## Aufruf

```
/adr <Titel der Entscheidung>
```

**Beispiele:**
```
/adr RTOS-Auswahl: Zephyr OS statt FreeRTOS
/adr USB-Stack: Verzicht auf Zephyr UCPD-Treiber zugunsten direkter STM32 LL-API
```

## Wann verwenden

TRIGGER: Nutzer ruft `/adr` auf oder möchte eine Architekturentscheidung dokumentieren.

SKIP: Requirements (`/req`), Code-Reviews oder allgemeine Fragen ohne Entscheidungscharakter.

## Ablauf

1. Nächste freie ADR-Nummer ermitteln: vorhandene Dateien in `doc/adr/` prüfen, höchste Nummer +1
2. Falls `doc/adr/` nicht existiert, Verzeichnis anlegen
3. Dateiname: `doc/adr/<NNNN>-<kebab-case-titel>.md` (vierstellige Nummer, z.B. `0001-rtos-auswahl.md`)
4. ADR mit dem Template befüllen — alle Abschnitte ausfüllen, keine Platzhalter stehen lassen
5. Status auf `Proposed` setzen, Datum auf heute (ISO 8601), Autor und Genehmiger auf `Siegfried Kamlah`
6. CLAUDE.md-Abschnitt `## ADRs` ergänzen oder anlegen mit Verweis auf die neue Datei (falls noch kein Abschnitt vorhanden)
7. Pfad der erstellten Datei bestätigen

## Nummerierungsschema

- Nummern sind vierstellig und fortlaufend: `0001`, `0002`, …
- Keine Kategorisierung durch Nummernbereiche — rein chronologisch
- Lücken in der Nummerierung sind erlaubt (z.B. wenn ein ADR zurückgezogen wurde)

## Template

```markdown
# [ADR-XXXX] Title of Decision

**Status**: Proposed | Accepted | Deprecated | Superseded by ADR-XXXX
**Last Updated**: YYYY-MM-DD
**Author**: Siegfried Kamlah
**Approved by**: Siegfried Kamlah

## Context
Explain the background and why a decision is being made. Include constraints, business requirements, or technical challenges.

## Decision
Clearly describe the decision made and justify why.

## Consequences
Describe the positive and negative effects of this decision.

## Alternatives Considered
List and briefly explain other options that were evaluated.

## Compliance
Explain how compliance for this ADR will be achieved (e.g., CI checks, static analysis, traceability reports). Preference is for automation wherever possible.

## Related ADRs
- [ADR-0102: RTOS vs Baremetal](../embedded/0102-rtos-vs-baremetal.md)
- [ADR-0202: Communication Protocol](../shared/0202-communication-protocol.md)
```

## Hinweise

- Alle Abschnitte sind Pflicht — leere Abschnitte mit „Keine." kennzeichnen
- `## Related ADRs` darf entfallen, wenn keine verwandten ADRs existieren
- Status-Übergänge: `Proposed` → `Accepted` → `Deprecated` oder `Superseded by ADR-XXXX`
- Bestehende ADRs nie inhaltlich ändern — stattdessen neues ADR anlegen und altes auf `Superseded` setzen
- In der Änderungshistorie das aktuelle Datum (ISO 8601) eintragen, nicht den Platzhalter `YYYY-MM-DD`
