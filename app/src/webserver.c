/* WEB-REQ-01..05, TMP-REQ-02 */
#include "webserver.h"
#include "temp_data.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <string.h>

LOG_MODULE_REGISTER(webserver, LOG_LEVEL_ERR);

static uint16_t g_HttpPort = 80U;

/* WEB-NFR-02: Statischer Antwortpuffer */
#define HTML_BUF_SIZE (2048U)
static uint8_t g_HtmlBuf[HTML_BUF_SIZE];

/* ------------------------------------------------------------------ */
/* HTML-Fragmente                                                      */
/* ------------------------------------------------------------------ */

static const char k_HtmlA[] =
    "<!DOCTYPE html><html>"
    "<head><meta charset=\"utf-8\"><title>";

/* CSS + oeffnendes <h1> (Hostname folgt danach) */
static const char k_HtmlB[] =
    "</title>"
    "<style>"
    "body{font-family:sans-serif;margin:0;padding:16px;}"
    ".hdr{width:100%;box-sizing:border-box;text-align:center;"
    "background:#f4f4f4;padding:16px;border:2px solid black;}"
    ".row{display:flex;justify-content:space-around;margin:4px 0 16px;}"
    ".cell{border:1px solid #ccc;border-radius:4px;padding:12px 20px;"
    "text-align:center;flex:1;margin:0 4px;}"
    ".lbl{font-size:12px;color:#666;}"
    ".val{font-size:24px;font-weight:bold;margin-top:4px;}"
    "h2{margin:12px 0 4px;}"
    "</style>"
    "</head><body>"
    "<div class=\"hdr\"><h1>";

static const char k_HtmlAfterH1[]  = "</h1></div>";
static const char k_HtmlSec1[]     = "<h2>Brennertemperatur</h2><div class=\"row\">";
static const char k_HtmlSec2[]     = "</div><h2>Kerntemperatur</h2><div class=\"row\">";
static const char k_HtmlSec3[]     = "</div><h2>Zieltemperatur</h2><div class=\"row\">";
static const char k_HtmlEnd[]      = "</div></body></html>";

static const char k_CellPre[]  = "<div class=\"cell\"><div class=\"lbl\">";
static const char k_CellMid[]  = "</div><div class=\"val\">";
static const char k_CellPost[] = "</div></div>";
static const char k_CellNA[]   = "--";
static const char k_DegUnit[]  = "&nbsp;&deg;C";

/* ------------------------------------------------------------------ */
/* HTML-Puffer-Hilfsfunktionen (kein snprintf; MISRA 21.6)            */
/* ------------------------------------------------------------------ */

typedef struct {
    size_t pos;
    bool   overflow;
} HtmlCtx_t;

static void Html_Append(HtmlCtx_t *ctx, const char *str, size_t len)
{
    if (ctx->overflow) {
        return;
    }
    if ((ctx->pos + len) >= HTML_BUF_SIZE) {
        ctx->overflow = true;
        return;
    }
    (void)memcpy(&g_HtmlBuf[ctx->pos], str, len);
    ctx->pos += len;
}

/* Vorzeichenbehaftete 16-Bit-Ganzzahl als Dezimalstring in ctx schreiben */
static void Html_AppendInt16(HtmlCtx_t *ctx, int16_t val)
{
    char     digits[5]; /* max. "9999" fuer gueltige Grilltemperaturen */
    size_t   dLen = 0U;
    uint16_t uval;
    size_t   i;

    if (val < (int16_t)0) {
        Html_Append(ctx, "-", 1U);
        uval = (uint16_t)(-(int32_t)val);
    } else {
        uval = (uint16_t)val;
    }

    /* Ziffern in umgekehrter Reihenfolge extrahieren */
    do {
        digits[dLen] = (char)('0' + (char)(uval % 10U));
        dLen++;
        uval = (uint16_t)(uval / 10U);
    } while ((uval > 0U) && (dLen < sizeof(digits)));

    /* In korrekter Reihenfolge ausgeben */
    i = dLen;
    while (i > 0U) {
        i--;
        Html_Append(ctx, &digits[i], 1U);
    }
}

/* ------------------------------------------------------------------ */
/* Temperaturzelle                                                     */
/* ------------------------------------------------------------------ */

static void Html_AppendCell(HtmlCtx_t *ctx, uint8_t num, const Temp_Entry_t *entry)
{
    char numChar = (char)('0' + (char)num);

    Html_Append(ctx, k_CellPre,  sizeof(k_CellPre)  - 1U);
    Html_Append(ctx, &numChar, 1U);
    Html_Append(ctx, k_CellMid,  sizeof(k_CellMid)  - 1U);

    if (entry->valid) {
        Html_AppendInt16(ctx, entry->value);
        Html_Append(ctx, k_DegUnit, sizeof(k_DegUnit) - 1U);
    } else {
        Html_Append(ctx, k_CellNA, sizeof(k_CellNA) - 1U);
    }

    Html_Append(ctx, k_CellPost, sizeof(k_CellPost) - 1U);
}

/* ------------------------------------------------------------------ */
/* Vollstaendige Seite aufbauen                          WEB-REQ-02ff  */
/* ------------------------------------------------------------------ */

static int Webserver_BuildHtml(const char *hostname, const Temp_Data_t *data)
{
    HtmlCtx_t ctx = { .pos = 0U, .overflow = false };
    size_t    hLen = strlen(hostname);
    uint8_t   i;

    Html_Append(&ctx, k_HtmlA,      sizeof(k_HtmlA)      - 1U);
    Html_Append(&ctx, hostname,     hLen);
    Html_Append(&ctx, k_HtmlB,      sizeof(k_HtmlB)      - 1U);
    Html_Append(&ctx, hostname,     hLen);
    Html_Append(&ctx, k_HtmlAfterH1,sizeof(k_HtmlAfterH1)- 1U);

    /* WEB-REQ-03: Brennertemperatur */
    Html_Append(&ctx, k_HtmlSec1, sizeof(k_HtmlSec1) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, (uint8_t)(i + 1U), &data->burner[i]);
    }

    /* WEB-REQ-04: Kerntemperatur */
    Html_Append(&ctx, k_HtmlSec2, sizeof(k_HtmlSec2) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, (uint8_t)(i + 1U), &data->core[i]);
    }

    /* WEB-REQ-05: Zieltemperatur */
    Html_Append(&ctx, k_HtmlSec3, sizeof(k_HtmlSec3) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, (uint8_t)(i + 1U), &data->target[i]);
    }

    Html_Append(&ctx, k_HtmlEnd, sizeof(k_HtmlEnd) - 1U);

    if (ctx.overflow) {
        return -ENOMEM;
    }

    return (int)ctx.pos;
}

/* ------------------------------------------------------------------ */
/* HTTP-Callback                                                       */
/* ------------------------------------------------------------------ */

static int Webserver_IndexCb(struct http_client_ctx *client,
                             enum http_transaction_status status,
                             const struct http_request_ctx *request_ctx,
                             struct http_response_ctx *response_ctx,
                             void *user_data)
{
    static const struct http_header k_Headers[] = {
        { .name = "Content-Type", .value = "text/html; charset=utf-8" },
    };
    const char  *hostname;
    int          htmlLen;
    Temp_Data_t  snapshot;

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
        (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
        return 0;
    }

    /* TMP-REQ-02: Temperaturdaten unter Mutex-Schutz kopieren */
    (void)k_mutex_lock(&g_TempMutex, K_FOREVER);
    snapshot = g_TempData;
    (void)k_mutex_unlock(&g_TempMutex);

    hostname = net_hostname_get();
    htmlLen  = Webserver_BuildHtml(hostname, &snapshot);

    if (htmlLen < 0) {
        LOG_ERR("HTML-Puffer zu klein: %d", htmlLen);
        return htmlLen;
    }

    response_ctx->status       = HTTP_200_OK;
    response_ctx->headers      = k_Headers;
    response_ctx->header_count = ARRAY_SIZE(k_Headers);
    response_ctx->body         = g_HtmlBuf;
    response_ctx->body_len     = (size_t)htmlLen;
    response_ctx->final_chunk  = true;

    return 0;
}

static struct http_resource_detail_dynamic g_IndexDetail = {
    .common = {
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .type                              = HTTP_RESOURCE_TYPE_DYNAMIC,
    },
    .cb        = Webserver_IndexCb,
    .user_data = NULL,
};

HTTP_SERVICE_DEFINE(grill_buddy_svc, NULL, &g_HttpPort, 1, 1, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(index_resource, grill_buddy_svc, "/", &g_IndexDetail);

/* ------------------------------------------------------------------ */
/* Oeffentliche API                                       WEB-REQ-01   */
/* ------------------------------------------------------------------ */

static bool g_Running = false;

void Webserver_Start(void)
{
    int rc;

    if (g_Running) {
        return;
    }

    rc = http_server_start();

    if (rc < 0) {
        LOG_ERR("HTTP-Server-Start fehlgeschlagen: %d", rc);
        return;
    }

    g_Running = true;
    LOG_INF("HTTP-Server gestartet (Port %u).", (unsigned int)g_HttpPort);
}

void Webserver_Stop(void)
{
    int rc;

    if (!g_Running) {
        return;
    }

    rc = http_server_stop();

    if (rc < 0) {
        LOG_ERR("HTTP-Server-Stop fehlgeschlagen: %d", rc);
        return;
    }

    g_Running = false;
    LOG_INF("HTTP-Server gestoppt.");
}
