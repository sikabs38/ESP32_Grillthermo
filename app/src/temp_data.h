/* TMP-REQ-01, TMP-REQ-02 */
#ifndef APP_TEMP_DATA_H
#define APP_TEMP_DATA_H

#include <stdint.h>
#include <stdbool.h>
#include <zephyr/kernel.h>

/* TMP-REQ-01: Anzahl Zonen */
#define TEMP_ZONE_COUNT (4U)

/* TMP-REQ-01: Einzelner Temperaturwert mit Gueltigkeitsflag.
 * valid = false bedeutet "nicht verfuegbar" → Anzeige "--".
 * Ein Wert von 0 ist ein gueltiger Messwert (kein Sentinel). */
typedef struct {
    int16_t value; /* Temperatur in °C */
    bool    valid; /* false: Wert nicht verfuegbar */
} Temp_Entry_t;

/* TMP-REQ-01: Gesamtstruktur aller Messwerte. Trotz des Namens "Temp" enthaelt
 * die Struct auch nicht-temperaturartige Sensordaten (z.B. Gasfuellstand), die
 * unter demselben Mutex/Condvar bereitgestellt werden. */
typedef struct {
    Temp_Entry_t burner[TEMP_ZONE_COUNT]; /* Brennertemperaturen (°C) */
    Temp_Entry_t core[TEMP_ZONE_COUNT];   /* Kerntemperaturen (°C, Fleischthermometer) */
    Temp_Entry_t gas;                     /* DSP-REQ-06: Gasflaschen-Fuellstand (%) */
} Temp_Data_t;

/* TMP-REQ-02: Global verfuegbare Datenstruktur und Mutex */
extern struct k_mutex g_TempMutex;
extern Temp_Data_t    g_TempData;

/* TMP-REQ-03: Generationszaehler und Condvar zur Aenderungsbenachrichtigung.
 * g_TempGen wird bei jeder Wertaenderung (unter g_TempMutex) erhoeht; Warter
 * auf g_TempCondvar (z.B. der SSE-Handler) werden geweckt. */
extern struct k_condvar g_TempCondvar;
extern uint32_t         g_TempGen;

/* TMP-REQ-03: Gruppenindex fuer Temp_Set() */
#define TEMP_GROUP_BURNER (0U)
#define TEMP_GROUP_CORE   (1U)

/* TMP-REQ-03: Einzelnen Messwert setzen, Generationszaehler erhoehen und Warter
 * wecken — alles atomar unter g_TempMutex.
 * group: TEMP_GROUP_*, zone: 0..TEMP_ZONE_COUNT-1.
 * Rueckgabe: 0 bei Erfolg, -EINVAL bei ungueltigem group/zone. */
int Temp_Set(uint8_t group, uint8_t zone, int16_t value, bool valid);

/* TMP-REQ-03: Aenderung signalisieren, ohne einen Wert zu setzen — fuer den Fall,
 * dass ein Erzeuger g_TempData direkt unter Mutex aktualisiert und danach genau
 * einmal eine Sammel-Benachrichtigung ausloesen moechte. */
void Temp_NotifyChanged(void);

/* DSP-REQ-06: Gasflaschen-Fuellstand setzen (Wert in Prozent 0..100, atomar unter
 * g_TempMutex; erhoeht g_TempGen und weckt Warter).
 * percent: 0..100 (bei valid = true erforderlich), ignoriert bei valid = false.
 * Rueckgabe: 0 bei Erfolg, -EINVAL bei (valid && percent ausserhalb 0..100). */
int Temp_SetGas(int16_t percent, bool valid);

#endif /* APP_TEMP_DATA_H */
