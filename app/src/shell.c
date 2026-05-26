/* SHL-REQ-06: PIN-geschuetzter Shell-Zugang */

#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <errno.h>

#define PIN_MIN_LEN   (4U)
#define PIN_MAX_LEN   (6U)
#define PIN_BUF_SIZE  (PIN_MAX_LEN + 1U)
#define PIN_DEFAULT   "000000"

/* CFG-REQ-04: Shell-PIN mit Standardwert und Laengenbegrenzung */
static char g_Pin[PIN_BUF_SIZE]  = PIN_DEFAULT;
/* SHL-REQ-06: Authentifizierungsstatus der aktiven Shell-Sitzung */
static bool g_Authenticated      = false;

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

    if ((len < PIN_MIN_LEN) || (len > PIN_MAX_LEN)) {
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
    return (strncmp(g_Pin, PIN_DEFAULT, PIN_BUF_SIZE) == 0);
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

/* CFG-REQ-04, CFG-REQ-05: PIN-Speicherung (RAM); NVS/AES ausstehend */
static void Shell_PinStore(const char *newPin)
{
    (void)strncpy(g_Pin, newPin, PIN_BUF_SIZE - 1U);
    g_Pin[PIN_BUF_SIZE - 1U] = '\0';
    /* TODO: CFG-REQ-05 — PIN AES-verschluesselt in NVS speichern */
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

    if (strncmp(argv[1], g_Pin, PIN_BUF_SIZE) != 0) {
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
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: wifi set <ssid> <password>");
        return -EINVAL;
    }

    shell_print(sh, "WiFi SSID gesetzt: %s", argv[1]);
    /* TODO: CFG-REQ-01 — in NVS speichern; CFG-REQ-05 — Passwort verschluesseln */

    return 0;
}

/* ------------------------------------------------------------------ */
/* mqtt                                                   SHL-REQ-03  */
/* ------------------------------------------------------------------ */

static int Shell_CmdMqttSet(const struct shell *sh, size_t argc, char **argv)
{
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: mqtt set <broker> <port>");
        return -EINVAL;
    }

    shell_print(sh, "MQTT Broker gesetzt: %s:%s", argv[1], argv[2]);
    /* TODO: CFG-REQ-01 — in NVS speichern */

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
    shell_print(sh, "WiFi SSID    : [nicht gesetzt]");
    shell_print(sh, "WiFi Passwort: ********");
    shell_print(sh, "MQTT Broker  : [nicht gesetzt]");
    shell_print(sh, "MQTT Port    : 1883");
    /* TODO: CFG-REQ-02 — Werte aus NVS laden */

    return 0;
}

/* ------------------------------------------------------------------ */
/* config pin                                             SHL-REQ-06  */
/* ------------------------------------------------------------------ */

static int Shell_CmdConfigPin(const struct shell *sh, size_t argc, char **argv)
{
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 3) {
        shell_error(sh, "Verwendung: config pin <alte-pin> <neue-pin>");
        return -EINVAL;
    }

    if (strncmp(argv[1], g_Pin, PIN_BUF_SIZE) != 0) {
        shell_error(sh, "Falsche alte PIN.");
        return -EACCES;
    }

    if (!Shell_PinIsValid(argv[2])) {
        shell_error(sh, "Ungueltige PIN: 4-6 Ziffern erforderlich.");
        return -EINVAL;
    }

    Shell_PinStore(argv[2]);
    shell_print(sh, "PIN erfolgreich geaendert.");

    return 0;
}

/* ------------------------------------------------------------------ */
/* config reset                                           CFG-REQ-03  */
/* ------------------------------------------------------------------ */

static int Shell_CmdConfigReset(const struct shell *sh, size_t argc, char **argv)
{
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if ((argc < 2) || (strncmp(argv[1], "confirm", 8U) != 0)) {
        shell_warn(sh, "Alle Einstellungen werden auf Werkseinstellungen zurueckgesetzt.");
        shell_warn(sh, "Zur Bestaetigung: config reset confirm");
        return 0;
    }

    Shell_PinStore(PIN_DEFAULT);
    shell_print(sh, "Konfiguration auf Werkseinstellungen zurueckgesetzt.");
    shell_print(sh, "PIN zurueckgesetzt auf: " PIN_DEFAULT);
    /* TODO: CFG-REQ-03 — alle Parameter in NVS loeschen */

    return 0;
}

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
    SHELL_CMD_ARG(reset, NULL, "Werkseinstellungen: confirm zum Bestaetigen", Shell_CmdConfigReset, 1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(login,  NULL,        "Anmelden: <pin>",  Shell_CmdLogin);
SHELL_CMD_REGISTER(logout, NULL,        "Abmelden",         Shell_CmdLogout);
SHELL_CMD_REGISTER(wifi,   &sub_wifi,   "WiFi-Konfiguration",   NULL);
SHELL_CMD_REGISTER(mqtt,   &sub_mqtt,   "MQTT-Konfiguration",   NULL);
SHELL_CMD_REGISTER(config, &sub_config, "Systemkonfiguration",  NULL);
