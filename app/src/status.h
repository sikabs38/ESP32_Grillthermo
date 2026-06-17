/* STA-REQ-06: Verbindungsstatus-API fuer RGB-LED-Statusanzeige */
#ifndef APP_STATUS_H
#define APP_STATUS_H

#include <stdbool.h>

/* STA-REQ-06: Verbindungsstatus melden und LED sofort aktualisieren.
 * Thread-sicher; Reaktionszeit <= 500 ms (STA-NFR-03). */
void Status_SetConnectionState(bool wifiConnected, bool btConnected);

#endif /* APP_STATUS_H */
