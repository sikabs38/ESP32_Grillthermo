/* BLE-REQ-01..10, BLE-NFR-01..04: Bluetooth-Kommunikation mit Otto Wilde G32 */
#include "bluetooth.h"
#include "config.h"
#include "temp_data.h"
#include "wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/printk.h>
#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/hci.h>
#include <zephyr/bluetooth/conn.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/sys/byteorder.h>
#include <string.h>
#include <stddef.h>
#include <errno.h>

LOG_MODULE_REGISTER(bluetooth_app, LOG_LEVEL_INF);

/* BLE-NFR-02: Statisch allozierter Thread-Stack */
#define BT_THREAD_STACK_SIZE      (4096U)
#define BT_THREAD_PRIORITY        (7)

/* BLE-REQ-03: Scan-Timeout (60 s — erhoeht wegen reduziertem Scan-Tastverhältnis) */
#define BT_SCAN_TIMEOUT_MS        (60000U)
/* BLE-REQ-03: Maximale Anzahl unterschiedlicher Treffer (Dedupe per MAC) */
#define BT_SCAN_MAX_RESULTS       (16U)
/* BLE-REQ-08: Wiederverbindungsintervall (10 s) */
#define BT_RECONNECT_INTERVAL_MS  (10000U)
/* BLE-REQ-08: Verbindungsaufbau-Timeout — Grill nicht in Reichweite (Einheit 10 ms) */
#define BT_CONNECT_TIMEOUT_10MS   (3000U)

/* Scan-Parameter: 15 % Tastverhältnis (Intervall 100 ms, Fenster 15 ms).
 * BT und WiFi teilen auf ESP32-S3 ein Funkmodul; ein Tastverhältnis von 100 %
 * (BT_GAP_SCAN_FAST_INTERVAL == BT_GAP_SCAN_FAST_WINDOW) blockiert WiFi
 * vollständig und verhindert den Aufbau der Webanzeige während des Scans.
 * Einheit: 0,625 ms-Schritte; 160 × 0,625 = 100 ms, 24 × 0,625 = 15 ms. */
#define BT_SCAN_INTERVAL_UNITS    (160U)
#define BT_SCAN_WINDOW_UNITS      (24U)

/* BLE-REQ-04..06: Mindest-Payload-Groesse, ab der wir parsen
 * (mindestens bis Ende Gas-Gewicht-Bytes). Kleinere Pakete werden verworfen.
 * Gas-Prozent (Index 31) wird nur ausgewertet, falls das Paket lang genug ist. */
#define BT_NOTIFY_MIN_LEN         (24U)

/* EXPERIMENTAL (Sniff-Auswertung 2026-05-30): das BLE-Notify nutzt das
 * gleiche Layout wie das Cloud-TCP-Frame inkl. 6 B Header (a33a + meta).
 * Offsets werden aus bluetooth.md Abschnitt 2.2, Spalte "Byte (TCP)" uebernommen.
 * Falls sich die Annahme bestaetigt, in der Anforderung dokumentieren. */
#define BT_OFF_BURNER             (6U)   /* 4 Brenner, je 2 B (off 6..13) */
#define BT_OFF_CORE               (14U)  /* 4 Sonden, je 2 B (off 14..21) */
#define BT_OFF_GAS_GRAMS_HI       (22U)
#define BT_OFF_GAS_GRAMS_LO       (23U)
#define BT_OFF_GAS_PERCENT        (31U)

/* BLE-REQ-04/05: Sentinel-Wert fuer nicht angeschlossene Sonde (Abschnitt 2.3) */
#define BT_TEMP_SENTINEL          (1500)
#define BT_TEMP_BURNER_MAX        (500)
#define BT_TEMP_CORE_MAX          (150)

/* G32-Filter (Advertising-Name + Service-UUID) */
#define BT_G32_ADV_NAME           "OWG-G32C"
#define BT_G32_ADV_NAME_LEN       (8U)

/* GATT-UUIDs (bluetooth.md Abschnitt 2.1) */
static struct bt_uuid_128 g_G32ServiceUuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0xdc0f41eaU, 0xb6aeU, 0x46a8U, 0xa19eU, 0x1a3bf4342bcbULL));
static struct bt_uuid_128 g_G32TxCharUuid = BT_UUID_INIT_128(
    BT_UUID_128_ENCODE(0xdc0f41e2U, 0xb6aeU, 0x46a8U, 0xa19eU, 0x1a3bf4342bcbULL));
static struct bt_uuid_16  g_CccUuid = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

/* BLE-REQ-08: Verbindungsaufbau-Parameter mit Timeout (verhindert unbegrenztes Scannen).
 * Fenster = halbes Intervall (50 % Tastverhältnis) statt 100 %, damit WiFi
 * waehrend des Connect-Scans weiter Pakete empfangen und senden kann. */
static const struct bt_conn_le_create_param g_CreateParam = {
    .options        = BT_CONN_LE_OPT_NONE,
    .interval       = BT_GAP_SCAN_FAST_INTERVAL,
    .window         = BT_GAP_SCAN_FAST_WINDOW,
    .interval_coded = 0U,
    .window_coded   = 0U,
    .timeout        = BT_CONNECT_TIMEOUT_10MS,
};

/* ------------------------------------------------------------------ */
/* Statischer Modul-Zustand                                            */
/* ------------------------------------------------------------------ */

/* BLE-REQ-02: Eigene Synchronisationsobjekte */
static K_SEM_DEFINE(g_ReconnectSem, 0, 1);
static K_MUTEX_DEFINE(g_StateMutex);

static struct bt_conn                 *g_Conn = NULL;
static struct bt_gatt_discover_params  g_DiscoverParams;
static struct bt_gatt_subscribe_params g_SubscribeParams;
static struct bt_uuid_128              g_DiscoverUuid; /* in discover_func ueberschrieben */

/* BLE-REQ-10: Statuszustand (unter g_StateMutex) */
static bool     g_Connected           = false;
static uint32_t g_LastPacketUptimeMs  = 0U;
static char     g_PeerMac[CFG_GRILL_MAC_BUF_SIZE] = {0};

/* BLE-REQ-03: Scan-Zustand */
static bool                    g_ScanActive    = false;
static bool                    g_ScanFilterG32 = false;
static struct k_work_delayable g_ScanTimeoutWork;

/* BLE-REQ-03: Dedupe-Tabelle fuer Scan-Treffer (nur waehrend eines Scans). */
typedef struct {
    bt_addr_le_t addr;
    bool         used;
} ScanSeen_t;
static ScanSeen_t g_ScanSeen[BT_SCAN_MAX_RESULTS];
static size_t     g_ScanSeenCount = 0U;

static bool g_BtReady = false;

/* Diagnose: bei true wird jedes Notify als Hex-Dump + Decodierung ausgegeben */
static volatile bool g_SniffActive = false;

/* ------------------------------------------------------------------ */
/* Hilfsfunktionen                                                     */
/* ------------------------------------------------------------------ */

/* BLE-REQ-04/05: Brenner-/Kerntemperatur aus zwei Bytes dekodieren.
 * Formel (bluetooth.md 2.2): value_C = x[hi] * 10 + x[lo] / 10 (ganzzahlig). */
static int16_t Bt_DecodeTemp(uint8_t hi, uint8_t lo)
{
    int value = ((int)hi * 10) + ((int)lo / 10);
    return (int16_t)value;
}

/* BLE-REQ-04/05: Sentinel ≥ 1500 °C → Sonde nicht angeschlossen. */
static bool Bt_IsTempSentinel(int16_t value)
{
    return (value >= BT_TEMP_SENTINEL);
}

/* BLE-REQ-04..06: Alle Messwerte invalidieren (bei Disconnect/Reconnect) */
static void Bt_InvalidateAllValues(void)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        (void)Temp_Set((uint8_t)TEMP_GROUP_BURNER, i, (int16_t)0, false);
        (void)Temp_Set((uint8_t)TEMP_GROUP_CORE,   i, (int16_t)0, false);
    }
    (void)Temp_SetGas((int16_t)0, false);
}

/* Dedupe-Pruefung: true wenn Adresse bereits im aktuellen Scan gesehen. */
static bool Bt_ScanSeenContains(const bt_addr_le_t *addr)
{
    size_t i;

    for (i = 0U; i < g_ScanSeenCount; i++) {
        if (g_ScanSeen[i].used &&
            (bt_addr_le_cmp(&g_ScanSeen[i].addr, addr) == 0)) {
            return true;
        }
    }
    return false;
}

static void Bt_ScanSeenAdd(const bt_addr_le_t *addr)
{
    if (g_ScanSeenCount >= BT_SCAN_MAX_RESULTS) {
        return;
    }
    g_ScanSeen[g_ScanSeenCount].addr = *addr;
    g_ScanSeen[g_ScanSeenCount].used = true;
    g_ScanSeenCount++;
}

/* ------------------------------------------------------------------ */
/* Diagnose: Hex-Dump + Decodierung der Notify-Payload                 */
/* ------------------------------------------------------------------ */

static void Bt_SniffDumpZoneGroup(const char *label, uint16_t baseOff,
                                  const uint8_t *data, uint16_t length)
{
    uint8_t i;

    printk(" %s (off %u..%u):", label,
           (unsigned int)baseOff,
           (unsigned int)(baseOff + (TEMP_ZONE_COUNT * 2U) - 1U));

    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        uint16_t hiOff = baseOff + ((uint16_t)i * 2U);
        uint16_t loOff = hiOff + 1U;

        if (loOff >= length) {
            printk("  z%u[--]", (unsigned int)i);
            continue;
        }

        uint8_t hi   = data[hiOff];
        uint8_t lo   = data[loOff];
        int16_t v    = Bt_DecodeTemp(hi, lo);
        const char *sent = Bt_IsTempSentinel(v) ? " SENT" : "";

        printk("  z%u[%02u,%02u]=%02X:%02X->%d C%s",
               (unsigned int)i,
               (unsigned int)hiOff, (unsigned int)loOff,
               (unsigned int)hi, (unsigned int)lo,
               (int)v, sent);
    }
    printk("\n");
}

static void Bt_SniffDump(const uint8_t *data, uint16_t length)
{
    uint16_t i;

    printk("BT-Sniff len=%u\n", (unsigned int)length);

    /* Hex-Dump: 16 Byte pro Zeile, zwei Halbgruppen mit Trennzeichen */
    printk(" hex:");
    for (i = 0U; i < length; i++) {
        if ((i > 0U) && ((i % 16U) == 0U)) {
            printk("\n     ");
        } else if ((i > 0U) && ((i % 8U) == 0U)) {
            printk(" ");
        } else {
            /* kein extra Trenner */
        }
        printk(" %02X", (unsigned int)data[i]);
    }
    printk("\n");

    /* Aktuelle Belegung gemaess BT_OFF_*-Konstanten */
    Bt_SniffDumpZoneGroup("BURNER", (uint16_t)BT_OFF_BURNER, data, length);
    Bt_SniffDumpZoneGroup("CORE  ", (uint16_t)BT_OFF_CORE,   data, length);

    if ((uint16_t)BT_OFF_GAS_GRAMS_LO < length) {
        uint16_t grams = (uint16_t)(((uint16_t)data[BT_OFF_GAS_GRAMS_HI] << 8) |
                                    (uint16_t)data[BT_OFF_GAS_GRAMS_LO]);
        printk(" GAS   grams[%u,%u]=%02X:%02X=%u g",
               (unsigned int)BT_OFF_GAS_GRAMS_HI,
               (unsigned int)BT_OFF_GAS_GRAMS_LO,
               (unsigned int)data[BT_OFF_GAS_GRAMS_HI],
               (unsigned int)data[BT_OFF_GAS_GRAMS_LO],
               (unsigned int)grams);
    } else {
        printk(" GAS   grams[--]");
    }
    if ((uint16_t)BT_OFF_GAS_PERCENT < length) {
        printk("  percent[%u]=%02X=%u %%\n",
               (unsigned int)BT_OFF_GAS_PERCENT,
               (unsigned int)data[BT_OFF_GAS_PERCENT],
               (unsigned int)data[BT_OFF_GAS_PERCENT]);
    } else {
        printk("  percent[--]\n");
    }
}

/* ------------------------------------------------------------------ */
/* Notify-Parser                                          BLE-REQ-04..06 */
/* ------------------------------------------------------------------ */

static void Bt_ParseAndDispatch(const uint8_t *data, uint16_t length)
{
    /* Diagnose: bei aktivem Sniff jede Payload ausgeben — auch zu kurze,
     * damit man im Debug sieht, wie viel ankommt. */
    if (g_SniffActive) {
        Bt_SniffDump(data, length);
    }

    uint8_t  i;
    int16_t  burner[TEMP_ZONE_COUNT];
    int16_t  core[TEMP_ZONE_COUNT];
    uint16_t gasGrams;
    int16_t  gasPercent;
    bool     gasValid;

    if (length < BT_NOTIFY_MIN_LEN) {
        LOG_WRN("Notify zu kurz: %u", (unsigned int)length);
        return;
    }

    /* BLE-REQ-04: Brennertemperaturen */
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        uint8_t hi = data[BT_OFF_BURNER + (i * 2U)];
        uint8_t lo = data[BT_OFF_BURNER + (i * 2U) + 1U];
        burner[i]  = Bt_DecodeTemp(hi, lo);
    }

    /* BLE-REQ-05: Kerntemperaturen */
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        uint8_t hi = data[BT_OFF_CORE + (i * 2U)];
        uint8_t lo = data[BT_OFF_CORE + (i * 2U) + 1U];
        core[i]    = Bt_DecodeTemp(hi, lo);
    }

    /* BLE-REQ-06: Gas */
    gasGrams   = (uint16_t)(((uint16_t)data[BT_OFF_GAS_GRAMS_HI] << 8) |
                            (uint16_t)data[BT_OFF_GAS_GRAMS_LO]);
    /* Percent-Byte liegt erst hinter dem Gas-Gewicht; bei kurzen Paketen fehlt es. */
    if ((uint16_t)BT_OFF_GAS_PERCENT < length) {
        gasPercent = (int16_t)data[BT_OFF_GAS_PERCENT];
    } else {
        gasPercent = -1; /* Sentinel: nicht im Paket enthalten */
    }

    /* BLE-REQ-04: Brennerwerte ablegen */
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        if (Bt_IsTempSentinel(burner[i])) {
            (void)Temp_Set((uint8_t)TEMP_GROUP_BURNER, i, (int16_t)0, false);
        } else if ((burner[i] < 0) || (burner[i] > BT_TEMP_BURNER_MAX)) {
            /* Implausibler Wert — verwerfen, ohne g_TempData zu aendern */
        } else {
            (void)Temp_Set((uint8_t)TEMP_GROUP_BURNER, i, burner[i], true);
        }
    }

    /* BLE-REQ-05: Kerntemperaturen ablegen */
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        if (Bt_IsTempSentinel(core[i])) {
            (void)Temp_Set((uint8_t)TEMP_GROUP_CORE, i, (int16_t)0, false);
        } else if ((core[i] < 0) || (core[i] > BT_TEMP_CORE_MAX)) {
            /* Implausibler Wert — verwerfen */
        } else {
            (void)Temp_Set((uint8_t)TEMP_GROUP_CORE, i, core[i], true);
        }
    }

    /* BLE-REQ-06: Gas-Plausibilisierung
     *  - Percent-Byte nicht im Paket (gasPercent == -1) → ungueltig
     *  - Sensor nicht angeschlossen (Percent=0 UND Grams=0) → ungueltig
     *  - sonst: Wert auf 0..100 begrenzen */
    if (gasPercent < 0) {
        gasValid = false;
    } else {
        gasValid = !((gasPercent == 0) && (gasGrams == 0U));
    }
    if (gasValid) {
        if (gasPercent > 100) {
            gasPercent = 100;
        }
        (void)Temp_SetGas(gasPercent, true);
    } else {
        (void)Temp_SetGas((int16_t)0, false);
    }

    (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
    g_LastPacketUptimeMs = (uint32_t)k_uptime_get();
    (void)k_mutex_unlock(&g_StateMutex);
}

static uint8_t Bt_NotifyCb(struct bt_conn *conn,
                           struct bt_gatt_subscribe_params *params,
                           const void *data, uint16_t length)
{
    ARG_UNUSED(conn);

    if (data == NULL) {
        LOG_INF("Notify-Subscription beendet.");
        params->value_handle = 0U;
        return BT_GATT_ITER_STOP;
    }

    Bt_ParseAndDispatch((const uint8_t *)data, length);

    return BT_GATT_ITER_CONTINUE;
}

/* ------------------------------------------------------------------ */
/* GATT-Discovery                                         BLE-REQ-04..06 */
/* ------------------------------------------------------------------ */

static uint8_t Bt_DiscoverFunc(struct bt_conn *conn,
                               const struct bt_gatt_attr *attr,
                               struct bt_gatt_discover_params *params)
{
    int err;

    if (attr == NULL) {
        LOG_WRN("Discovery beendet ohne Treffer.");
        (void)memset(params, 0, sizeof(*params));
        return BT_GATT_ITER_STOP;
    }

    if (bt_uuid_cmp(params->uuid, &g_G32ServiceUuid.uuid) == 0) {
        /* Service gefunden — nach TX-Charakteristik suchen */
        (void)memcpy(&g_DiscoverUuid, &g_G32TxCharUuid, sizeof(g_G32TxCharUuid));
        params->uuid         = &g_DiscoverUuid.uuid;
        params->start_handle = attr->handle + 1U;
        params->type         = BT_GATT_DISCOVER_CHARACTERISTIC;

        err = bt_gatt_discover(conn, params);
        if (err != 0) {
            LOG_ERR("Charakteristik-Discovery fehlgeschlagen: %d", err);
        }
        return BT_GATT_ITER_STOP;
    }

    if (bt_uuid_cmp(params->uuid, &g_G32TxCharUuid.uuid) == 0) {
        /* Charakteristik gefunden — Wert-Handle merken, CCC suchen */
        g_SubscribeParams.value_handle = bt_gatt_attr_value_handle(attr);

        (void)memcpy(&g_DiscoverUuid, &g_CccUuid, sizeof(g_CccUuid));
        params->uuid         = &g_DiscoverUuid.uuid;
        params->start_handle = attr->handle + 2U;
        params->type         = BT_GATT_DISCOVER_DESCRIPTOR;

        err = bt_gatt_discover(conn, params);
        if (err != 0) {
            LOG_ERR("CCC-Discovery fehlgeschlagen: %d", err);
        }
        return BT_GATT_ITER_STOP;
    }

    /* CCC-Descriptor — Notify aktivieren */
    g_SubscribeParams.notify     = Bt_NotifyCb;
    g_SubscribeParams.value      = BT_GATT_CCC_NOTIFY;
    g_SubscribeParams.ccc_handle = attr->handle;

    err = bt_gatt_subscribe(conn, &g_SubscribeParams);
    if ((err != 0) && (err != -EALREADY)) {
        LOG_ERR("Notify-Subscribe fehlgeschlagen: %d", err);
    } else {
        (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
        g_Connected = true;
        (void)k_mutex_unlock(&g_StateMutex);
        printk("BT: Verbunden mit %s.\n", g_PeerMac);
    }

    return BT_GATT_ITER_STOP;
}

static int Bt_StartDiscovery(struct bt_conn *conn)
{
    (void)memcpy(&g_DiscoverUuid, &g_G32ServiceUuid, sizeof(g_G32ServiceUuid));
    g_DiscoverParams.uuid         = &g_DiscoverUuid.uuid;
    g_DiscoverParams.func         = Bt_DiscoverFunc;
    g_DiscoverParams.start_handle = BT_ATT_FIRST_ATTRIBUTE_HANDLE;
    g_DiscoverParams.end_handle   = BT_ATT_LAST_ATTRIBUTE_HANDLE;
    g_DiscoverParams.type         = BT_GATT_DISCOVER_PRIMARY;

    return bt_gatt_discover(conn, &g_DiscoverParams);
}

/* ------------------------------------------------------------------ */
/* Verbindungs-Callbacks                                 BLE-REQ-08/09  */
/* ------------------------------------------------------------------ */

static void Bt_ConnectedCb(struct bt_conn *conn, uint8_t err)
{
    char addr[BT_ADDR_LE_STR_LEN];

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));

    if (err != 0U) {
        printk("BT: Verbindung fehlgeschlagen: 0x%02x.\n", (unsigned int)err);
        bt_conn_unref(conn);
        (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
        if (g_Conn == conn) {
            g_Conn = NULL;
        }
        g_Connected = false;
        (void)k_mutex_unlock(&g_StateMutex);
        /* BLE-REQ-08: Reconnect anstossen (Intervall im Thread) */
        (void)k_sem_give(&g_ReconnectSem);
        return;
    }

    LOG_INF("BLE-Verbindung aufgebaut: %s", addr);
    /* GATT-Discovery starten — Notify-Subscribe folgt im Discover-Callback */
    if (Bt_StartDiscovery(conn) != 0) {
        LOG_ERR("GATT-Discovery konnte nicht gestartet werden.");
    }
}

static void Bt_DisconnectedCb(struct bt_conn *conn, uint8_t reason)
{
    char addr[BT_ADDR_LE_STR_LEN];
    bool wasOurs;

    bt_addr_le_to_str(bt_conn_get_dst(conn), addr, sizeof(addr));
    printk("BT: Verbindung getrennt (Grund 0x%02x).\n", (unsigned int)reason);

    (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
    wasOurs = (g_Conn == conn);
    if (wasOurs) {
        bt_conn_unref(g_Conn);
        g_Conn = NULL;
    }
    g_Connected = false;
    (void)k_mutex_unlock(&g_StateMutex);

    /* BLE-REQ-04..06: alle Werte invalidieren */
    Bt_InvalidateAllValues();

    /* BLE-REQ-08: Wiederverbindung anstossen */
    if (wasOurs) {
        (void)k_sem_give(&g_ReconnectSem);
    }
}

BT_CONN_CB_DEFINE(g_BtConnCallbacks) = {
    .connected    = Bt_ConnectedCb,
    .disconnected = Bt_DisconnectedCb,
};

/* ------------------------------------------------------------------ */
/* Scan-Callbacks                                          BLE-REQ-03  */
/* ------------------------------------------------------------------ */

typedef struct {
    char    name[32];
    size_t  nameLen;
    bool    hasG32Uuid;
} ScanAdInfo_t;

static bool Bt_ParseAdCb(struct bt_data *data, void *user_data)
{
    ScanAdInfo_t *info = (ScanAdInfo_t *)user_data;

    switch (data->type) {
    case BT_DATA_NAME_SHORTENED:
    case BT_DATA_NAME_COMPLETE: {
        size_t copy = (data->data_len < (sizeof(info->name) - 1U))
                      ? data->data_len : (sizeof(info->name) - 1U);
        (void)memcpy(info->name, data->data, copy);
        info->name[copy] = '\0';
        info->nameLen    = copy;
        break;
    }
    case BT_DATA_UUID128_ALL:
    case BT_DATA_UUID128_SOME: {
        size_t i;
        /* 16 Byte pro UUID; vergleiche mit G32-Service-UUID (Little-Endian-Bytes) */
        for (i = 0U; (i + 16U) <= data->data_len; i += 16U) {
            if (memcmp(&data->data[i], g_G32ServiceUuid.val, 16U) == 0) {
                info->hasG32Uuid = true;
                break;
            }
        }
        break;
    }
    default:
        break;
    }
    return true;
}

static void Bt_ScanFoundCb(const bt_addr_le_t *addr, int8_t rssi, uint8_t adv_type,
                           struct net_buf_simple *ad)
{
    ScanAdInfo_t info;
    char         addrStr[BT_ADDR_LE_STR_LEN];
    bool         passesFilter;

    ARG_UNUSED(adv_type);

    if (Bt_ScanSeenContains(addr)) {
        return;
    }

    (void)memset(&info, 0, sizeof(info));
    bt_data_parse(ad, Bt_ParseAdCb, &info);

    if (g_ScanFilterG32) {
        bool nameMatches = (info.nameLen >= BT_G32_ADV_NAME_LEN) &&
                           (strncmp(info.name, BT_G32_ADV_NAME, BT_G32_ADV_NAME_LEN) == 0);
        passesFilter = nameMatches || info.hasG32Uuid;
    } else {
        passesFilter = true;
    }

    if (!passesFilter) {
        return;
    }

    Bt_ScanSeenAdd(addr);
    bt_addr_le_to_str(addr, addrStr, sizeof(addrStr));
    printk("BT: %s  %-20s  RSSI %d\n",
           addrStr,
           (info.nameLen > 0U) ? info.name : "<no name>",
           (int)rssi);
}

static void Bt_ScanTimeoutWork(struct k_work *work)
{
    ARG_UNUSED(work);
    (void)Bluetooth_ScanStop();
}

/* ------------------------------------------------------------------ */
/* Verbindungsaufbau-Helfer                                BLE-REQ-08  */
/* ------------------------------------------------------------------ */

static int Bt_DoConnect(const char *macStr)
{
    bt_addr_le_t addr;
    int          rc;

    /* Public-Adresse-Variante zuerst; falls vom Grill als Random gesendet,
     * akzeptiert Zephyr die Verbindung dennoch (Adresstyp im AD-Report). */
    rc = bt_addr_le_from_str(macStr, "public", &addr);
    if (rc != 0) {
        rc = bt_addr_le_from_str(macStr, "random", &addr);
    }
    if (rc != 0) {
        LOG_ERR("MAC-Parse-Fehler: %d", rc);
        return rc;
    }

    /* Falls noch ein Scan laeuft, vor dem Connect stoppen */
    if (g_ScanActive) {
        (void)Bluetooth_ScanStop();
    }

    printk("BT: Verbinde mit %s ...\n", macStr);

    (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
    (void)strncpy(g_PeerMac, macStr, CFG_GRILL_MAC_STR_LEN);
    g_PeerMac[CFG_GRILL_MAC_STR_LEN] = '\0';
    if (g_Conn != NULL) {
        bt_conn_unref(g_Conn);
        g_Conn = NULL;
    }
    (void)k_mutex_unlock(&g_StateMutex);

    rc = bt_conn_le_create(&addr, &g_CreateParam,
                           BT_LE_CONN_PARAM_DEFAULT, &g_Conn);
    if (rc != 0) {
        printk("BT: Verbindungsauftrag fehlgeschlagen (%d).\n", rc);
        return rc;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Bluetooth-Thread                                       BLE-REQ-02/08 */
/* ------------------------------------------------------------------ */

static void Bt_Thread(void *p1, void *p2, void *p3)
{
    Config_Data_t config;
    int           rc;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* BLE-REQ-01: Stack initialisieren */
    rc = bt_enable(NULL);
    if (rc != 0) {
        LOG_ERR("bt_enable fehlgeschlagen: %d", rc);
        return;
    }
    g_BtReady = true;
    LOG_INF("Bluetooth bereit.");

    /* BLE-REQ-08: Erst verbinden wenn WiFi steht — BT und WiFi teilen auf dem
     * ESP32-S3 ein Funkmodul; simultanes BLE-Scanning waehrend WiFi-Assoziation
     * verhindert den WLAN-Verbindungsaufbau. Timeout 2 min als Fallback fuer
     * den Fall, dass kein WLAN konfiguriert ist. */
    if (!Wifi_WaitConnected(K_SECONDS(120))) {
        printk("BT: WiFi nicht verbunden (Timeout) — starte trotzdem.\n");
    }

    /* BLE-REQ-08: Beim Start einmal direkt versuchen */
    (void)k_sem_give(&g_ReconnectSem);

    while (true) {
        (void)k_sem_take(&g_ReconnectSem, K_FOREVER);

        /* Aktuelle Konfiguration lesen */
        if (Config_Load(&config) != 0) {
            Config_GetDefaults(&config);
        }

        if (config.grillMac[0] == '\0') {
            printk("BT: Keine Kopplung gespeichert.\n");
            continue;
        }

        /* Pruefen, ob bereits verbunden — dann nichts tun */
        (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
        if (g_Connected) {
            (void)k_mutex_unlock(&g_StateMutex);
            continue;
        }
        (void)k_mutex_unlock(&g_StateMutex);

        rc = Bt_DoConnect(config.grillMac);
        if (rc != 0) {
            /* BLE-REQ-08: nach 10 s erneut versuchen */
            k_sleep(K_MSEC(BT_RECONNECT_INTERVAL_MS));
            (void)k_sem_give(&g_ReconnectSem);
        }
        /* Bei Erfolg: Verbindungs-Callback uebernimmt; bei spaeterem Disconnect
         * gibt der Disconnected-Callback wieder das Semaphor frei. */
    }
}

K_THREAD_DEFINE(bluetooth_thread, BT_THREAD_STACK_SIZE,
                Bt_Thread, NULL, NULL, NULL,
                BT_THREAD_PRIORITY, 0, 0);

/* ------------------------------------------------------------------ */
/* Oeffentliche API                                        BLE-REQ-03/10 */
/* ------------------------------------------------------------------ */

int Bluetooth_ScanStart(bool filterByG32)
{
    struct bt_le_scan_param params = {
        .type     = BT_LE_SCAN_TYPE_ACTIVE,
        .options  = BT_LE_SCAN_OPT_NONE,
        .interval = BT_SCAN_INTERVAL_UNITS,
        .window   = BT_SCAN_WINDOW_UNITS,
    };
    int rc;

    if (!g_BtReady) {
        return -ENODEV;
    }
    if (g_ScanActive) {
        return -EALREADY;
    }

    g_ScanSeenCount = 0U;
    (void)memset(g_ScanSeen, 0, sizeof(g_ScanSeen));
    g_ScanFilterG32 = filterByG32;

    rc = bt_le_scan_start(&params, Bt_ScanFoundCb);
    if (rc != 0) {
        return rc;
    }

    g_ScanActive = true;
    printk("BT: Scan gestartet (max. 30 s) ...\n");

    k_work_init_delayable(&g_ScanTimeoutWork, Bt_ScanTimeoutWork);
    (void)k_work_schedule(&g_ScanTimeoutWork, K_MSEC(BT_SCAN_TIMEOUT_MS));
    return 0;
}

int Bluetooth_ScanStop(void)
{
    int rc;

    if (!g_ScanActive) {
        return 0;
    }

    (void)k_work_cancel_delayable(&g_ScanTimeoutWork);

    rc = bt_le_scan_stop();
    g_ScanActive = false;
    printk("BT: Scan beendet.\n");
    return rc;
}

int Bluetooth_Reconnect(void)
{
    if (!g_BtReady) {
        return -ENODEV;
    }
    (void)k_sem_give(&g_ReconnectSem);
    return 0;
}

int Bluetooth_Disconnect(void)
{
    int rc = 0;

    if (!g_BtReady) {
        return -ENODEV;
    }

    (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
    if (g_Conn != NULL) {
        rc = bt_conn_disconnect(g_Conn, BT_HCI_ERR_REMOTE_USER_TERM_CONN);
    }
    g_PeerMac[0] = '\0';
    (void)k_mutex_unlock(&g_StateMutex);

    return rc;
}

void Bluetooth_GetStatus(Bluetooth_Status_t *status)
{
    Config_Data_t config;

    if (status == NULL) {
        return;
    }

    (void)memset(status, 0, sizeof(*status));

    if (Config_Load(&config) != 0) {
        Config_GetDefaults(&config);
    }
    status->paired = (config.grillMac[0] != '\0');

    (void)k_mutex_lock(&g_StateMutex, K_FOREVER);
    status->connected          = g_Connected;
    status->lastPacketUptimeMs = g_LastPacketUptimeMs;
    (void)strncpy(status->peerMac,
                  (g_PeerMac[0] != '\0') ? g_PeerMac : config.grillMac,
                  CFG_GRILL_MAC_STR_LEN);
    status->peerMac[CFG_GRILL_MAC_STR_LEN] = '\0';
    (void)k_mutex_unlock(&g_StateMutex);
}

void Bluetooth_SetSniff(bool enable)
{
    g_SniffActive = enable;
}

bool Bluetooth_GetSniff(void)
{
    return g_SniffActive;
}
