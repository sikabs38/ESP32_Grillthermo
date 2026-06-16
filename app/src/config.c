/* CFG-REQ-01, CFG-REQ-02, CFG-REQ-03, CFG-REQ-05, CFG-REQ-06,
 * CFG-NFR-01, CFG-NFR-02, CFG-NFR-03 */
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <psa/crypto.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(config_store, LOG_LEVEL_ERR);

/* CFG-NFR-01: Ausschliesslich Zephyr NVS API, kein direkter Flash-Zugriff */
/* CFG-NFR-02: Alle Puffer statisch alloziert, keine dynamische Speicherverwaltung */

/* ESP32-S3: Flash-Loeschseite 4 KB; storage_partition 192 KB = 48 Sektoren */
#define NVS_SECTOR_SIZE  (0x1000U)
#define NVS_SECTOR_COUNT (48U)
#define NVS_SLOT_COUNT   (3U)
#define NVS_ID_BASE      (1U)

/* CFG-REQ-05: AES-128-CBC-Parameter */
#define AES_BLOCK_SIZE  (16U)
#define AES_KEY_BITS    (128U)
#define AES_KEY_LEN     (AES_KEY_BITS / 8U)  /* 16 Byte */
#define CFG_IV_LEN      (AES_BLOCK_SIZE)       /* PSA schreibt IV vor Chiffrat */

/* Naechstes AES-Block-Vielfaches >= sizeof(Config_Data_t) fuer CBC-Ausrichtung */
#define CFG_ENC_SIZE \
    (((sizeof(Config_Data_t) + AES_BLOCK_SIZE - 1U) / AES_BLOCK_SIZE) * AES_BLOCK_SIZE)

/* PSA-Ausgabepuffer: IV (16 B) || Chiffrat (CFG_ENC_SIZE B) */
#define CFG_BUF_SIZE    (CFG_IV_LEN + CFG_ENC_SIZE)

/* CFG-REQ-05: AES-128-Schluessel (Compile-Zeit-Secret).
 * Fuer die Produktion durch einen geraetespezifischen Wert ersetzen,
 * z.B. aus dem ESP32-S3-eFuse abgeleitet. */
static const uint8_t k_AesKey[AES_KEY_LEN] = {
    0x47U, 0x72U, 0x69U, 0x6CU, 0x6CU, 0x74U, 0x68U, 0x65U,
    0x72U, 0x6DU, 0x6FU, 0x21U, 0x45U, 0x53U, 0x50U, 0x33U
};

/* CFG-REQ-05, CFG-REQ-06: Im NVS gespeicherter Datensatz.
 * encData enthaelt PSA-Ausgabe: IV (16 B) || AES-128-CBC-Chiffrat (CFG_ENC_SIZE B).
 * crc muss das letzte Feld sein und deckt alle vorigen Felder ab. */
typedef struct {
    uint8_t  generation;
    bool     valid;
    uint8_t  encData[CFG_BUF_SIZE];
    uint32_t crc;
} Config_Stored_t;

static struct nvs_fs g_NvsFs;
static bool          g_Initialized = false;

/* CFG-REQ-05: AES-128-CBC-Verschluesselung von Config_Data_t via PSA.
 * encOut zeigt auf Puffer der Groesse CFG_BUF_SIZE (IV || Chiffrat).
 * Gibt 0 bei Erfolg, -EIO bei PSA-Fehler. */
static int Config_Encrypt(const Config_Data_t *plain, uint8_t *encOut)
{
    static uint8_t       plainBuf[CFG_ENC_SIZE];
    psa_key_attributes_t attrs  = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t keyId  = MBEDTLS_SVC_KEY_ID_INIT;
    size_t               outLen = 0U;
    psa_status_t         st;

    (void)memset(plainBuf, 0, CFG_ENC_SIZE);
    (void)memcpy(plainBuf, plain, sizeof(Config_Data_t));

    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_ENCRYPT);
    psa_set_key_algorithm(&attrs, PSA_ALG_CBC_NO_PADDING);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, (psa_key_bits_t)AES_KEY_BITS);

    st = psa_import_key(&attrs, k_AesKey, AES_KEY_LEN, &keyId);
    if (st == PSA_SUCCESS) {
        st = psa_cipher_encrypt(keyId, PSA_ALG_CBC_NO_PADDING,
                                plainBuf, CFG_ENC_SIZE,
                                encOut, CFG_BUF_SIZE, &outLen);
        (void)psa_destroy_key(keyId);
    }

    (void)memset(plainBuf, 0, CFG_ENC_SIZE);

    if (st != PSA_SUCCESS) {
        LOG_ERR("AES-Verschluesselung fehlgeschlagen: %d", (int)st);
        return -EIO;
    }

    return 0;
}

/* CFG-REQ-05: AES-128-CBC-Entschluesselung via PSA.
 * encIn zeigt auf Puffer der Groesse CFG_BUF_SIZE (IV || Chiffrat).
 * Gibt 0 bei Erfolg, -EIO bei PSA-Fehler. */
static int Config_Decrypt(const uint8_t *encIn, Config_Data_t *plain)
{
    static uint8_t       decBuf[CFG_ENC_SIZE];
    psa_key_attributes_t attrs  = PSA_KEY_ATTRIBUTES_INIT;
    mbedtls_svc_key_id_t keyId  = MBEDTLS_SVC_KEY_ID_INIT;
    size_t               outLen = 0U;
    psa_status_t         st;

    psa_set_key_usage_flags(&attrs, PSA_KEY_USAGE_DECRYPT);
    psa_set_key_algorithm(&attrs, PSA_ALG_CBC_NO_PADDING);
    psa_set_key_type(&attrs, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attrs, (psa_key_bits_t)AES_KEY_BITS);

    st = psa_import_key(&attrs, k_AesKey, AES_KEY_LEN, &keyId);
    if (st == PSA_SUCCESS) {
        st = psa_cipher_decrypt(keyId, PSA_ALG_CBC_NO_PADDING,
                                encIn, CFG_BUF_SIZE,
                                decBuf, CFG_ENC_SIZE, &outLen);
        (void)psa_destroy_key(keyId);
    }

    if (st == PSA_SUCCESS) {
        (void)memcpy(plain, decBuf, sizeof(Config_Data_t));
    }

    /* CFG-REQ-05: Klartext nach Verwendung im RAM loeschen */
    (void)memset(decBuf, 0, CFG_ENC_SIZE);

    if (st != PSA_SUCCESS) {
        LOG_ERR("AES-Entschluesselung fehlgeschlagen: %d", (int)st);
        return -EIO;
    }

    return 0;
}

/* CFG-REQ-06: CRC32 ueber alle Felder des Datensatzes ausser dem CRC-Feld */
static uint32_t Config_ComputeStoredCrc(const Config_Stored_t *stored)
{
    /* MISRA 11.3: uint8_t*-Cast erforderlich fuer byte-weisen CRC-Zugriff */
    return crc32_ieee((const uint8_t *)stored, offsetof(Config_Stored_t, crc));
}

/* CFG-REQ-06: Prueft Gueltigkeitsflag und CRC */
static bool Config_IsStoredValid(const Config_Stored_t *stored)
{
    return stored->valid && (stored->crc == Config_ComputeStoredCrc(stored));
}

/* CFG-REQ-06: Neueste gueltige Generation unter den drei Slots suchen.
 * Rueckgabe: Slot-Index [0..2] oder -1 wenn kein gueltiger Eintrag. */
static int Config_FindNewest(const Config_Stored_t stored[NVS_SLOT_COUNT])
{
    int    newestIdx = -1;
    size_t i;

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        if (!Config_IsStoredValid(&stored[i])) {
            continue;
        }

        if (newestIdx < 0) {
            newestIdx = (int)i;
        } else {
            /* Modularer uint8_t-Vergleich sichert korrekte Behandlung des Ueberlaufs */
            uint8_t diff = stored[i].generation - stored[newestIdx].generation;

            if (diff < 128U) {
                newestIdx = (int)i;
            }
        }
    }

    return newestIdx;
}

/* CFG-REQ-02: Konfiguration aus NVS laden; bei Fehler Standardwerte verwenden */
int Config_Load(Config_Data_t *data)
{
    static Config_Stored_t records[NVS_SLOT_COUNT];
    uint16_t               id;
    int                    newestIdx;
    ssize_t                rc;
    size_t                 i;
    int                    decRc;

    if (data == NULL) {
        return -EINVAL;
    }

    if (!g_Initialized) {
        Config_GetDefaults(data);
        return -ENODEV;
    }

    (void)memset(records, 0, sizeof(records));

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        id = (uint16_t)(NVS_ID_BASE + i);
        rc = nvs_read(&g_NvsFs, id, &records[i], sizeof(Config_Stored_t));

        if (rc == (ssize_t)sizeof(Config_Stored_t)) {
            /* Vollstaendiger Datensatz */
        } else if (rc == -ENOENT) {
            /* Slot leer */
        } else if (rc > 0) {
            /* Datensatz aus anderer Firmware-Version — CRC-Pruefung schlaegt fehl */
            LOG_WRN("NVS-Slot %u: Unbekannte Datensatzgroesse (%d); ignoriert.",
                    (unsigned int)id, (int)rc);
            (void)memset(&records[i], 0, sizeof(Config_Stored_t));
        } else {
            /* CFG-NFR-03: Echter Lesefehler loggen */
            LOG_ERR("NVS-Lesefehler Slot %u: %d", (unsigned int)id, (int)rc);
        }
    }

    newestIdx = Config_FindNewest(records);

    if (newestIdx < 0) {
        /* CFG-NFR-03: Kein gueltiger Eintrag — Standardwerte */
        LOG_INF("Keine gueltige Konfiguration; Standardwerte aktiv.");
        Config_GetDefaults(data);
        return -ENOENT;
    }

    decRc = Config_Decrypt(records[newestIdx].encData, data);

    if (decRc != 0) {
        /* CFG-NFR-03: Entschluesselung fehlgeschlagen */
        Config_GetDefaults(data);
        return decRc;
    }

    LOG_INF("Konfiguration geladen (Generation %u).",
            (unsigned int)records[newestIdx].generation);

    return 0;
}

/* CFG-REQ-01: Konfiguration in NVS speichern.
 * Schreibvorgang wird vor der Rueckkehr abgeschlossen. */
int Config_Save(const Config_Data_t *data)
{
    static Config_Stored_t records[NVS_SLOT_COUNT];
    static Config_Stored_t newRecord;
    uint16_t               writeId;
    int                    newestIdx;
    ssize_t                rc;
    size_t                 i;
    int                    encRc;

    if (data == NULL) {
        return -EINVAL;
    }

    if (!g_Initialized) {
        return -ENODEV;
    }

    (void)memset(records, 0, sizeof(records));

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        uint16_t id = (uint16_t)(NVS_ID_BASE + i);

        (void)nvs_read(&g_NvsFs, id, &records[i], sizeof(Config_Stored_t));
    }

    newestIdx = Config_FindNewest(records);

    if (newestIdx < 0) {
        newRecord.generation = 0U;
    } else {
        newRecord.generation = (uint8_t)(records[newestIdx].generation + 1U);
    }

    newRecord.valid = true;

    /* CFG-REQ-05: Daten vor dem Schreiben verschluesseln */
    (void)memset(newRecord.encData, 0, CFG_BUF_SIZE);
    encRc = Config_Encrypt(data, newRecord.encData);

    if (encRc != 0) {
        return encRc;
    }

    newRecord.crc = Config_ComputeStoredCrc(&newRecord);

    /* CFG-REQ-06: Slot-Zuweisung per Modulo */
    writeId = (uint16_t)(NVS_ID_BASE + (newRecord.generation % (uint8_t)NVS_SLOT_COUNT));

    rc = nvs_write(&g_NvsFs, writeId, &newRecord, sizeof(Config_Stored_t));

    if (rc < 0) {
        /* CFG-NFR-03: Schreibfehler loggen */
        LOG_ERR("NVS-Schreibfehler Slot %u: %d", (unsigned int)writeId, (int)rc);
        return (int)rc;
    }

    LOG_INF("Konfiguration gespeichert (Generation %u, Slot %u).",
            (unsigned int)newRecord.generation, (unsigned int)writeId);

    return 0;
}

/* CFG-REQ-04: Standardwerte fuer alle Parameter setzen */
void Config_GetDefaults(Config_Data_t *data)
{
    if (data == NULL) {
        return;
    }

    (void)memset(data, 0, sizeof(Config_Data_t));
    data->mqttPort = (uint16_t)CFG_MQTT_PORT_DEFAULT;
    (void)strncpy(data->pin, CFG_PIN_DEFAULT, CFG_PIN_BUF_SIZE - 1U);
    data->pin[CFG_PIN_BUF_SIZE - 1U] = '\0';
}

/* CFG-REQ-03: Alle NVS-Slots ungueltig schreiben.
 * CFG-REQ-06: Ungueltige Eintraege haben valid=false UND absichtlich falsche CRC. */
int Config_InvalidateAll(void)
{
    Config_Stored_t stored;
    uint16_t        id;
    ssize_t         rc;
    int             lastError = 0;
    size_t          i;

    if (!g_Initialized) {
        return -ENODEV;
    }

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        id = (uint16_t)(NVS_ID_BASE + i);

        (void)memset(&stored, 0, sizeof(Config_Stored_t));
        stored.generation = (uint8_t)i;
        stored.valid      = false;
        /* Korrekten Wert berechnen und negieren — garantiert falsche CRC */
        stored.crc = Config_ComputeStoredCrc(&stored) ^ 0xFFFFFFFFU;

        rc = nvs_write(&g_NvsFs, id, &stored, sizeof(Config_Stored_t));

        if (rc < 0) {
            /* CFG-NFR-03: Fehler loggen, restliche Slots weiter invalidieren */
            LOG_ERR("NVS-Invalidierungsfehler Slot %u: %d", (unsigned int)id, (int)rc);
            lastError = (int)rc;
        }
    }

    return lastError;
}

/* CFG-REQ-01, CFG-REQ-02: NVS-Dateisystem initialisieren und einhaengen */
int Config_Init(void)
{
    psa_status_t psaSt;
    int          rc;

    if (g_Initialized) {
        return 0;
    }

    /* CFG-REQ-05: PSA-Krypto-Subsystem initialisieren (einmalig) */
    psaSt = psa_crypto_init();
    if (psaSt != PSA_SUCCESS) {
        LOG_ERR("PSA-Initialisierungsfehler: %d", (int)psaSt);
        return -EIO;
    }

    g_NvsFs.flash_device = FIXED_PARTITION_DEVICE(storage_partition);
    g_NvsFs.offset       = (off_t)FIXED_PARTITION_OFFSET(storage_partition);
    g_NvsFs.sector_size  = (uint32_t)NVS_SECTOR_SIZE;
    g_NvsFs.sector_count = (uint32_t)NVS_SECTOR_COUNT;

    rc = nvs_mount(&g_NvsFs);

    if (rc < 0) {
        /* CFG-NFR-03: Initialisierungsfehler loggen; System bleibt funktionsfaehig */
        LOG_ERR("NVS-Initialisierungsfehler: %d", rc);
        return rc;
    }

    g_Initialized = true;
    LOG_INF("NVS initialisiert (%u Sektoren x %u Byte).",
            (unsigned int)NVS_SECTOR_COUNT, (unsigned int)NVS_SECTOR_SIZE);

    return 0;
}
