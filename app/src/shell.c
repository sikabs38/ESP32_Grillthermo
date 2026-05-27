/* SHL-REQ-01, SHL-REQ-06: PIN-geschuetzter Shell-Zugang ueber USB CDC-ACM */
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(grill_shell, LOG_LEVEL_INF);

/* CFG-REQ-02, CFG-REQ-04: Aktive Konfiguration im RAM; aus NVS geladen beim Start */
static Config_Data_t g_Config;
/* SHL-REQ-06: Authentifizierungsstatus der aktiven Shell-Sitzung */
static bool          g_Authenticated = false;

/* ------------------------------------------------------------------ */
/* Hilfsfunktionen                                                     */
/* ------------------------------------------------------------------ */

/* SHL-REQ-06: Validierung — 4-6 Ziffern */
static bool Shell_PinIsValid(const char *pin)
{
    size_t len;
    size_t i;

    if (pin == NULL) {
        return false;
    }

    len = strlen(pin);

    if ((len < CFG_PIN_MIN_LEN) || (len > CFG_PIN_MAX_LEN)) {
        return false;
    }

    for (i = 0U; i < len; i++) {
        if ((pin[i] < '0') || (pin[i] > '9')) {
            return false;
        }
    }

    return true;
}

/* SHL-REQ-06: Erkennung unveraenderter Standard-PIN fuer Hinweis */
static bool Shell_PinIsDefault(void)
{
    return (strncmp(g_Config.pin, CFG_PIN_DEFAULT, CFG_PIN_BUF_SIZE) == 0);
}

/* SHL-REQ-06: Zugriffsschutz — wird in allen geschuetzten Befehlen geprueft */
static bool Shell_CheckAuth(const struct shell *sh)
{
    if (!g_Authenticated) {
        shell_error(sh, "Zugang verweigert. Bitte mit \"login <pin>\" anmelden.");
        return false;
    }

    return true;
}

/* CFG-REQ-01, CFG-REQ-04: PIN in g_Config aktualisieren und in NVS speichern */
static int Shell_PinStore(const char *newPin)
{
    int rc;

    (void)strncpy(g_Config.pin, newPin, CFG_PIN_BUF_SIZE - 1U);
    g_Config.pin[CFG_PIN_BUF_SIZE - 1U] = '\0';
    /* TODO: CFG-REQ-05 — PIN AES-verschluesselt speichern */

    rc = Config_Save(&g_Config);

    if (rc < 0) {
        LOG_ERR("PIN-Speicherfehler: %d", rc);
    }

    return rc;
}

/* ------------------------------------------------------------------ */
/* login / logout                                         SHL-REQ-06  */
/* ------------------------------------------------------------------ */

static int Shell_CmdLogin(const struct shell *sh, size_t argc, char **argv)
{
    if (argc != 2) {
        shell_error(sh, "Verwendung: login <pin>");
        return -EINVAL;
    }

    if (strncmp(argv[1], g_Config.pin, CFG_PIN_BUF_SIZE) != 0) {
        shell_error(sh, "Falsche PIN. Zugang verweigert.");
        g_Authenticated = false;
        return -EACCES;
    }

    g_Authenticated = true;
    shell_obscure_set(sh, false);
    shell_print(sh, "Anmeldung erfolgreich.");

    if (Shell_PinIsDefault()) {
        shell_warn(sh,
            "Warnung: Standard-PIN aktiv. "
            "Bitte mit \"config pin 000000 <neue-pin>\" aendern.");
    }

    return 0;
}

static int Shell_CmdLogout(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    g_Authenticated = false;
    shell_obscure_set(sh, true);
    shell_print(sh, "Abgemeldet.");

    return 0;
}

/* ------------------------------------------------------------------ */
/* wifi                                                   SHL-REQ-02  */
/* ------------------------------------------------------------------ */

static int Shell_CmdWifiSet(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: wifi set <ssid> <password>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > CFG_WIFI_SSID_MAX_LEN) {
        shell_error(sh, "SSID zu lang (max. %u Zeichen).", (unsigned int)CFG_WIFI_SSID_MAX_LEN);
        return -EINVAL;
    }

    if (strlen(argv[2]) > CFG_WIFI_PASS_MAX_LEN) {
        shell_error(sh, "Passwort zu lang (max. %u Zeichen).", (unsigned int)CFG_WIFI_PASS_MAX_LEN);
        return -EINVAL;
    }

    (void)strncpy(g_Config.wifiSsid, argv[1], CFG_WIFI_SSID_MAX_LEN);
    g_Config.wifiSsid[CFG_WIFI_SSID_MAX_LEN] = '\0';
    (void)strncpy(g_Config.wifiPassword, argv[2], CFG_WIFI_PASS_MAX_LEN);
    g_Config.wifiPassword[CFG_WIFI_PASS_MAX_LEN] = '\0';
    /* TODO: CFG-REQ-05 — Passwort vor dem Speichern AES-verschluesseln */

    rc = Config_Save(&g_Config);

    if (rc < 0) {
        shell_error(sh, "Speicherfehler: %d", rc);
        return rc;
    }

    shell_print(sh, "WiFi SSID gesetzt: %s", g_Config.wifiSsid);

    return 0;
}

/* ------------------------------------------------------------------ */
/* mqtt                                                   SHL-REQ-03  */
/* ------------------------------------------------------------------ */

static int Shell_CmdMqttSet(const struct shell *sh, size_t argc, char **argv)
{
    unsigned long portLong;
    char         *endPtr = NULL;
    int           rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: mqtt set <broker> <port>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > CFG_MQTT_BROKER_MAX_LEN) {
        shell_error(sh, "Broker-Adresse zu lang (max. %u Zeichen).",
                    (unsigned int)CFG_MQTT_BROKER_MAX_LEN);
        return -EINVAL;
    }

    portLong = strtoul(argv[2], &endPtr, 10);

    if ((endPtr == argv[2]) || (*endPtr != '\0') || (portLong == 0UL) || (portLong > 65535UL)) {
        shell_error(sh, "Ungueltiger Port (1-65535).");
        return -EINVAL;
    }

    (void)strncpy(g_Config.mqttBroker, argv[1], CFG_MQTT_BROKER_MAX_LEN);
    g_Config.mqttBroker[CFG_MQTT_BROKER_MAX_LEN] = '\0';
    g_Config.mqttPort = (uint16_t)portLong;

    rc = Config_Save(&g_Config);

    if (rc < 0) {
        shell_error(sh, "Speicherfehler: %d", rc);
        return rc;
    }

    shell_print(sh, "MQTT Broker gesetzt: %s:%u",
                g_Config.mqttBroker, (unsigned int)g_Config.mqttPort);

    return 0;
}

/* ------------------------------------------------------------------ */
/* config show                                            SHL-REQ-04  */
/* ------------------------------------------------------------------ */

static int Shell_CmdConfigShow(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    shell_print(sh, "--- Grill Buddy Konfiguration ---");
    shell_print(sh, "WiFi SSID    : %s",
                (g_Config.wifiSsid[0] != '\0') ? g_Config.wifiSsid : "[nicht gesetzt]");
    /* SHL-REQ-05, CFG-REQ-05: Passwort nie im Klartext ausgeben */
    shell_print(sh, "WiFi Passwort: ********");
    shell_print(sh, "MQTT Broker  : %s",
                (g_Config.mqttBroker[0] != '\0') ? g_Config.mqttBroker : "[nicht gesetzt]");
    shell_print(sh, "MQTT Port    : %u", (unsigned int)g_Config.mqttPort);

    return 0;
}

/* ------------------------------------------------------------------ */
/* config pin                                             SHL-REQ-06  */
/* ------------------------------------------------------------------ */

static int Shell_CmdConfigPin(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: config pin <alte-pin> <neue-pin>");
        return -EINVAL;
    }

    if (strncmp(argv[1], g_Config.pin, CFG_PIN_BUF_SIZE) != 0) {
        shell_error(sh, "Falsche alte PIN.");
        return -EACCES;
    }

    if (!Shell_PinIsValid(argv[2])) {
        shell_error(sh, "Ungueltige PIN: 4-6 Ziffern erforderlich.");
        return -EINVAL;
    }

    rc = Shell_PinStore(argv[2]);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Speichern der PIN: %d", rc);
        return rc;
    }

    shell_print(sh, "PIN erfolgreich geaendert.");

    return 0;
}

/* ------------------------------------------------------------------ */
/* config reset                                           CFG-REQ-03  */
/* ------------------------------------------------------------------ */

static int Shell_CmdConfigReset(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if ((argc < 2) || (strncmp(argv[1], "confirm", 8U) != 0)) {
        shell_warn(sh, "Alle Einstellungen werden auf Werkseinstellungen zurueckgesetzt.");
        shell_warn(sh, "Zur Bestaetigung: config reset confirm");
        return 0;
    }

    /* CFG-REQ-03: Alle NVS-Eintraege invalidieren, dann Standardwerte speichern */
    (void)Config_InvalidateAll();
    Config_GetDefaults(&g_Config);

    rc = Config_Save(&g_Config);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Speichern der Standardkonfiguration: %d", rc);
        return rc;
    }

    shell_print(sh, "Konfiguration auf Werkseinstellungen zurueckgesetzt.");
    shell_print(sh, "PIN zurueckgesetzt auf: " CFG_PIN_DEFAULT);

    return 0;
}

/* ------------------------------------------------------------------ */
/* Initialisierung                              CFG-REQ-02, SHL-REQ-01 */
/* ------------------------------------------------------------------ */

static int Shell_LoadConfig(void)
{
    int rc;

    rc = Config_Init();

    if (rc < 0) {
        LOG_ERR("Config-Initialisierungsfehler: %d; Standardwerte aktiv.", rc);
        Config_GetDefaults(&g_Config);
        return 0; /* Fehler nicht weiterleiten; Shell bleibt funktionsfaehig */
    }

    rc = Config_Load(&g_Config);

    if (rc != 0) {
        LOG_WRN("Konfiguration nicht geladen (rc=%d); Standardwerte aktiv.", rc);
    }

    return 0;
}

SYS_INIT(Shell_LoadConfig, APPLICATION, 0);

/* ------------------------------------------------------------------ */
/* Befehlsbaum                                                        */
/* ------------------------------------------------------------------ */

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi,
    SHELL_CMD_ARG(set, NULL, "WiFi konfigurieren: <ssid> <password>", Shell_CmdWifiSet, 3, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_mqtt,
    SHELL_CMD_ARG(set, NULL, "MQTT konfigurieren: <broker> <port>", Shell_CmdMqttSet, 3, 0),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config,
    SHELL_CMD(show, NULL, "Konfiguration anzeigen", Shell_CmdConfigShow),
    SHELL_CMD_ARG(pin, NULL, "PIN aendern: <alte-pin> <neue-pin>", Shell_CmdConfigPin, 3, 0),
    SHELL_CMD_ARG(reset, NULL, "Werkseinstellungen: confirm zum Bestaetigen",
                  Shell_CmdConfigReset, 1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(login,  NULL,        "Anmelden: <pin>",      Shell_CmdLogin);
SHELL_CMD_REGISTER(logout, NULL,        "Abmelden",             Shell_CmdLogout);
SHELL_CMD_REGISTER(wifi,   &sub_wifi,   "WiFi-Konfiguration",   NULL);
SHELL_CMD_REGISTER(mqtt,   &sub_mqtt,   "MQTT-Konfiguration",   NULL);
SHELL_CMD_REGISTER(config, &sub_config, "Systemkonfiguration",  NULL);
