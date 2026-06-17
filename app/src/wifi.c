/* WIF-REQ-01, WIF-REQ-02, WIF-REQ-03, WIF-REQ-04, WIF-REQ-06, WIF-REQ-08: WiFi-Verbindungsverwaltung */
#include "wifi.h"
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/net_if.h>
#include <zephyr/net/net_event.h>
#include <zephyr/net/wifi_mgmt.h>
#if defined(CONFIG_NET_HOSTNAME_ENABLE)
#include <zephyr/net/hostname.h>
#endif
#include <string.h>

LOG_MODULE_REGISTER(wifi_app, LOG_LEVEL_DBG);

/* WIF-NFR-02: Statisch allozierter Thread-Stack */
#define WIFI_THREAD_STACK_SIZE  (4096U)
#define WIFI_THREAD_PRIORITY    (5)
/* WIF-REQ-07: Verbindungs-Timeout und Wiederholungsintervall */
#define WIFI_CONNECT_TIMEOUT_S  (60U)
#define WIFI_RETRY_DELAY_S      (30U)

/* WIF-REQ-01: Semaphor mit Initialwert 1 — loest Verbindungsversuch beim Start aus */
static K_SEM_DEFINE(g_ReconnectSem, 1, 1);

/* Semaphor fuer Verbindungsergebnis aus dem Callback an den Thread */
static K_SEM_DEFINE(g_ConnectResultSem, 0, 1);

/* Einmalige Verbindungs-Meldung fuer Wifi_WaitConnected():
 * wird gegeben, sobald die erste IP-Verbindung steht; bleibt danach verfuegbar
 * (max=1 verhindert Ansammlung, take ohne Abgabe durch den Aufrufer ist korrekt). */
static K_SEM_DEFINE(g_ConnectedOnceSem, 0, 1);

static struct net_mgmt_event_callback g_WifiCb;
static struct net_mgmt_event_callback g_IpCb;
static struct net_mgmt_event_callback g_ScanCb;

static bool     g_ConnectSuccess  = false;
static bool     g_Connected       = false;

/* WIF-REQ-08/09: Scan-Zustand und Ergebnispuffer */
static bool              g_ScanActive      = false;
static uint8_t           g_ScanResultCount = 0U;
static Wifi_ScanResult_t g_ScanResults[WIFI_SCAN_MAX_RESULTS];
static Wifi_ScanDoneCb_t g_ScanDoneCb      = NULL;
static void             *g_ScanUserData    = NULL;

/* WIF-REQ-05: Zwischengespeicherter Status fuer Wifi_GetStatus() */
static char g_CurrentSsid[CFG_WIFI_SSID_MAX_LEN + 1U] = {0};
static char g_CurrentIp[WIFI_IP_ADDR_STR_LEN]         = {0};

/* ------------------------------------------------------------------ */
/* Hilfsfunktion: IP-Adresse ausgeben                     WIF-REQ-03  */
/* ------------------------------------------------------------------ */

static void Wifi_PrintIpAddress(const struct net_if *iface)
{
    uint8_t i;

    if (iface->config.ip.ipv4 == NULL) {
        return;
    }

    for (i = 0U; i < (uint8_t)NET_IF_MAX_IPV4_ADDR; i++) {
        if (iface->config.ip.ipv4->unicast[i].ipv4.is_used) {
            (void)net_addr_ntop(AF_INET,
                &iface->config.ip.ipv4->unicast[i].ipv4.address.in_addr,
                g_CurrentIp, sizeof(g_CurrentIp));
            LOG_INF("Verbunden. IP: %s", g_CurrentIp);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* Event-Callbacks                                        WIF-REQ-03  */
/* ------------------------------------------------------------------ */

static void Wifi_EventCallback(struct net_mgmt_event_callback *cb,
                               uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (mgmt_event == NET_EVENT_WIFI_CONNECT_RESULT) {
        const struct wifi_status *status = (const struct wifi_status *)cb->info;
        g_ConnectSuccess = (status->conn_status == WIFI_STATUS_CONN_SUCCESS);
        k_sem_give(&g_ConnectResultSem);
    } else if (mgmt_event == NET_EVENT_WIFI_DISCONNECT_RESULT) {
        g_Connected    = false;
        g_CurrentIp[0] = '\0';
        LOG_INF("Verbindung getrennt.");
        /* WIF-REQ-07: Automatisch neu verbinden */
        k_sem_give(&g_ReconnectSem);
    }
}

static void Wifi_IpCallback(struct net_mgmt_event_callback *cb,
                             uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(cb);

    if (mgmt_event == NET_EVENT_IPV4_ADDR_ADD) {
        Wifi_PrintIpAddress(iface);
    }
}

/* ------------------------------------------------------------------ */
/* Scan-Callback                                          WIF-REQ-08  */
/* ------------------------------------------------------------------ */

static void Wifi_ScanCallback(struct net_mgmt_event_callback *cb,
                              uint64_t mgmt_event, struct net_if *iface)
{
    ARG_UNUSED(iface);

    if (mgmt_event == NET_EVENT_WIFI_SCAN_RESULT) {
        const struct wifi_scan_result *entry =
            (const struct wifi_scan_result *)cb->info;

        /* WIF-REQ-09: Ueberzaehlige Ergebnisse still verwerfen */
        if (g_ScanResultCount >= WIFI_SCAN_MAX_RESULTS) {
            return;
        }

        (void)memcpy(g_ScanResults[g_ScanResultCount].ssid,
                     entry->ssid, entry->ssid_length);
        g_ScanResults[g_ScanResultCount].ssid[entry->ssid_length] = '\0';
        g_ScanResults[g_ScanResultCount].rssi     = entry->rssi;
        g_ScanResults[g_ScanResultCount].security = entry->security;
        g_ScanResultCount++;

    } else if (mgmt_event == NET_EVENT_WIFI_SCAN_DONE) {
        g_ScanActive = false;
        /* WIF-REQ-09: Ergebnisse an den Aufrufer uebergeben */
        if (g_ScanDoneCb != NULL) {
            g_ScanDoneCb(g_ScanResultCount, g_ScanResults, g_ScanUserData);
        }
    }
}

/* ------------------------------------------------------------------ */
/* WiFi-Thread                                            WIF-REQ-02  */
/* ------------------------------------------------------------------ */

static void Wifi_Thread(void *p1, void *p2, void *p3)
{
    Config_Data_t                config;
    struct net_if               *iface;
    struct wifi_connect_req_params params;
    int                          rc;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    /* Callbacks registrieren */
    net_mgmt_init_event_callback(&g_WifiCb, Wifi_EventCallback,
        NET_EVENT_WIFI_CONNECT_RESULT | NET_EVENT_WIFI_DISCONNECT_RESULT);
    net_mgmt_add_event_callback(&g_WifiCb);

    net_mgmt_init_event_callback(&g_IpCb, Wifi_IpCallback,
        NET_EVENT_IPV4_ADDR_ADD);
    net_mgmt_add_event_callback(&g_IpCb);

    /* WIF-REQ-08: Scan-Events registrieren */
    net_mgmt_init_event_callback(&g_ScanCb, Wifi_ScanCallback,
        NET_EVENT_WIFI_SCAN_RESULT | NET_EVENT_WIFI_SCAN_DONE);
    net_mgmt_add_event_callback(&g_ScanCb);

    iface = net_if_get_default();

    while (true) {
        /* WIF-REQ-01/04: Auf Verbindungsauftrag warten */
        (void)k_sem_take(&g_ReconnectSem, K_FOREVER);

        /* Aktuelle Konfiguration lesen */
        if (Config_Load(&config) != 0) {
            Config_GetDefaults(&config);
        }

        if (config.wifiSsid[0] == '\0') {
            LOG_INF("Keine SSID konfiguriert.");
            continue;
        }

        /* Laufende Verbindung sauber trennen */
        if (g_Connected) {
            (void)net_mgmt(NET_REQUEST_WIFI_DISCONNECT, iface, NULL, 0U);
            g_Connected = false;
            k_sleep(K_MSEC(500));
        }

        /* WIF-REQ-06: Hostname vor dem Connect setzen (wirkt auf DHCP-Option 12) */
#if defined(CONFIG_NET_HOSTNAME_ENABLE)
        if (config.wifiHostname[0] != '\0') {
            rc = net_hostname_set(config.wifiHostname,
                                  strlen(config.wifiHostname));
            if (rc < 0) {
                LOG_WRN("Hostname setzen fehlgeschlagen: %d", rc);
            } else {
                LOG_INF("Hostname: %s", config.wifiHostname);
            }
        }
#endif

        (void)strncpy(g_CurrentSsid, config.wifiSsid, CFG_WIFI_SSID_MAX_LEN);
        g_CurrentSsid[CFG_WIFI_SSID_MAX_LEN] = '\0';
        LOG_INF("Verbinde mit \"%s\" ...", g_CurrentSsid);

        (void)memset(&params, 0, sizeof(params));
        params.ssid        = (const uint8_t *)config.wifiSsid;
        params.ssid_length = (uint8_t)strlen(config.wifiSsid);
        params.channel     = WIFI_CHANNEL_ANY;
        params.timeout     = SYS_FOREVER_MS;

        if (config.wifiPassword[0] != '\0') {
            params.psk        = (const uint8_t *)config.wifiPassword;
            params.psk_length = (uint8_t)strlen(config.wifiPassword);
            params.security   = WIFI_SECURITY_TYPE_PSK;
        } else {
            params.security = WIFI_SECURITY_TYPE_NONE;
        }

        rc = net_mgmt(NET_REQUEST_WIFI_CONNECT, iface, &params,
                      sizeof(params));
        if (rc < 0) {
            LOG_ERR("Verbindungsauftrag fehlgeschlagen (%d).", rc);
            continue;
        }

        /* WIF-REQ-07: Auf Verbindungsergebnis warten (max. 60 Sekunden) */
        if (k_sem_take(&g_ConnectResultSem, K_SECONDS(WIFI_CONNECT_TIMEOUT_S)) != 0) {
            LOG_WRN("Verbindung fehlgeschlagen (Timeout). Neuer Versuch in %u s.",
                    WIFI_RETRY_DELAY_S);
            k_sleep(K_SECONDS(WIFI_RETRY_DELAY_S));
            k_sem_give(&g_ReconnectSem);
            continue;
        }

        if (g_ConnectSuccess) {
            g_Connected = true;
            /* Einmalig signalisieren — wartendes Bluetooth_Thread() kann jetzt starten */
            (void)k_sem_give(&g_ConnectedOnceSem);
        } else {
            LOG_WRN("Verbindung fehlgeschlagen. Neuer Versuch in %u s.",
                    WIFI_RETRY_DELAY_S);
            k_sleep(K_SECONDS(WIFI_RETRY_DELAY_S));
            k_sem_give(&g_ReconnectSem);
        }
    }
}

/* WIF-NFR-02: Statisch allozierter Stack */
K_THREAD_DEFINE(wifi_thread, WIFI_THREAD_STACK_SIZE,
                Wifi_Thread, NULL, NULL, NULL,
                WIFI_THREAD_PRIORITY, 0, 0);

/* ------------------------------------------------------------------ */
/* Oeffentliche API                                       WIF-REQ-04  */
/* ------------------------------------------------------------------ */

void Wifi_Reconnect(void)
{
    (void)k_sem_give(&g_ReconnectSem);
}

bool Wifi_WaitConnected(k_timeout_t timeout)
{
    if (k_sem_take(&g_ConnectedOnceSem, timeout) != 0) {
        return false;
    }
    /* Semaphor direkt zurueckgeben, damit weitere Aufrufer nicht blockieren */
    (void)k_sem_give(&g_ConnectedOnceSem);
    return true;
}

/* WIF-REQ-08 */
int Wifi_Scan(Wifi_ScanDoneCb_t cb, void *user_data)
{
    struct net_if *iface = net_if_get_default();
    int            rc;

    if (g_ScanActive) {
        return -EBUSY;
    }

    g_ScanActive      = true;
    g_ScanResultCount = 0U;
    g_ScanDoneCb      = cb;
    g_ScanUserData    = user_data;
    (void)memset(g_ScanResults, 0, sizeof(g_ScanResults));

    rc = net_mgmt(NET_REQUEST_WIFI_SCAN, iface, NULL, 0U);
    if (rc != 0) {
        g_ScanActive = false;
        return -EIO;
    }

    return 0;
}

/* WIF-REQ-09 */
const char *Wifi_SecurityStr(uint8_t security)
{
    return wifi_security_txt((enum wifi_security_type)security);
}

/* WIF-REQ-05 */
void Wifi_GetStatus(Wifi_Status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->connected = g_Connected;
    (void)strncpy(status->ssid, g_CurrentSsid, CFG_WIFI_SSID_MAX_LEN);
    status->ssid[CFG_WIFI_SSID_MAX_LEN] = '\0';
    (void)strncpy(status->ip, g_CurrentIp, WIFI_IP_ADDR_STR_LEN - 1U);
    status->ip[WIFI_IP_ADDR_STR_LEN - 1U] = '\0';
}
