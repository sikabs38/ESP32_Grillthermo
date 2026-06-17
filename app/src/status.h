/* STA-REQ-06..08: API fuer RGB-LED-Statusanzeige */
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <stdbool.h>
#include <stdint.h>

/* STA-REQ-07: 10 logarithmische Helligkeitsstufen (0 = minimal, 9 = 100 %) */
#define STATUS_BRIGHTNESS_STEPS    (10U)
/* STA-REQ-07: Standard-Helligkeit Stufe 6 = 64/255 = 25 % */
#define STATUS_BRIGHTNESS_DEFAULT  (6U)

/* STA-REQ-06: Verbindungsstatus melden und LED sofort aktualisieren.
 * Thread-sicher; Reaktionszeit <= 500 ms (STA-NFR-03). */
void Status_SetConnectionState(bool wifiConnected, bool btConnected);

/* STA-REQ-08: Helligkeitsstufe setzen (0..STATUS_BRIGHTNESS_STEPS-1). */
void Status_SetBrightness(uint8_t step);

/* STA-REQ-08: Aktuelle Helligkeitsstufe abfragen. */
uint8_t Status_GetBrightness(void);

#endif /* APP_STATUS_H */
