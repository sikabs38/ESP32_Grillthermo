/* Workaround fuer Zephyr 4.4 / hal_espressif esp32s3:
 * Der Coexist-Blob (libcoexist.a) referenziert coexist_printf, dessen
 * Quelle (components/esp_coex/src/lib_printf.c) im esp32s3-CMakeLists
 * NICHT im CONFIG_ESP32_SW_COEXIST_ENABLE-Block eingehaengt ist
 * (vorhanden nur im esp32-Build). Solange das Upstream nicht gepatcht
 * ist, liefern wir die Funktion hier nach.
 *
 * Die Funktion wird nur bei aktivierter BT+WiFi-Koexistenz vom Blob
 * aufgerufen; sie sollte das Format ausgeben und die Laenge melden. */

#include <stdarg.h>
#include <stdio.h>
#include <zephyr/sys/printk.h>

int coexist_printf(const char *format, ...);

int coexist_printf(const char *format, ...)
{
    va_list args;
    char    buf[80];
    int     len;

    va_start(args, format);
    len = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    if (len > 0) {
        printk("[coexist] %s", buf);
    }

    return len;
}
