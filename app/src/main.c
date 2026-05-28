/* SHL-REQ-01: USB CDC-ACM wird automatisch durch
 * CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT initialisiert.
 * SHL-REQ-07: Bootmeldung und DTR-Erkennung in shell.c */
#include "webserver.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(grill_buddy, LOG_LEVEL_ERR);

int main(void)
{
    LOG_INF("Grill Buddy gestartet.");
    Webserver_Start();
    return 0;
}
