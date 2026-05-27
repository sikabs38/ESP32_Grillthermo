/* CFG-REQ-01, CFG-REQ-02, CFG-REQ-04, CFG-REQ-06: Konfigurationsspeicher-API */
#ifndef APP_CONFIG_H
#define APP_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

#define CFG_WIFI_SSID_MAX_LEN   (32U)
#define CFG_WIFI_PASS_MAX_LEN   (64U)
#define CFG_MQTT_BROKER_MAX_LEN (128U)
#define CFG_MQTT_PORT_DEFAULT   (1883U)
#define CFG_PIN_MIN_LEN         (4U)
#define CFG_PIN_MAX_LEN         (6U)
#define CFG_PIN_BUF_SIZE        (CFG_PIN_MAX_LEN + 1U)
#define CFG_PIN_DEFAULT         "000000"

/* CFG-REQ-04: Konfigurationsparameter */
typedef struct {
    char     wifiSsid[CFG_WIFI_SSID_MAX_LEN + 1U];
    char     wifiPassword[CFG_WIFI_PASS_MAX_LEN + 1U]; /* TODO: CFG-REQ-05 AES */
    char     mqttBroker[CFG_MQTT_BROKER_MAX_LEN + 1U];
    uint16_t mqttPort;
    char     pin[CFG_PIN_BUF_SIZE];                    /* TODO: CFG-REQ-05 AES */
} Config_Data_t;

int  Config_Init(void);
int  Config_Load(Config_Data_t *data);
int  Config_Save(const Config_Data_t *data);
void Config_GetDefaults(Config_Data_t *data);
int  Config_InvalidateAll(void);

#endif /* APP_CONFIG_H */
