/* CFG-REQ-01, CFG-REQ-02, CFG-REQ-04, CFG-REQ-06: Konfigurationsspeicher-API */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define CFG_WIFI_SSID_MAX_LEN      (32U)
#define CFG_WIFI_PASS_MAX_LEN      (64U)
#define CFG_WIFI_HOSTNAME_MAX_LEN  (63U)  /* WIF-REQ-06: Zephyr NET_HOSTNAME_MAX_LEN (range 1-63) */
#define CFG_MQTT_BROKER_MAX_LEN    (128U)
#define CFG_MQTT_PASS_MAX_LEN      (64U)
#define CFG_MQTT_PORT_DEFAULT      (1883U)
#define CFG_PIN_MIN_LEN            (4U)
#define CFG_PIN_MAX_LEN            (6U)
#define CFG_PIN_BUF_SIZE           (CFG_PIN_MAX_LEN + 1U)
#define CFG_PIN_DEFAULT            "000000"
/* BLE-REQ-07: MAC des gekoppelten Otto-Wilde-G32 als Zeichenkette
 * "AA:BB:CC:DD:EE:FF" (17 Zeichen + Nullterminator). Leer = keine Kopplung. */
#define CFG_GRILL_MAC_STR_LEN      (17U)
#define CFG_GRILL_MAC_BUF_SIZE     (CFG_GRILL_MAC_STR_LEN + 1U)

/* CFG-REQ-04: Konfigurationsparameter */
typedef struct {
    char     wifiSsid[CFG_WIFI_SSID_MAX_LEN + 1U];
    char     wifiPassword[CFG_WIFI_PASS_MAX_LEN + 1U]; /* TODO: CFG-REQ-05 AES */
    char     wifiHostname[CFG_WIFI_HOSTNAME_MAX_LEN + 1U]; /* WIF-REQ-06 */
    char     mqttBroker[CFG_MQTT_BROKER_MAX_LEN + 1U];
    uint16_t mqttPort;
    char     pin[CFG_PIN_BUF_SIZE];                    /* TODO: CFG-REQ-05 AES */
    char     grillMac[CFG_GRILL_MAC_BUF_SIZE];         /* BLE-REQ-07 */
    /* Felder, die nach einer Struct-Erweiterung hinzugefuegt wurden, werden ans Ende
     * gestellt, damit aeltere NVS-Datensaetze rueckwaertskompatibel lesbar bleiben. */
    char     mqttPassword[CFG_MQTT_PASS_MAX_LEN + 1U];
} Config_Data_t;

int  Config_Init(void);
int  Config_Load(Config_Data_t *data);
int  Config_Save(const Config_Data_t *data);
void Config_GetDefaults(Config_Data_t *data);
int  Config_InvalidateAll(void);

#endif /* APP_CONFIG_H */
