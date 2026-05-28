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

/* TMP-REQ-01: Gesamtstruktur aller Messwerte */
typedef struct {
    Temp_Entry_t burner[TEMP_ZONE_COUNT]; /* Brennertemperaturen */
    Temp_Entry_t core[TEMP_ZONE_COUNT];   /* Kerntemperaturen (Fleischthermometer) */
    Temp_Entry_t target[TEMP_ZONE_COUNT]; /* Zieltemperaturen */
} Temp_Data_t;

/* TMP-REQ-02: Global verfuegbare Datenstruktur und Mutex */
extern struct k_mutex g_TempMutex;
extern Temp_Data_t    g_TempData;

#endif /* APP_TEMP_DATA_H */
