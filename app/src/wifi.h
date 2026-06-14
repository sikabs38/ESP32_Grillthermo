/* WIF-REQ-01, WIF-REQ-02, WIF-REQ-03, WIF-REQ-04, WIF-REQ-05 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <zephyr/kernel.h>
#include "config.h"

#define WIFI_IP_ADDR_STR_LEN (16U)

typedef struct {
    bool connected;
    char ssid[CFG_WIFI_SSID_MAX_LEN + 1U];
    char ip[WIFI_IP_ADDR_STR_LEN];
} Wifi_Status_t;

/* WIF-REQ-04: Neuen Verbindungsversuch ausloesen (z.B. nach Konfigurationsspeicherung) */
void Wifi_Reconnect(void);

/* WIF-REQ-05: Aktuellen Verbindungsstatus liefern */
void Wifi_GetStatus(Wifi_Status_t *status);

/* Blockiert bis zur ersten erfolgreichen WiFi-Verbindung oder Timeout.
 * Gibt true zurueck wenn verbunden, false bei Timeout.
 * Gedacht fuer Module, die erst nach WiFi-Verbindung starten sollen (z.B. Bluetooth). */
bool Wifi_WaitConnected(k_timeout_t timeout);

#endif /* APP_WIFI_H */
