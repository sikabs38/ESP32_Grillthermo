/* SHL-REQ-01, SHL-REQ-06, SHL-REQ-07, SHL-REQ-08, SHL-REQ-10: Shell, Login, Bootmeldung, Bootloader, Version */
#include "config.h"
#include "wifi.h"
#include "bluetooth.h"
#include "temp_data.h"
#include "version.h"

#include <zephyr/kernel.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/shell/shell_uart.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/version.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* SHL-REQ-08: ESP32-S3 RTC-Register fuer ROM-Bootloader (Download-Modus).
 * Adressen gemaess ESP32-S3 Technical Reference Manual, Kap. 7.
 * MISRA 11.4 Abweichung: Hardware-Registerzugriff erfordert Integer-zu-Pointer-Cast. */
#define ESP32S3_RTC_CNTL_BASE         (0x60008000UL)
#define ESP32S3_RTC_CNTL_OPTIONS0_REG (ESP32S3_RTC_CNTL_BASE + 0x000UL)
#define ESP32S3_RTC_CNTL_OPTION1_REG  (ESP32S3_RTC_CNTL_BASE + 0x12CUL)
#define ESP32S3_SW_SYS_RST            (1UL << 31U)
#define ESP32S3_FORCE_DOWNLOAD_BOOT   (1UL)

LOG_MODULE_REGISTER(grill_shell, LOG_LEVEL_ERR);

/* SHL-REQ-07: CPU-Taktfrequenz in MHz zur Anzeige in der Bootmeldung */
#define BOOT_CPU_FREQ_MHZ ((unsigned int)(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC / 1000000U))

/* SHL-REQ-07: CDC-ACM-Geraet fuer DTR-Erkennung (Verbindungsaufbau) */
static const struct device *const g_ShellUartDev =
    DEVICE_DT_GET(DT_CHOSEN(zephyr_shell_uart));

/* CFG-REQ-02, CFG-REQ-04: Aktive Konfiguration im RAM; aus NVS geladen beim Start */
static Config_Data_t g_Config;
/* SHL-REQ-06: Authentifizierungsstatus der aktiven Shell-Sitzung */
static bool          g_Authenticated     = false;
/* SHL-REQ-06: Puffer fuer PIN-Eingabe im Login-Bypass */
static char          g_PinBuf[CFG_PIN_BUF_SIZE];
static size_t        g_PinBufLen         = 0U;
/* SHL-REQ-06: Verhindert doppelten "login: " Prompt */
static bool          g_LoginPromptPrinted = false;
/* SHL-REQ-07: DTR-Zustand des vorherigen Poll-Zyklus; Arbeitseinheit fuer DTR-Polling */
static bool                    g_PrevDtr = false;
static struct k_work_delayable g_DtrWork;
/* SHL-REQ-08: Puffer fuer PIN-Eingabe im Bootloader-Bypass */
static char   g_BootPinBuf[CFG_PIN_BUF_SIZE];
static size_t g_BootPinBufLen        = 0U;
static bool   g_BootPinPromptPrinted = false;
/* SHL-REQ-02: Zwischenspeicher fuer SSID; Passworteingabe-Puffer im WiFi-Bypass */
static char   g_WifiSsidStaging[CFG_WIFI_SSID_MAX_LEN + 1U];
static char   g_WifiPassBuf[CFG_WIFI_PASS_MAX_LEN + 1U];
static size_t g_WifiPassBufLen = 0U;
/* WIF-REQ-06: Hostname */
static char   g_WifiHostnameBuf[CFG_WIFI_HOSTNAME_MAX_LEN + 1U];

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
        shell_error(sh, "Zugang verweigert. Bitte zuerst anmelden.");
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
/* Bootmeldung                                            SHL-REQ-07  */
/* ------------------------------------------------------------------ */

/* SHL-REQ-07: Bootmeldung via printk — aufrufbar ausserhalb Shell-Kontext */
static void Shell_PrintBanner(void)
{
    printk("\n"
           "=========================\n"
           "=== ESP32 Grillthermo ===\n"
           "=== Temperaturmonitor ===\n"
           "=========================\n"
           "Zephyr OS: " KERNEL_VERSION_STRING "\n"
           "CPU:       ESP32-S3 (Xtensa LX7)\n");
    printk("Takt:      %u MHz\n"
           "Version:   " ESP32_GRILLTHERMO_VERSION_STRING "\n"
           "-------------------------\n",
           BOOT_CPU_FREQ_MHZ);
}

/* SHL-REQ-07: Bootmeldung via shell_fprintf — fuer Shell-Kontext (z.B. logout) */
static void Shell_PrintBannerShell(const struct shell *sh)
{
    shell_fprintf(sh, SHELL_NORMAL,
                  "\n"
                  "=========================\n"
                  "=== ESP32 Grillthermo ===\n"
                  "=== Temperaturmonitor ===\n"
                  "=========================\n"
                  "Zephyr OS: " KERNEL_VERSION_STRING "\n"
                  "CPU:       ESP32-S3 (Xtensa LX7)\n");
    shell_fprintf(sh, SHELL_NORMAL,
                  "Takt:      %u MHz\n"
                  "Version:   " ESP32_GRILLTHERMO_VERSION_STRING "\n"
                  "-------------------------\n",
                  BOOT_CPU_FREQ_MHZ);
}

/* SHL-REQ-07: DTR-Polling — erkennt Verbindungsaufbau (0->1 Flanke) */
static void Shell_DtrWork(struct k_work *work)
{
    int  dtr     = 0;
    bool currDtr = false;
    int  rc;

    ARG_UNUSED(work);

    rc = uart_line_ctrl_get(g_ShellUartDev, UART_LINE_CTRL_DTR, &dtr);
    if (rc == 0) {
        currDtr = (bool)(dtr != 0);
        if (currDtr && !g_PrevDtr) {
            Shell_PrintBanner();
            g_LoginPromptPrinted = false;
        }
        g_PrevDtr = currDtr;
    }

    (void)k_work_reschedule(&g_DtrWork, K_MSEC(200));
}

/* ------------------------------------------------------------------ */
/* Login-Bypass                                           SHL-REQ-06  */
/* ------------------------------------------------------------------ */

/* SHL-REQ-06: Bypass-Callback — empfaengt alle Eingaben vor der Shell-Verarbeitung.
 * Zeigt "login: " als Prompt und verdeckt PIN-Zeichen mit '*'.              */
static void Shell_LoginBypass(const struct shell *sh, uint8_t *data,
                              size_t len, void *user_data)
{
    size_t i;

    ARG_UNUSED(user_data);

    if (!g_LoginPromptPrinted) {
        shell_fprintf(sh, SHELL_NORMAL, "login: ");
        g_LoginPromptPrinted = true;
    }

    for (i = 0U; i < len; i++) {
        uint8_t c = data[i];

        if ((c == (uint8_t)'\r') || (c == (uint8_t)'\n')) {
            g_PinBuf[g_PinBufLen] = '\0';
            shell_fprintf(sh, SHELL_NORMAL, "\n");

            if (strncmp(g_PinBuf, g_Config.pin, CFG_PIN_BUF_SIZE) == 0) {
                g_Authenticated = true;
                g_PinBufLen     = 0U;
                (void)shell_obscure_set(sh, false);
                (void)shell_prompt_change(sh, "Grillthermo: ");
                shell_set_bypass(sh, NULL, NULL);
                shell_print(sh, "Anmeldung erfolgreich.");
                if (Shell_PinIsDefault()) {
                    shell_warn(sh,
                        "Warnung: Standard-PIN aktiv. "
                        "Bitte mit \"config pin 000000 <neue-pin>\" aendern.");
                }
                return;
            }

            shell_print(sh, "Falsche PIN. Zugang verweigert.");
            g_PinBufLen = 0U;
            shell_fprintf(sh, SHELL_NORMAL, "login: ");

        } else if ((c == (uint8_t)'\b') || (c == 0x7fU)) {
            if (g_PinBufLen > 0U) {
                g_PinBufLen--;
                shell_fprintf(sh, SHELL_NORMAL, "\b \b");
            }
        } else if (g_PinBufLen < (CFG_PIN_BUF_SIZE - 1U)) {
            g_PinBuf[g_PinBufLen] = (char)c;
            g_PinBufLen++;
            shell_fprintf(sh, SHELL_NORMAL, "*");
        } else {
            /* Puffer voll; Zeichen ignorieren */
        }
    }
}

/* SHL-REQ-06: Login-Modus aktivieren — Bypass setzen und Zustand zuruecksetzen */
static void Shell_LoginSetup(const struct shell *sh)
{
    g_Authenticated      = false;
    g_PinBufLen          = 0U;
    g_PinBuf[0]          = '\0';
    g_LoginPromptPrinted = false;
    shell_set_bypass(sh, Shell_LoginBypass, NULL);
}

/* ------------------------------------------------------------------ */
/* logout                                                 SHL-REQ-06  */
/* ------------------------------------------------------------------ */

static int Shell_CmdLogout(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    shell_print(sh, "Abgemeldet.");
    Shell_PrintBannerShell(sh);
    Shell_LoginSetup(sh);
    shell_fprintf(sh, SHELL_NORMAL, "login: ");
    g_LoginPromptPrinted = true;

    return 0;
}

/* ------------------------------------------------------------------ */
/* wifi set — Passwort-Bypass                             SHL-REQ-02  */
/* ------------------------------------------------------------------ */

/* SHL-REQ-02: Bypass-Callback — nimmt WiFi-Passwort verdeckt entgegen. */
static void Shell_WifiPasswordBypass(const struct shell *sh, uint8_t *data,
                                     size_t len, void *user_data)
{
    int    rc;
    size_t i;

    ARG_UNUSED(user_data);

    for (i = 0U; i < len; i++) {
        uint8_t c = data[i];

        if ((c == (uint8_t)'\r') || (c == (uint8_t)'\n')) {
            g_WifiPassBuf[g_WifiPassBufLen] = '\0';
            shell_fprintf(sh, SHELL_NORMAL, "\n");

            if (g_WifiPassBufLen == 0U) {
                shell_error(sh, "Passwort darf nicht leer sein.");
                g_WifiPassBufLen = 0U;
                shell_fprintf(sh, SHELL_NORMAL, "Passwort: ");
                return;
            }

            /* SSID und Passwort in Konfiguration uebernehmen */
            (void)strncpy(g_Config.wifiSsid, g_WifiSsidStaging,
                          CFG_WIFI_SSID_MAX_LEN);
            g_Config.wifiSsid[CFG_WIFI_SSID_MAX_LEN] = '\0';
            (void)strncpy(g_Config.wifiPassword, g_WifiPassBuf,
                          CFG_WIFI_PASS_MAX_LEN);
            g_Config.wifiPassword[CFG_WIFI_PASS_MAX_LEN] = '\0';
            /* TODO: CFG-REQ-05 — Passwort vor dem Speichern AES-verschluesseln */

            rc = Config_Save(&g_Config);

            /* Bypass vor shell_print beenden, damit der Shell-Prompt korrekt erscheint */
            g_WifiPassBufLen = 0U;
            g_WifiPassBuf[0] = '\0';
            (void)shell_obscure_set(sh, false);
            shell_set_bypass(sh, NULL, NULL);

            if (rc < 0) {
                shell_error(sh, "Speicherfehler: %d", rc);
            } else {
                shell_print(sh, "WiFi SSID gesetzt: %s", g_Config.wifiSsid);
                /* WIF-REQ-04: Neuen Verbindungsversuch ausloesen */
                Wifi_Reconnect();
            }

            return;

        } else if ((c == (uint8_t)'\b') || (c == 0x7fU)) {
            if (g_WifiPassBufLen > 0U) {
                g_WifiPassBufLen--;
                shell_fprintf(sh, SHELL_NORMAL, "\b \b");
            }
        } else if (g_WifiPassBufLen < CFG_WIFI_PASS_MAX_LEN) {
            g_WifiPassBuf[g_WifiPassBufLen] = (char)c;
            g_WifiPassBufLen++;
            shell_fprintf(sh, SHELL_NORMAL, "*");
        } else {
            /* Puffer voll (64 Zeichen); Zeichen ignorieren */
        }
    }
}

/* ------------------------------------------------------------------ */
/* wifi                                                   SHL-REQ-02  */
/* ------------------------------------------------------------------ */

static int Shell_CmdWifiSet(const struct shell *sh, size_t argc, char **argv)
{
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 2) {
        shell_error(sh, "Verwendung: wifi set <ssid>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > CFG_WIFI_SSID_MAX_LEN) {
        shell_error(sh, "SSID zu lang (max. %u Zeichen).", (unsigned int)CFG_WIFI_SSID_MAX_LEN);
        return -EINVAL;
    }

    /* SSID zwischenspeichern; Passwort wird im Bypass abgefragt */
    (void)strncpy(g_WifiSsidStaging, argv[1], CFG_WIFI_SSID_MAX_LEN);
    g_WifiSsidStaging[CFG_WIFI_SSID_MAX_LEN] = '\0';

    g_WifiPassBufLen = 0U;
    g_WifiPassBuf[0] = '\0';
    (void)shell_obscure_set(sh, true);
    shell_set_bypass(sh, Shell_WifiPasswordBypass, NULL);
    shell_fprintf(sh, SHELL_NORMAL, "Passwort: ");

    return 0;
}

/* ------------------------------------------------------------------ */
/* wifi hostname                                          WIF-REQ-06  */
/* ------------------------------------------------------------------ */

static int Shell_CmdWifiHostname(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc != 2) {
        shell_error(sh, "Verwendung: wifi hostname <name>");
        return -EINVAL;
    }

    if (strlen(argv[1]) > CFG_WIFI_HOSTNAME_MAX_LEN) {
        shell_error(sh, "Hostname zu lang (max. %u Zeichen).",
                    (unsigned int)CFG_WIFI_HOSTNAME_MAX_LEN);
        return -EINVAL;
    }

    (void)strncpy(g_WifiHostnameBuf, argv[1], CFG_WIFI_HOSTNAME_MAX_LEN);
    g_WifiHostnameBuf[CFG_WIFI_HOSTNAME_MAX_LEN] = '\0';

    (void)strncpy(g_Config.wifiHostname, g_WifiHostnameBuf, CFG_WIFI_HOSTNAME_MAX_LEN);
    g_Config.wifiHostname[CFG_WIFI_HOSTNAME_MAX_LEN] = '\0';

    rc = Config_Save(&g_Config);

    if (rc < 0) {
        shell_error(sh, "Speicherfehler: %d", rc);
        return rc;
    }

    shell_print(sh, "Hostname gesetzt: %s", g_Config.wifiHostname);
    /* WIF-REQ-06: Verbindung neu aufbauen damit Hostname sofort per DHCP announciert wird */
    Wifi_Reconnect();

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

    shell_print(sh, "--- ESP32 Grillthermo Konfiguration ---");
    shell_print(sh, "WiFi SSID    : %s",
                (g_Config.wifiSsid[0] != '\0') ? g_Config.wifiSsid : "[nicht gesetzt]");
    /* SHL-REQ-05, CFG-REQ-05: Passwort nie im Klartext ausgeben */
    shell_print(sh, "WiFi Passwort: ********");
    shell_print(sh, "Hostname     : %s",
                (g_Config.wifiHostname[0] != '\0') ? g_Config.wifiHostname : "[nicht gesetzt]");
    /* BLE-REQ-07: Grill-MAC im config-show ausgeben */
    shell_print(sh, "Grill MAC    : %s",
                (g_Config.grillMac[0] != '\0') ? g_Config.grillMac : "[nicht gesetzt]");

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
/* bootloader                                             SHL-REQ-08  */
/* ------------------------------------------------------------------ */

/* SHL-REQ-08: Bypass-Callback — nimmt PIN-Eingabe entgegen, wechselt bei
 * korrekter PIN in den ROM-Download-Modus des ESP32-S3.               */
static void Shell_BootloaderPinBypass(const struct shell *sh, uint8_t *data,
                                      size_t len, void *user_data)
{
    /* MISRA 11.4: volatile-Pointer fuer Hardware-Registerzugriff */
    volatile uint32_t *const opt0 = (volatile uint32_t *)ESP32S3_RTC_CNTL_OPTIONS0_REG;
    volatile uint32_t *const opt1 = (volatile uint32_t *)ESP32S3_RTC_CNTL_OPTION1_REG;
    size_t i;

    ARG_UNUSED(user_data);

    for (i = 0U; i < len; i++) {
        uint8_t c = data[i];

        if ((c == (uint8_t)'\r') || (c == (uint8_t)'\n')) {
            g_BootPinBuf[g_BootPinBufLen] = '\0';
            shell_fprintf(sh, SHELL_NORMAL, "\n");

            if (strncmp(g_BootPinBuf, g_Config.pin, CFG_PIN_BUF_SIZE) == 0) {
                shell_print(sh, "PIN korrekt. Wechsle in den Download-Modus ...");
                /* SHL-REQ-08: FORCE_DOWNLOAD_BOOT setzen, dann SW-System-Reset.
                 * memw: Xtensa-Speicherbarriere — sichert Commit vor Reset-Trigger. */
                *opt1 = *opt1 | ESP32S3_FORCE_DOWNLOAD_BOOT;
                __asm__ volatile("memw" ::: "memory");
                *opt0 = *opt0 | ESP32S3_SW_SYS_RST;
                /* Ab hier kein Rueckkehr */
            } else {
                shell_error(sh, "Falsche PIN. Vorgang abgebrochen.");
            }

            /* Bypass beenden (nur bei falscher PIN erreichbar) */
            g_BootPinBufLen        = 0U;
            g_BootPinBuf[0]        = '\0';
            g_BootPinPromptPrinted = false;
            (void)shell_obscure_set(sh, false);
            shell_set_bypass(sh, NULL, NULL);
            return;

        } else if ((c == (uint8_t)'\b') || (c == 0x7fU)) {
            if (g_BootPinBufLen > 0U) {
                g_BootPinBufLen--;
                shell_fprintf(sh, SHELL_NORMAL, "\b \b");
            }
        } else if (g_BootPinBufLen < (CFG_PIN_BUF_SIZE - 1U)) {
            g_BootPinBuf[g_BootPinBufLen] = (char)c;
            g_BootPinBufLen++;
            shell_fprintf(sh, SHELL_NORMAL, "*");
        } else {
            /* Puffer voll; Zeichen ignorieren */
        }
    }
}

static int Shell_CmdBootloader(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    g_BootPinBufLen        = 0U;
    g_BootPinBuf[0]        = '\0';
    g_BootPinPromptPrinted = true;
    (void)shell_obscure_set(sh, true);
    shell_set_bypass(sh, Shell_BootloaderPinBypass, NULL);
    shell_fprintf(sh, SHELL_NORMAL, "Pin: ");

    return 0;
}

/* ------------------------------------------------------------------ */
/* Initialisierung                              CFG-REQ-02, SHL-REQ-01 */
/* ------------------------------------------------------------------ */

static int Shell_LoadConfig(void)
{
    int                rc;
    const struct shell *sh;

    rc = Config_Init();

    if (rc < 0) {
        LOG_ERR("Config-Initialisierungsfehler: %d; Standardwerte aktiv.", rc);
        Config_GetDefaults(&g_Config);
    } else {
        rc = Config_Load(&g_Config);

        if (rc != 0) {
            LOG_WRN("Konfiguration nicht geladen (rc=%d); Standardwerte aktiv.", rc);
        }
    }

    /* SHL-REQ-06: Login-Bypass vor erster Nutzereingabe aktivieren */
    sh = shell_backend_uart_get_ptr();
    if (sh != NULL) {
        Shell_LoginSetup(sh);
    }

    /* SHL-REQ-07: DTR-Polling starten — 500 ms Verzoegerung fuer USB-Initialisierung */
    k_work_init_delayable(&g_DtrWork, Shell_DtrWork);
    (void)k_work_schedule(&g_DtrWork, K_MSEC(500));

    return 0;
}

SYS_INIT(Shell_LoadConfig, APPLICATION, 0);

/* ------------------------------------------------------------------ */
/* Befehlsbaum                                                        */
/* ------------------------------------------------------------------ */

/* wifi scan                                              WIF-REQ-08  */
static int Shell_CmdWifiScan(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    rc = Wifi_Scan();
    if (rc == -EBUSY) {
        shell_print(sh, "WiFi: Scan laeuft bereits.");
        return 0;
    }
    if (rc != 0) {
        shell_error(sh, "WiFi: Scan konnte nicht gestartet werden (%d).", rc);
        return rc;
    }

    shell_print(sh, "WiFi: Scan gestartet...");
    return 0;
}

static int Shell_CmdWifiReconnect(const struct shell *sh, size_t argc, char **argv)
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    Wifi_Reconnect();
    shell_print(sh, "WiFi: Verbindungsversuch gestartet.");

    return 0;
}

/* SHL-REQ-09, WIF-REQ-05: WiFi-Verbindungsstatus anzeigen */
static int Shell_CmdWifiStatus(const struct shell *sh, size_t argc, char **argv)
{
    Wifi_Status_t status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    Wifi_GetStatus(&status);

    if (status.connected) {
        shell_print(sh, "WiFi Status  : Verbunden");
        shell_print(sh, "SSID         : %s", status.ssid);
        shell_print(sh, "IP-Adresse   : %s", status.ip);
    } else {
        shell_print(sh, "WiFi Status  : Getrennt");
        if (status.ssid[0] != '\0') {
            shell_print(sh, "SSID         : %s", status.ssid);
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* temp — Testbefehl zum manuellen Setzen von Messwerten   TMP-REQ-03 */
/* ------------------------------------------------------------------ */

/* Gruppenname in TEMP_GROUP_* uebersetzen */
static int Shell_TempParseGroup(const char *name, uint8_t *group)
{
    if (strcmp(name, "burner") == 0) {
        *group = (uint8_t)TEMP_GROUP_BURNER;
    } else if (strcmp(name, "core") == 0) {
        *group = (uint8_t)TEMP_GROUP_CORE;
    } else {
        return -EINVAL;
    }

    return 0;
}

/* Zone (1..TEMP_ZONE_COUNT) parsen und auf 0-basierten Index pruefen */
static int Shell_TempParseZone(const char *arg, uint8_t *zoneIdx)
{
    char         *endPtr = NULL;
    unsigned long zone;

    zone = strtoul(arg, &endPtr, 10);

    if ((endPtr == arg) || (*endPtr != '\0') ||
        (zone < 1UL) || (zone > (unsigned long)TEMP_ZONE_COUNT)) {
        return -EINVAL;
    }

    *zoneIdx = (uint8_t)(zone - 1UL);

    return 0;
}

/* TMP-REQ-03: Einzelwert setzen (valid=true), loest SSE-Push aus (WEB-REQ-06) */
static int Shell_CmdTempSet(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t group;
    uint8_t zoneIdx;
    long    value;
    char   *endPtr = NULL;
    int     rc;

    ARG_UNUSED(argc);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (Shell_TempParseGroup(argv[1], &group) != 0) {
        shell_error(sh, "Ungueltige Gruppe: burner oder core.");
        return -EINVAL;
    }

    if (Shell_TempParseZone(argv[2], &zoneIdx) != 0) {
        shell_error(sh, "Ungueltige Zone (1-%u).", (unsigned int)TEMP_ZONE_COUNT);
        return -EINVAL;
    }

    value = strtol(argv[3], &endPtr, 10);

    if ((endPtr == argv[3]) || (*endPtr != '\0') ||
        (value < -32768L) || (value > 32767L)) {
        shell_error(sh, "Ungueltiger Wert (-32768..32767).");
        return -EINVAL;
    }

    rc = Temp_Set(group, zoneIdx, (int16_t)value, true);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Setzen: %d", rc);
        return rc;
    }

    shell_print(sh, "Gesetzt: %s Zone %s = %ld", argv[1], argv[2], value);

    return 0;
}

/* TMP-REQ-03: Wert als ungueltig markieren (Anzeige "--"), loest SSE-Push aus */
static int Shell_CmdTempClear(const struct shell *sh, size_t argc, char **argv)
{
    uint8_t group;
    uint8_t zoneIdx;
    int     rc;

    ARG_UNUSED(argc);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (Shell_TempParseGroup(argv[1], &group) != 0) {
        shell_error(sh, "Ungueltige Gruppe: burner oder core.");
        return -EINVAL;
    }

    if (Shell_TempParseZone(argv[2], &zoneIdx) != 0) {
        shell_error(sh, "Ungueltige Zone (1-%u).", (unsigned int)TEMP_ZONE_COUNT);
        return -EINVAL;
    }

    rc = Temp_Set(group, zoneIdx, (int16_t)0, false);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Loeschen: %d", rc);
        return rc;
    }

    shell_print(sh, "Geloescht: %s Zone %s (--)", argv[1], argv[2]);

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_wifi,
    SHELL_CMD_ARG(set,      NULL, "WiFi konfigurieren: <ssid>",       Shell_CmdWifiSet,      2, 0),
    SHELL_CMD_ARG(hostname, NULL, "Hostnamen setzen: <name>",         Shell_CmdWifiHostname, 2, 0),
    SHELL_CMD_ARG(reconnect,NULL, "WiFi-Verbindung neu aufbauen",     Shell_CmdWifiReconnect,1, 0),
    SHELL_CMD(    status,   NULL, "WiFi-Status anzeigen",             Shell_CmdWifiStatus),
    SHELL_CMD(    scan,     NULL, "Verfuegbare WLAN-Netze anzeigen",  Shell_CmdWifiScan),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_config,
    SHELL_CMD(show, NULL, "Konfiguration anzeigen", Shell_CmdConfigShow),
    SHELL_CMD_ARG(pin, NULL, "PIN aendern: <alte-pin> <neue-pin>", Shell_CmdConfigPin, 3, 0),
    SHELL_CMD_ARG(reset, NULL, "Werkseinstellungen: confirm zum Bestaetigen",
                  Shell_CmdConfigReset, 1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_temp,
    SHELL_CMD_ARG(set,   NULL, "Wert setzen: <burner|core> <1-4> <wert>",
                  Shell_CmdTempSet,   4, 0),
    SHELL_CMD_ARG(clear, NULL, "Wert auf -- setzen: <burner|core> <1-4>",
                  Shell_CmdTempClear, 3, 0),
    SHELL_SUBCMD_SET_END
);

/* ------------------------------------------------------------------ */
/* gas — Testbefehl fuer den Gasflaschen-Fuellstand        DSP-REQ-06 */
/* ------------------------------------------------------------------ */

static int Shell_CmdGasSet(const struct shell *sh, size_t argc, char **argv)
{
    long  value;
    char *endPtr = NULL;
    int   rc;

    ARG_UNUSED(argc);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    value = strtol(argv[1], &endPtr, 10);

    if ((endPtr == argv[1]) || (*endPtr != '\0') ||
        (value < 0L) || (value > 100L)) {
        shell_error(sh, "Ungueltiger Wert (0-100).");
        return -EINVAL;
    }

    rc = Temp_SetGas((int16_t)value, true);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Setzen: %d", rc);
        return rc;
    }

    shell_print(sh, "Gasflasche: %ld%%", value);

    return 0;
}

static int Shell_CmdGasClear(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    rc = Temp_SetGas((int16_t)0, false);

    if (rc < 0) {
        shell_error(sh, "Fehler beim Loeschen: %d", rc);
        return rc;
    }

    shell_print(sh, "Gasflasche: --");

    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_gas,
    SHELL_CMD_ARG(set,   NULL, "Fuellstand setzen: <0-100>", Shell_CmdGasSet,   2, 0),
    SHELL_CMD(    clear, NULL, "Fuellstand auf -- setzen",  Shell_CmdGasClear),
    SHELL_SUBCMD_SET_END
);

/* ------------------------------------------------------------------ */
/* bt — Bluetooth-Kopplung mit Otto Wilde G32              BLE-REQ-03 */
/* ------------------------------------------------------------------ */

/* BLE-REQ-03: MAC-Format "AA:BB:CC:DD:EE:FF" pruefen */
static bool Shell_BtMacIsValid(const char *mac)
{
    size_t i;

    if (mac == NULL) {
        return false;
    }
    if (strlen(mac) != CFG_GRILL_MAC_STR_LEN) {
        return false;
    }
    for (i = 0U; i < CFG_GRILL_MAC_STR_LEN; i++) {
        char c = mac[i];
        if (((i + 1U) % 3U) == 0U) {
            if (c != ':') {
                return false;
            }
        } else {
            bool isDigit = (c >= '0') && (c <= '9');
            bool isUpper = (c >= 'A') && (c <= 'F');
            bool isLower = (c >= 'a') && (c <= 'f');
            if (!isDigit && !isUpper && !isLower) {
                return false;
            }
        }
    }
    return true;
}

/* BLE-REQ-03: bt scan [all] */
static int Shell_CmdBtScan(const struct shell *sh, size_t argc, char **argv)
{
    bool filterByG32 = true;
    int  rc;

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc >= 2) {
        if (strcmp(argv[1], "all") == 0) {
            filterByG32 = false;
        } else if (strcmp(argv[1], "stop") == 0) {
            rc = Bluetooth_ScanStop();
            if (rc < 0) {
                shell_error(sh, "Scan-Stop fehlgeschlagen: %d", rc);
                return rc;
            }
            return 0;
        } else {
            shell_error(sh, "Verwendung: bt scan [all|stop]");
            return -EINVAL;
        }
    }

    rc = Bluetooth_ScanStart(filterByG32);
    if (rc == -EALREADY) {
        shell_warn(sh, "Scan laeuft bereits.");
        return 0;
    }
    if (rc < 0) {
        shell_error(sh, "Scan-Start fehlgeschlagen: %d", rc);
        return rc;
    }
    return 0;
}

/* BLE-REQ-03/07/08: bt pair <mac> */
static int Shell_CmdBtPair(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    ARG_UNUSED(argc);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (!Shell_BtMacIsValid(argv[1])) {
        shell_error(sh, "Ungueltige MAC. Format: AA:BB:CC:DD:EE:FF");
        return -EINVAL;
    }

    /* BLE-REQ-07: MAC persistent in g_Config ablegen */
    (void)strncpy(g_Config.grillMac, argv[1], CFG_GRILL_MAC_STR_LEN);
    g_Config.grillMac[CFG_GRILL_MAC_STR_LEN] = '\0';

    rc = Config_Save(&g_Config);
    if (rc < 0) {
        shell_error(sh, "Speicherfehler: %d", rc);
        return rc;
    }

    shell_print(sh, "Grill-MAC gespeichert: %s", g_Config.grillMac);
    /* BLE-REQ-08: Verbindungsversuch ausloesen */
    (void)Bluetooth_Reconnect();
    return 0;
}

/* BLE-REQ-03/07: bt unpair */
static int Shell_CmdBtUnpair(const struct shell *sh, size_t argc, char **argv)
{
    int rc;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (g_Config.grillMac[0] == '\0') {
        shell_print(sh, "Keine Kopplung gespeichert.");
        return 0;
    }

    g_Config.grillMac[0] = '\0';
    rc = Config_Save(&g_Config);
    if (rc < 0) {
        shell_error(sh, "Speicherfehler: %d", rc);
        return rc;
    }

    (void)Bluetooth_Disconnect();
    shell_print(sh, "Kopplung entfernt.");
    return 0;
}

/* BLE-REQ-10: bt status */
static int Shell_CmdBtStatus(const struct shell *sh, size_t argc, char **argv)
{
    Bluetooth_Status_t status;

    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    Bluetooth_GetStatus(&status);

    shell_print(sh, "BT Status    : %s",
                status.connected ? "Verbunden" : "Getrennt");
    shell_print(sh, "Gekoppelt    : %s",
                status.paired ? "Ja" : "Nein");
    if (status.peerMac[0] != '\0') {
        shell_print(sh, "Grill MAC    : %s", status.peerMac);
    }
    if (status.connected) {
        int64_t  nowMs = k_uptime_get();
        uint32_t ageMs = (uint32_t)(nowMs - (int64_t)status.lastPacketUptimeMs);
        shell_print(sh, "Letztes Paket: vor %u ms", (unsigned int)ageMs);
    }
    return 0;
}

/* Diagnose: bt sniff on|off — Notify-Payloads als Hex-Dump + Decodierung */
static int Shell_CmdBtSniff(const struct shell *sh, size_t argc, char **argv)
{
    if (!Shell_CheckAuth(sh)) {
        return -EACCES;
    }

    if (argc < 2) {
        shell_print(sh, "Sniff ist %s.", Bluetooth_GetSniff() ? "an" : "aus");
        shell_print(sh, "Verwendung: bt sniff <on|off>");
        return 0;
    }

    if (strcmp(argv[1], "on") == 0) {
        Bluetooth_SetSniff(true);
        shell_print(sh, "Sniff aktiviert. Vorsicht: kann die Konsole fluten.");
    } else if (strcmp(argv[1], "off") == 0) {
        Bluetooth_SetSniff(false);
        shell_print(sh, "Sniff deaktiviert.");
    } else {
        shell_error(sh, "Verwendung: bt sniff <on|off>");
        return -EINVAL;
    }
    return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_bt,
    SHELL_CMD_ARG(scan,   NULL,
                  "Geraete-Scan: [all] alle, [stop] beenden, sonst nur G32",
                  Shell_CmdBtScan,   1, 1),
    SHELL_CMD_ARG(pair,   NULL,
                  "Kopplung speichern und verbinden: <AA:BB:CC:DD:EE:FF>",
                  Shell_CmdBtPair,   2, 0),
    SHELL_CMD(    unpair, NULL,
                  "Kopplung entfernen und Verbindung trennen",
                  Shell_CmdBtUnpair),
    SHELL_CMD(    status, NULL,
                  "Bluetooth-Status anzeigen",
                  Shell_CmdBtStatus),
    SHELL_CMD_ARG(sniff,  NULL,
                  "Notify-Payloads als Hex-Dump + aktuelle Decodierung: <on|off>",
                  Shell_CmdBtSniff,  1, 1),
    SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(logout,     NULL,        "Abmelden",                         Shell_CmdLogout);
SHELL_CMD_REGISTER(wifi,       &sub_wifi,   "WiFi-Konfiguration",               NULL);
SHELL_CMD_REGISTER(config,     &sub_config, "Systemkonfiguration",              NULL);
SHELL_CMD_REGISTER(temp,       &sub_temp,   "Temperaturwerte setzen (Test)",    NULL);
SHELL_CMD_REGISTER(gas,        &sub_gas,    "Gasflaschen-Fuellstand setzen (Test)", NULL);
SHELL_CMD_REGISTER(bt,         &sub_bt,     "Bluetooth-Kopplung mit Otto Wilde G32", NULL);
SHELL_CMD_REGISTER(bootloader, NULL,        "In Download-Modus wechseln",       Shell_CmdBootloader);
