/* MQT-REQ-01, MQT-REQ-04: MQTT-Verbindungsmanagement */
#ifndef APP_MQTT_H
#define APP_MQTT_H

#include <stdbool.h>
#include "config.h"

typedef struct {
    bool connected;
    char broker[CFG_MQTT_BROKER_MAX_LEN + 1U];
} Mqtt_Status_t;

/* MQT-REQ-04: Aktuellen MQTT-Verbindungsstatus liefern */
void Mqtt_GetStatus(Mqtt_Status_t *status);

/* MQT-REQ-01: Sofortigen Wiederverbindungsversuch ausloesen */
void Mqtt_Reconnect(void);

#endif /* APP_MQTT_H */
