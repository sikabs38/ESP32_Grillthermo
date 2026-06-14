/* MQT-REQ-01, MQT-NFR-01, MQT-NFR-02: MQTT-Verbindung, MISRA-konform, statischer Speicher */
#include "mqtt.h"
#include "config.h"
#include "wifi.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/mqtt.h>
#include <zephyr/net/socket.h>
#include <string.h>

LOG_MODULE_REGISTER(grill_mqtt, LOG_LEVEL_INF);

/* MQT-NFR-02: Puffer als Compile-Zeit-Konstanten dimensioniert */
#define MQTT_RX_BUF_SIZE   (512U)
#define MQTT_TX_BUF_SIZE   (256U)
#define MQTT_KEEPALIVE_S   (60U)
#define MQTT_POLL_TIMEOUT_MS (30000)
#define MQTT_PORT_STR      "1883"
#define MQTT_RECONNECT_S   (30U)
#define MQTT_STACK_SIZE    (4096U)
#define MQTT_THREAD_PRIO   (5)

static uint8_t            g_RxBuf[MQTT_RX_BUF_SIZE];
static uint8_t            g_TxBuf[MQTT_TX_BUF_SIZE];
static struct mqtt_client g_Client;
/* MISRA 11.4: struct sockaddr_in fuer Broker-Adresse; cast auf const void* fuer MQTT-API noetig */
static struct sockaddr_in g_BrokerAddr;
static bool               g_Connected = false;
static char               g_BrokerHostname[CFG_MQTT_BROKER_MAX_LEN + 1U];
static char               g_ClientId[CFG_WIFI_HOSTNAME_MAX_LEN + 1U];
static char               g_Password[CFG_MQTT_PASS_MAX_LEN + 1U];
static struct mqtt_utf8   g_MqttPassword;
static struct mqtt_utf8   g_MqttUserName;

/* MQT-REQ-01: Semaphor fuer sofortigen Wiederverbindungsversuch */
static K_SEM_DEFINE(g_ReconnectSem, 0, 1);

static K_THREAD_STACK_DEFINE(g_MqttStack, MQTT_STACK_SIZE);
static struct k_thread g_MqttThread;

static void Mqtt_EventCb(struct mqtt_client *client, const struct mqtt_evt *evt)
{
    ARG_UNUSED(client);

    switch (evt->type) {
    case MQTT_EVT_CONNACK:
        if (evt->result == 0) {
            g_Connected = true;
            LOG_INF("MQTT verbunden: %s", g_BrokerHostname);
        } else {
            LOG_WRN("MQTT CONNACK Fehler: %d", evt->result);
        }
        break;

    case MQTT_EVT_DISCONNECT:
        g_Connected = false;
        LOG_INF("MQTT getrennt");
        break;

    default:
        break;
    }
}

static int Mqtt_DoConnect(void)
{
    struct zsock_addrinfo *res = NULL;
    int rc;

    rc = zsock_getaddrinfo(g_BrokerHostname, MQTT_PORT_STR, NULL, &res);
    if (rc != 0) {
        LOG_ERR("DNS-Aufloesung fehlgeschlagen fuer %s: %d", g_BrokerHostname, rc);
        return -EHOSTUNREACH;
    }

    if (res == NULL) {
        return -EHOSTUNREACH;
    }

    /* Adresse in statischen Puffer kopieren; res->ai_addr zeigt auf struct sockaddr_in */
    (void)memcpy(&g_BrokerAddr, res->ai_addr, sizeof(g_BrokerAddr));
    zsock_freeaddrinfo(res);

    mqtt_client_init(&g_Client);

    /* MISRA 11.3: Pointer-Cast auf const void* fuer Zephyr MQTT API (broker-Feld) */
    g_Client.broker          = (const void *)&g_BrokerAddr;
    g_Client.evt_cb          = Mqtt_EventCb;
    g_Client.client_id.utf8  = (const uint8_t *)g_ClientId;
    g_Client.client_id.size  = (uint32_t)strlen(g_ClientId);
    g_Client.keepalive       = (uint16_t)MQTT_KEEPALIVE_S;
    g_Client.protocol_version = (uint8_t)MQTT_VERSION_3_1_1;
    g_Client.rx_buf          = g_RxBuf;
    g_Client.rx_buf_size     = (uint32_t)MQTT_RX_BUF_SIZE;
    g_Client.tx_buf          = g_TxBuf;
    g_Client.tx_buf_size     = (uint32_t)MQTT_TX_BUF_SIZE;
    g_Client.transport.type  = MQTT_TRANSPORT_NON_SECURE;
    g_Client.user_name       = NULL;
    g_Client.password        = NULL;

    if (g_Password[0] != '\0') {
        /* MQTT-Spec: Passwort erfordert Benutzername; ClientId wird als Benutzername verwendet */
        g_MqttUserName.utf8    = (const uint8_t *)g_ClientId;
        g_MqttUserName.size    = (uint32_t)strlen(g_ClientId);
        g_MqttPassword.utf8    = (const uint8_t *)g_Password;
        g_MqttPassword.size    = (uint32_t)strlen(g_Password);
        g_Client.user_name     = &g_MqttUserName;
        g_Client.password      = &g_MqttPassword;
    }

    rc = mqtt_connect(&g_Client);
    if (rc != 0) {
        LOG_ERR("mqtt_connect fehlgeschlagen: %d", rc);
    }

    return rc;
}

static void Mqtt_RunEventLoop(void)
{
    struct zsock_pollfd fds;
    int                 rc;

    fds.events  = ZSOCK_POLLIN;
    fds.revents = 0;

    while (g_Connected) {
        fds.fd = g_Client.transport.tcp.sock;

        rc = zsock_poll(&fds, 1, MQTT_POLL_TIMEOUT_MS);
        if (rc < 0) {
            LOG_ERR("poll-Fehler: %d", rc);
            break;
        }

        if (rc > 0) {
            rc = mqtt_input(&g_Client);
            if (rc != 0) {
                LOG_ERR("mqtt_input-Fehler: %d", rc);
                break;
            }
        }

        rc = mqtt_live(&g_Client);
        if ((rc != 0) && (rc != -EAGAIN)) {
            LOG_ERR("mqtt_live-Fehler: %d", rc);
            break;
        }
    }
}

static void Mqtt_Thread(void *p1, void *p2, void *p3)
{
    Config_Data_t cfg;

    ARG_UNUSED(p1);
    ARG_UNUSED(p2);
    ARG_UNUSED(p3);

    for (;;) {
        /* MQT-REQ-01: Erst WiFi abwarten, bevor Verbindungsversuch */
        while (!Wifi_WaitConnected(K_SECONDS(30))) {
            /* Weiter warten bis WiFi bereit ist */
        }

        if (Config_Load(&cfg) != 0) {
            Config_GetDefaults(&cfg);
        }

        if (cfg.mqttBroker[0] == '\0') {
            /* Kein Broker konfiguriert — auf Konfigurationsaenderung warten */
            (void)k_sem_take(&g_ReconnectSem, K_SECONDS(10));
            continue;
        }

        (void)strncpy(g_BrokerHostname, cfg.mqttBroker, CFG_MQTT_BROKER_MAX_LEN);
        g_BrokerHostname[CFG_MQTT_BROKER_MAX_LEN] = '\0';
        (void)strncpy(g_Password, cfg.mqttPassword, CFG_MQTT_PASS_MAX_LEN);
        g_Password[CFG_MQTT_PASS_MAX_LEN] = '\0';
        (void)strncpy(g_ClientId, cfg.wifiHostname, CFG_WIFI_HOSTNAME_MAX_LEN);
        g_ClientId[CFG_WIFI_HOSTNAME_MAX_LEN] = '\0';

        if (g_ClientId[0] == '\0') {
            /* Fallback: ohne Hostname keine sinnvolle Client-ID */
            (void)strncpy(g_ClientId, "ESP32_Grillthermo", CFG_WIFI_HOSTNAME_MAX_LEN);
            g_ClientId[CFG_WIFI_HOSTNAME_MAX_LEN] = '\0';
        }

        if (Mqtt_DoConnect() == 0) {
            Mqtt_RunEventLoop();
            mqtt_abort(&g_Client);
        }

        g_Connected = false;

        /* MQT-REQ-01: 30 s warten; Mqtt_Reconnect() kann die Wartezeit verkuerzen */
        (void)k_sem_take(&g_ReconnectSem, K_SECONDS(MQTT_RECONNECT_S));
    }
}

/* MQT-REQ-04: Verbindungsstatus abfragen */
void Mqtt_GetStatus(Mqtt_Status_t *status)
{
    if (status != NULL) {
        status->connected = g_Connected;
        (void)strncpy(status->broker, g_BrokerHostname, CFG_MQTT_BROKER_MAX_LEN);
        status->broker[CFG_MQTT_BROKER_MAX_LEN] = '\0';
    }
}

/* MQT-REQ-01: Sofortigen Wiederverbindungsversuch ausloesen */
void Mqtt_Reconnect(void)
{
    (void)k_sem_give(&g_ReconnectSem);
}

static int Mqtt_Init(void)
{
    (void)k_thread_create(&g_MqttThread, g_MqttStack,
                          K_THREAD_STACK_SIZEOF(g_MqttStack),
                          Mqtt_Thread, NULL, NULL, NULL,
                          MQTT_THREAD_PRIO, 0, K_NO_WAIT);
    (void)k_thread_name_set(&g_MqttThread, "mqtt");
    return 0;
}

SYS_INIT(Mqtt_Init, APPLICATION, 1);
