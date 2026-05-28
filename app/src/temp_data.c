/* TMP-REQ-01, TMP-REQ-02 */
#include "temp_data.h"

/* TMP-REQ-02: Statisch initialisierter Mutex (kein Init-Aufruf noetig) */
K_MUTEX_DEFINE(g_TempMutex);

/* TMP-REQ-01: Globale Temperaturdaten mit Initialisierungswerten.
 * Brenner: 20 °C / valid, Kern und Ziel: 0 °C / ungueltig (→ Anzeige "--"). */
Temp_Data_t g_TempData = {
    .burner = {
        { .value = (int16_t)20, .valid = true  },
        { .value = (int16_t)20, .valid = true  },
        { .value = (int16_t)20, .valid = true  },
        { .value = (int16_t)20, .valid = true  },
    },
    .core = {
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
    },
    .target = {
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
        { .value = (int16_t)0, .valid = false },
    },
};
