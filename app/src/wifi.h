/* WIF-REQ-01, WIF-REQ-02, WIF-REQ-03, WIF-REQ-04, WIF-REQ-05, WIF-REQ-08..11 */
#ifndef APP_WIFI_H
#define APP_WIFI_H

#include <stdbool.h>
#include <stdint.h>
#include <zephyr/kernel.h>
#include <zephyr/net/wifi.h>
#include "config.h"

#define WIFI_IP_ADDR_STR_LEN   (16U)
/* WIF-REQ-09: Maximale Anzahl gepufferter Scan-Ergebnisse */
#define WIFI_SCAN_MAX_RESULTS  (16U)

typedef struct {
    bool connected;
    char ssid[CFG_WIFI_SSID_MAX_LEN + 1U];
    char ip[WIFI_IP_ADDR_STR_LEN];
} Wifi_Status_t;

/* WIF-REQ-08/09: Ein gepuffertes Scan-Ergebnis */
typedef struct {
    char    ssid[WIFI_SSID_MAX_LEN + 1U];
    int8_t  rssi;
    uint8_t security;
} Wifi_ScanResult_t;

/* WIF-REQ-09: Callback, der nach Scan-Abschluss aufgerufen wird.
 * count == 0 bedeutet: kein Netzwerk gefunden. */
typedef void (*Wifi_ScanDoneCb_t)(uint8_t count,
                                   const Wifi_ScanResult_t *results,
                                   void *user_data);

/* WIF-REQ-04: Neuen Verbindungsversuch ausloesen (z.B. nach Konfigurationsspeicherung) */
void Wifi_Reconnect(void);

/* WIF-REQ-05: Aktuellen Verbindungsstatus liefern */
void Wifi_GetStatus(Wifi_Status_t *status);

/* Blockiert bis zur ersten erfolgreichen WiFi-Verbindung oder Timeout.
 * Gibt true zurueck wenn verbunden, false bei Timeout.
 * Gedacht fuer Module, die erst nach WiFi-Verbindung starten sollen (z.B. Bluetooth). */
bool Wifi_WaitConnected(k_timeout_t timeout);

/* WIF-REQ-08: Scan starten. cb wird bei Abschluss aus dem net_mgmt-Thread aufgerufen.
 * Gibt 0 bei Erfolg, -EBUSY wenn bereits ein Scan laeuft, -EIO bei Fehler. */
int Wifi_Scan(Wifi_ScanDoneCb_t cb, void *user_data);

/* WIF-REQ-09: Sicherheitstyp als lesbare Zeichenkette */
const char *Wifi_SecurityStr(uint8_t security);

#endif /* APP_WIFI_H */
