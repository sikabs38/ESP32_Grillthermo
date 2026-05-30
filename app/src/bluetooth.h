/* BLE-REQ-01..10: Bluetooth-Kommunikation mit Otto Wilde G32 */
#ifndef APP_BLUETOOTH_H
#define APP_BLUETOOTH_H

#include <stdint.h>
#include <stdbool.h>
#include "config.h"

/* BLE-REQ-10: Bluetooth-Statusstruktur fuer externe Module (Shell/LED) */
typedef struct {
    bool     connected;                            /* GATT-Verbindung mit Notify aktiv */
    bool     paired;                               /* Grill-MAC im Konfigurationsspeicher hinterlegt */
    char     peerMac[CFG_GRILL_MAC_BUF_SIZE];      /* gekoppelte MAC oder leer */
    uint32_t lastPacketUptimeMs;                   /* Uptime (ms) des letzten Notify-Pakets */
} Bluetooth_Status_t;

/* BLE-REQ-03: Scan starten.
 * filterByG32 = true: nur Geraete mit Advertising-Name "OWG-G32C" oder G32-Service-UUID
 * filterByG32 = false: alle empfangenen Geraete (Diagnose, "bt scan all").
 * Scan endet automatisch nach BLUETOOTH_SCAN_TIMEOUT_MS oder per Bluetooth_ScanStop(). */
int Bluetooth_ScanStart(bool filterByG32);

/* BLE-REQ-03: Laufenden Scan vorzeitig beenden. */
int Bluetooth_ScanStop(void);

/* BLE-REQ-04..08: Verbindungsaufbau zur gespeicherten Grill-MAC ausloesen.
 * Liest die aktuelle Konfiguration; bei leerer MAC nur Hinweis-Log, kein Fehler. */
int Bluetooth_Reconnect(void);

/* BLE-REQ-03: Aktive GATT-Verbindung zum Grill trennen. */
int Bluetooth_Disconnect(void);

/* BLE-REQ-10: Aktuellen Status abfragen. */
void Bluetooth_GetStatus(Bluetooth_Status_t *status);

/* Diagnose: Sniff-Modus ein-/ausschalten. Bei aktivem Sniff wird jede
 * empfangene Notify-Payload als Hex-Dump plus aktuell decodierter Belegung
 * (Brenner/Kern/Gas mit Byte-Offsets) auf die Konsole geschrieben. */
void Bluetooth_SetSniff(bool enable);
bool Bluetooth_GetSniff(void);

#endif /* APP_BLUETOOTH_H */
