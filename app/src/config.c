/* CFG-REQ-01, CFG-REQ-02, CFG-REQ-03, CFG-REQ-06, CFG-NFR-01, CFG-NFR-02, CFG-NFR-03 */
#include "config.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/fs/nvs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>

LOG_MODULE_REGISTER(config_store, LOG_LEVEL_ERR);

/* CFG-NFR-01: Ausschliesslich Zephyr NVS API, kein direkter Flash-Zugriff */
/* CFG-NFR-02: Alle Puffer statisch alloziert, keine dynamische Speicherverwaltung */

/* ESP32-S3: Flash-Loeschseite 4 KB; storage_partition 192 KB = 48 Sektoren */
#define NVS_SECTOR_SIZE  (0x1000U)
#define NVS_SECTOR_COUNT (48U)
#define NVS_SLOT_COUNT   (3U)   /* CFG-REQ-06: drei Generationen */
#define NVS_ID_BASE      (1U)   /* NVS-ID 0 ist reserviert */

/* CFG-REQ-06: Datensatz pro Slot — CRC muss das letzte Feld sein */
typedef struct {
    uint8_t       generation;
    bool          valid;
    Config_Data_t data;
    uint32_t      crc; /* deckt alle vorigen Felder ab */
} Config_Record_t;

static struct nvs_fs g_NvsFs;
static bool          g_Initialized = false;

/* CFG-REQ-06: CRC32 ueber alle Felder des Records ausser dem CRC-Feld selbst */
static uint32_t Config_ComputeCrc(const Config_Record_t *record)
{
    return crc32_ieee((const uint8_t *)record, offsetof(Config_Record_t, crc));
}

/* CFG-REQ-06: Prueft Gueltigkeitsflag und CRC-Korrektheit */
static bool Config_IsRecordValid(const Config_Record_t *record)
{
    uint32_t expected;

    if (!record->valid) {
        return false;
    }

    expected = Config_ComputeCrc(record);

    return (record->crc == expected);
}

/* CFG-REQ-06: Neueste gueltige Generation unter den drei Slots suchen.
 * Rueckgabe: Slot-Index [0..2] oder -1 wenn kein gueltiger Eintrag vorhanden. */
static int Config_FindNewest(const Config_Record_t records[NVS_SLOT_COUNT])
{
    int    newestIdx = -1;
    size_t i;

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        if (!Config_IsRecordValid(&records[i])) {
            continue;
        }

        if (newestIdx < 0) {
            newestIdx = (int)i;
        } else {
            /* Modularer uint8_t-Vergleich sichert korrekte Behandlung des Ueberlaufs */
            uint8_t diff = records[i].generation - records[newestIdx].generation;

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
    /* Statisch alloziert: 732 Byte; verhindert Stack-Ueberlauf auf dem Shell-Thread */
    static Config_Record_t records[NVS_SLOT_COUNT];
    uint16_t               id;
    int                    newestIdx;
    ssize_t                rc;
    size_t                 i;

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
        rc = nvs_read(&g_NvsFs, id, &records[i], sizeof(Config_Record_t));

        if ((rc != (ssize_t)sizeof(Config_Record_t)) && (rc != -ENOENT)) {
            /* CFG-NFR-03: Lesefehler loggen; Eintrag bleibt null-initialisiert (ungueltig) */
            LOG_ERR("NVS-Lesefehler Slot %u: %d", (unsigned int)id, (int)rc);
        }
    }

    newestIdx = Config_FindNewest(records);

    if (newestIdx < 0) {
        /* CFG-NFR-03: Kein gueltiger Eintrag — System startet mit Standardwerten */
        LOG_INF("Keine gueltige Konfiguration; Standardwerte aktiv.");
        Config_GetDefaults(data);
        return -ENOENT;
    }

    LOG_INF("Konfiguration geladen (Generation %u).",
            (unsigned int)records[newestIdx].generation);
    *data = records[newestIdx].data;

    return 0;
}

/* CFG-REQ-01: Konfiguration in NVS speichern.
 * Schreibvorgang wird vor der Rueckkehr abgeschlossen (kein Hintergrundpuffer). */
int Config_Save(const Config_Data_t *data)
{
    /* Statisch alloziert: 976 Byte gesamt; verhindert Stack-Ueberlauf auf dem Shell-Thread */
    static Config_Record_t records[NVS_SLOT_COUNT];
    static Config_Record_t newRecord;
    uint16_t               writeId;
    int                    newestIdx;
    ssize_t                rc;
    size_t                 i;

    if (data == NULL) {
        return -EINVAL;
    }

    if (!g_Initialized) {
        return -ENODEV;
    }

    (void)memset(records, 0, sizeof(records));

    /* Vorhandene Eintraege lesen um aktuelle Generation zu ermitteln */
    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        uint16_t id = (uint16_t)(NVS_ID_BASE + i);

        (void)nvs_read(&g_NvsFs, id, &records[i], sizeof(Config_Record_t));
    }

    newestIdx = Config_FindNewest(records);

    /* Naechste Generation berechnen; uint8_t-Ueberlauf (255 -> 0) ist gewollt */
    if (newestIdx < 0) {
        newRecord.generation = 0U;
    } else {
        newRecord.generation = (uint8_t)(records[newestIdx].generation + 1U);
    }

    newRecord.valid = true;
    newRecord.data  = *data;
    newRecord.crc   = Config_ComputeCrc(&newRecord);

    /* CFG-REQ-06: Slot-Zuweisung per Modulo ueber Slot-Anzahl */
    writeId = (uint16_t)(NVS_ID_BASE + (newRecord.generation % (uint8_t)NVS_SLOT_COUNT));

    rc = nvs_write(&g_NvsFs, writeId, &newRecord, sizeof(Config_Record_t));

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
    (void)strncpy(data->pin, CFG_PIN_DEFAULT, CFG_PIN_BUF_SIZE - 1U);
    data->pin[CFG_PIN_BUF_SIZE - 1U] = '\0';
    data->brightnessStep = 6U; /* STA-REQ-07/09: Stufe 6 = 25 % Standard */
}

/* CFG-REQ-03: Alle NVS-Slots ungueltig schreiben.
 * CFG-REQ-06: Ungueltige Eintraege haben valid=false UND absichtlich falsche CRC. */
int Config_InvalidateAll(void)
{
    Config_Record_t record;
    uint16_t        id;
    ssize_t         rc;
    int             lastError = 0;
    size_t          i;

    if (!g_Initialized) {
        return -ENODEV;
    }

    for (i = 0U; i < NVS_SLOT_COUNT; i++) {
        id = (uint16_t)(NVS_ID_BASE + i);

        (void)memset(&record, 0, sizeof(Config_Record_t));
        record.generation = (uint8_t)i;
        record.valid      = false;
        /* Korrekten Wert berechnen und dann negieren — garantiert falsche CRC */
        record.crc = Config_ComputeCrc(&record) ^ 0xFFFFFFFFU;

        rc = nvs_write(&g_NvsFs, id, &record, sizeof(Config_Record_t));

        if (rc < 0) {
            /* CFG-NFR-03: Fehler loggen, aber restliche Slots weiter invalidieren */
            LOG_ERR("NVS-Invalidierungsfehler Slot %u: %d", (unsigned int)id, (int)rc);
            lastError = (int)rc;
        }
    }

    return lastError;
}

/* CFG-REQ-01, CFG-REQ-02: NVS-Dateisystem initialisieren und einhaengen */
int Config_Init(void)
{
    int rc;

    if (g_Initialized) {
        return 0;
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
