#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(grill_buddy, LOG_LEVEL_INF);

int main(void)
{
    /* SHL-REQ-01: USB CDC-ACM wird automatisch durch
     * CONFIG_CDC_ACM_SERIAL_INITIALIZE_AT_BOOT initialisiert */
    LOG_INF("Grill Buddy starting");

    return 0;
}
