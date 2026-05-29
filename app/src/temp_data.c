/* TMP-REQ-01, TMP-REQ-02, TMP-REQ-03 */
#include "temp_data.h"

#include <errno.h>

/* TMP-REQ-02: Statisch initialisierter Mutex (kein Init-Aufruf noetig) */
K_MUTEX_DEFINE(g_TempMutex);

/* TMP-REQ-03: Statisch initialisierte Condvar und Generationszaehler */
K_CONDVAR_DEFINE(g_TempCondvar);
uint32_t g_TempGen = 0U;

/* TMP-REQ-01: Globale Temperaturdaten mit Initialisierungswerten.
 * Brenner: 20 °C / valid, Kern: 0 °C / ungueltig (→ Anzeige "--"). */
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
};

/* TMP-REQ-03: Einzelwert setzen, Generation erhoehen, Warter wecken */
int Temp_Set(uint8_t group, uint8_t zone, int16_t value, bool valid)
{
    Temp_Entry_t *arr;

    if (zone >= (uint8_t)TEMP_ZONE_COUNT) {
        return -EINVAL;
    }

    (void)k_mutex_lock(&g_TempMutex, K_FOREVER);

    switch (group) {
    case TEMP_GROUP_BURNER:
        arr = g_TempData.burner;
        break;
    case TEMP_GROUP_CORE:
        arr = g_TempData.core;
        break;
    default:
        (void)k_mutex_unlock(&g_TempMutex);
        return -EINVAL;
    }

    arr[zone].value = value;
    arr[zone].valid = valid;
    g_TempGen++;
    (void)k_condvar_broadcast(&g_TempCondvar);

    (void)k_mutex_unlock(&g_TempMutex);

    return 0;
}

/* TMP-REQ-03: Sammel-Benachrichtigung ohne Wertaenderung */
void Temp_NotifyChanged(void)
{
    (void)k_mutex_lock(&g_TempMutex, K_FOREVER);
    g_TempGen++;
    (void)k_condvar_broadcast(&g_TempCondvar);
    (void)k_mutex_unlock(&g_TempMutex);
}
