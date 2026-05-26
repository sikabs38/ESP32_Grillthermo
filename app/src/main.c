#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/usb/usb_device.h>

LOG_MODULE_REGISTER(grill_buddy, LOG_LEVEL_INF);

int main(void)
{
    int ret;

    /* SHL-REQ-01: USB-Stack aktivieren — Shell via CDC-ACM erreichbar */
    ret = usb_enable(NULL);
    if (ret != 0) {
        LOG_ERR("USB enable failed: %d", ret);
        return ret;
    }

    LOG_INF("Grill Buddy starting");

    return 0;
}
