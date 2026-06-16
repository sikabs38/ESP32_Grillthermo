/* SHL-REQ-01: USB CDC-ACM wird automatisch durch
 * CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT initialisiert.
 * SHL-REQ-07: Bootmeldung und DTR-Erkennung in shell.c */
#include "webserver.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(esp32_grillthermo, LOG_LEVEL_ERR);

int main(void)
{
    Config_Data_t cfg;

    LOG_INF("ESP32 Grillthermo gestartet.");

    /* CFG-REQ-07 / ADR-001: Webserver nur starten wenn Netzwerkmodus webserver.
     * Config_Init() wurde bereits via SYS_INIT (APPLICATION, 0) ausgefuehrt. */
    if (Config_Load(&cfg) != 0) {
        Config_GetDefaults(&cfg);
    }

    if (cfg.networkMode == CFG_NETWORK_WEBSERVER) {
        Webserver_Start();
    }

    return 0;
}
