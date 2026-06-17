/* STA-REQ-01..06, STA-NFR-01..03: RGB-LED Statusanzeige */
#include "status.h"
#include "wifi.h"
#include "bluetooth.h"

#include <zephyr/kernel.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/device.h>
#include <zephyr/devicetree.h>

/* STA-NFR-02: Statisch allozierter Stack */
#define STATUS_THREAD_STACK_SIZE  (1024U)
#define STATUS_THREAD_PRIORITY    (10)
/* STA-NFR-03: Abfrageintervall <= 250 ms sichert Reaktionszeit <= 500 ms */
#define STATUS_POLL_MS            (250U)

#define STATUS_LED_NODE  DT_ALIAS(led_strip)

/* STA-REQ-02..05: Farbwerte (RGB) */
static const struct led_rgb COLOR_ORANGE = { .r = 255U, .g = 80U,  .b = 0U   };
static const struct led_rgb COLOR_BLUE   = { .r = 0U,   .g = 0U,   .b = 255U };
static const struct led_rgb COLOR_CYAN   = { .r = 0U,   .g = 255U, .b = 255U };
static const struct led_rgb COLOR_WHITE  = { .r = 255U, .g = 255U, .b = 255U };

static const struct device *g_LedDev = NULL;
static struct led_rgb        g_Pixel[1];

static void Status_SetLed(const struct led_rgb *color)
{
    if (g_LedDev == NULL) {
        return;
    }
    g_Pixel[0] = *color;
    (void)led_strip_update_rgb(g_LedDev, g_Pixel, 1U);
}

/* STA-REQ-06 */
void Status_SetConnectionState(bool wifiConnected, bool btConnected)
{
    const struct led_rgb *color;

    if (wifiConnected && btConnected) {
        color = &COLOR_WHITE;   /* STA-REQ-05: beide verbunden */
    } else if (btConnected) {
        color = &COLOR_BLUE;    /* STA-REQ-03: nur Bluetooth */
    } else if (wifiConnected) {
        color = &COLOR_CYAN;    /* STA-REQ-04: nur WLAN */
    } else {
        color = &COLOR_ORANGE;  /* STA-REQ-02: keine Verbindung */
    }

    Status_SetLed(color);
}

static void Status_Thread(void *p1, void *p2, void *p3)
{
    Wifi_Status_t      wifiSt;
    Bluetooth_Status_t btSt;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* STA-REQ-01: LED initialisieren */
    g_LedDev = DEVICE_DT_GET(STATUS_LED_NODE);
    if (!device_is_ready(g_LedDev)) {
        g_LedDev = NULL;
        return;
    }

    /* Initialer Zustand: keine Verbindung -> Orange */
    Status_SetConnectionState(false, false);

    /* STA-REQ-06: Verbindungszustand abfragen und LED aktualisieren */
    while (true) {
        Wifi_GetStatus(&wifiSt);
        Bluetooth_GetStatus(&btSt);
        Status_SetConnectionState(wifiSt.connected, btSt.connected);
        k_sleep(K_MSEC(STATUS_POLL_MS));
    }
}

/* STA-NFR-02: Statisch allozierter Stack */
K_THREAD_DEFINE(status_thread, STATUS_THREAD_STACK_SIZE,
                Status_Thread, NULL, NULL, NULL,
                STATUS_THREAD_PRIORITY, 0, 0);
