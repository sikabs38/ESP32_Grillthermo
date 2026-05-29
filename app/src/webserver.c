/* WEB-REQ-01..09, TMP-REQ-02, TMP-REQ-03 */
#include "webserver.h"
#include "temp_data.h"

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/net/http/server.h>
#include <zephyr/net/http/service.h>
#include <zephyr/net/hostname.h>
#include <errno.h>
#include <string.h>

LOG_MODULE_REGISTER(webserver, LOG_LEVEL_ERR);

static uint16_t g_HttpPort = 80U;

/* WEB-NFR-02: Statischer Antwortpuffer fuer die HTML-Seite */
#define HTML_BUF_SIZE (3072U)
static uint8_t g_HtmlBuf[HTML_BUF_SIZE];

/* WEB-REQ-06: Statischer Antwortpuffer fuer einen SSE-Event (data: {json}\n\n) */
#define SSE_BUF_SIZE (512U)
static uint8_t g_SseBuf[SSE_BUF_SIZE];

/* WEB-REQ-06: Zustand des SSE-Streams. Da der HTTP-Server-Thread einzeln laeuft
 * und beim Streamen blockiert, ist effektiv nur ein /events-Client gleichzeitig
 * aktiv (siehe WEB-REQ-08); ein globaler Zustand genuegt daher. */
static bool     g_SseStreaming = false;
static uint32_t g_SseLastGen   = 0U;

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
static const char k_HtmlRowEnd[]   = "</div>";

/* WEB-REQ-07: EventSource-Client. Aktualisiert die zwoelf Wertzellen (b1-b4,
 * c1-c4, t1-t4) bei jedem SSE-Event ohne Seiten-Reload; bei valid=false "--".
 * WEB-REQ-09: col() faerbt Kerntemperatur-Zellen abhaengig von Zieltemperatur. */
static const char k_HtmlScript[] =
    "<script>"
    "var es=new EventSource('/events');"
    "function u(g,a){var i;for(i=0;i<a.length;i++){"
    "var el=document.getElementById(g+(i+1));"
    "if(el){el.innerHTML=a[i].ok?a[i].v+'&nbsp;&deg;C':'--';}}}"
    "function col(i,cv,tv){"
    "var el=document.getElementById('c'+(i+1));if(!el)return;"
    "if(!tv.ok||!cv.ok){el.style.background='';el.style.color='';return;}"
    "var d=cv.v-tv.v,"
    "bg=(d<-10)?'#ffffff':(d<-5)?'#ffff00':(d<=5)?'#00c800':'#ff6464';"
    "el.style.background=bg;el.style.color='#000000';}"
    "es.onmessage=function(e){var d=JSON.parse(e.data);"
    "u('b',d.burner);u('c',d.core);u('t',d.target);"
    "for(var j=0;j<4;j++){col(j,d.core[j],d.target[j]);}};"
    "</script></body></html>";

static const char k_CellPre[]   = "<div class=\"cell\"><div class=\"lbl\">";
static const char k_CellMidA[]  = "</div><div class=\"val\" id=\"";
static const char k_CellMidB[]  = "\">";
static const char k_CellPost[]  = "</div></div>";
static const char k_CellNA[]    = "--";
static const char k_DegUnit[]   = "&nbsp;&deg;C";

/* WEB-REQ-06: SSE-Rahmen und JSON-Fragmente (kompaktes Format) */
static const char k_SseDataPre[]  = "data: {\"burner\":[";
static const char k_SseCore[]     = "],\"core\":[";
static const char k_SseTarget[]   = "],\"target\":[";
static const char k_SseDataPost[] = "]}\n\n";
static const char k_SseKeepAlive[] = ": ka\n\n";
static const char k_JsonEntryPre[] = "{\"v\":";
static const char k_JsonEntryOk1[] = ",\"ok\":1}";
static const char k_JsonEntryOk0[] = ",\"ok\":0}";

/* ------------------------------------------------------------------ */
/* HTML-Puffer-Hilfsfunktionen (kein snprintf; MISRA 21.6)            */
/* ------------------------------------------------------------------ */

typedef struct {
    uint8_t *buf;
    size_t   size;
    size_t   pos;
    bool     overflow;
} BufCtx_t;

static void Buf_Append(BufCtx_t *ctx, const char *str, size_t len)
{
    if (ctx->overflow) {
        return;
    }
    if ((ctx->pos + len) >= ctx->size) {
        ctx->overflow = true;
        return;
    }
    (void)memcpy(&ctx->buf[ctx->pos], str, len);
    ctx->pos += len;
}

/* Vorzeichenbehaftete 16-Bit-Ganzzahl als Dezimalstring in ctx schreiben */
static void Buf_AppendInt16(BufCtx_t *ctx, int16_t val)
{
    char     digits[5]; /* max. "9999" fuer gueltige Grilltemperaturen */
    size_t   dLen = 0U;
    uint16_t uval;
    size_t   i;

    if (val < (int16_t)0) {
        Buf_Append(ctx, "-", 1U);
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
        Buf_Append(ctx, &digits[i], 1U);
    }
}

/* ------------------------------------------------------------------ */
/* Temperaturzelle                                                     */
/* ------------------------------------------------------------------ */

/* WEB-REQ-07: group ('b','c','t') + num (1..4) ergeben die stabile Zell-ID */
static void Html_AppendCell(BufCtx_t *ctx, char group, uint8_t num,
                            const Temp_Entry_t *entry)
{
    char numChar = (char)('0' + (char)num);

    Buf_Append(ctx, k_CellPre,  sizeof(k_CellPre)  - 1U);
    Buf_Append(ctx, &numChar, 1U);
    Buf_Append(ctx, k_CellMidA, sizeof(k_CellMidA) - 1U);
    Buf_Append(ctx, &group, 1U);
    Buf_Append(ctx, &numChar, 1U);
    Buf_Append(ctx, k_CellMidB, sizeof(k_CellMidB) - 1U);

    if (entry->valid) {
        Buf_AppendInt16(ctx, entry->value);
        Buf_Append(ctx, k_DegUnit, sizeof(k_DegUnit) - 1U);
    } else {
        Buf_Append(ctx, k_CellNA, sizeof(k_CellNA) - 1U);
    }

    Buf_Append(ctx, k_CellPost, sizeof(k_CellPost) - 1U);
}

/* ------------------------------------------------------------------ */
/* Vollstaendige Seite aufbauen                          WEB-REQ-02ff  */
/* ------------------------------------------------------------------ */

static int Webserver_BuildHtml(const char *hostname, const Temp_Data_t *data)
{
    BufCtx_t ctx = { .buf = g_HtmlBuf, .size = HTML_BUF_SIZE,
                     .pos = 0U, .overflow = false };
    size_t   hLen = strlen(hostname);
    uint8_t  i;

    Buf_Append(&ctx, k_HtmlA,      sizeof(k_HtmlA)      - 1U);
    Buf_Append(&ctx, hostname,     hLen);
    Buf_Append(&ctx, k_HtmlB,      sizeof(k_HtmlB)      - 1U);
    Buf_Append(&ctx, hostname,     hLen);
    Buf_Append(&ctx, k_HtmlAfterH1,sizeof(k_HtmlAfterH1)- 1U);

    /* WEB-REQ-03: Brennertemperatur (Zell-IDs b1..b4) */
    Buf_Append(&ctx, k_HtmlSec1, sizeof(k_HtmlSec1) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, 'b', (uint8_t)(i + 1U), &data->burner[i]);
    }

    /* WEB-REQ-04: Kerntemperatur (Zell-IDs c1..c4) */
    Buf_Append(&ctx, k_HtmlSec2, sizeof(k_HtmlSec2) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, 'c', (uint8_t)(i + 1U), &data->core[i]);
    }

    /* WEB-REQ-05: Zieltemperatur (Zell-IDs t1..t4) */
    Buf_Append(&ctx, k_HtmlSec3, sizeof(k_HtmlSec3) - 1U);
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        Html_AppendCell(&ctx, 't', (uint8_t)(i + 1U), &data->target[i]);
    }

    Buf_Append(&ctx, k_HtmlRowEnd, sizeof(k_HtmlRowEnd) - 1U);
    /* WEB-REQ-07: clientseitiges EventSource-Script */
    Buf_Append(&ctx, k_HtmlScript, sizeof(k_HtmlScript) - 1U);

    if (ctx.overflow) {
        return -ENOMEM;
    }

    return (int)ctx.pos;
}

/* ------------------------------------------------------------------ */
/* SSE-Datenframe (JSON)                                  WEB-REQ-06   */
/* ------------------------------------------------------------------ */

/* Eine Gruppe als JSON-Array von {"v":<wert>,"ok":<0|1>}-Objekten */
static void Sse_AppendGroup(BufCtx_t *ctx, const Temp_Entry_t *arr)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        if (i > 0U) {
            Buf_Append(ctx, ",", 1U);
        }
        Buf_Append(ctx, k_JsonEntryPre, sizeof(k_JsonEntryPre) - 1U);
        Buf_AppendInt16(ctx, arr[i].value);
        if (arr[i].valid) {
            Buf_Append(ctx, k_JsonEntryOk1, sizeof(k_JsonEntryOk1) - 1U);
        } else {
            Buf_Append(ctx, k_JsonEntryOk0, sizeof(k_JsonEntryOk0) - 1U);
        }
    }
}

/* Kompletten "data: {json}\n\n"-Frame in g_SseBuf aufbauen */
static int Webserver_BuildSseData(const Temp_Data_t *data)
{
    BufCtx_t ctx = { .buf = g_SseBuf, .size = SSE_BUF_SIZE,
                     .pos = 0U, .overflow = false };

    Buf_Append(&ctx, k_SseDataPre, sizeof(k_SseDataPre) - 1U);
    Sse_AppendGroup(&ctx, data->burner);
    Buf_Append(&ctx, k_SseCore, sizeof(k_SseCore) - 1U);
    Sse_AppendGroup(&ctx, data->core);
    Buf_Append(&ctx, k_SseTarget, sizeof(k_SseTarget) - 1U);
    Sse_AppendGroup(&ctx, data->target);
    Buf_Append(&ctx, k_SseDataPost, sizeof(k_SseDataPost) - 1U);

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

/* ------------------------------------------------------------------ */
/* SSE-Callback                                           WEB-REQ-06   */
/* ------------------------------------------------------------------ */

/* Hinweis: Der HTTP-Server-Thread laeuft einzeln. Dieser Handler blockiert
 * waehrend des Streamens auf g_TempCondvar und belegt den Thread dauerhaft;
 * daher ist effektiv nur ein /events-Client gleichzeitig moeglich. */
static int Webserver_EventsCb(struct http_client_ctx *client,
                              enum http_transaction_status status,
                              const struct http_request_ctx *request_ctx,
                              struct http_response_ctx *response_ctx,
                              void *user_data)
{
    static const struct http_header k_SseHeaders[] = {
        { .name = "Content-Type",  .value = "text/event-stream" },
        { .name = "Cache-Control", .value = "no-cache" },
    };
    Temp_Data_t snapshot;
    int         dataLen;
    bool        changed;
    int         rc;

    ARG_UNUSED(client);
    ARG_UNUSED(request_ctx);
    ARG_UNUSED(user_data);

    /* Verbindungsende: Stream-Zustand fuer den naechsten Client zuruecksetzen */
    if ((status == HTTP_SERVER_TRANSACTION_ABORTED) ||
        (status == HTTP_SERVER_TRANSACTION_COMPLETE)) {
        g_SseStreaming = false;
        return 0;
    }

    if (!g_SseStreaming) {
        /* Erster Aufruf: Header senden und aktuellen Stand initial uebertragen */
        (void)k_mutex_lock(&g_TempMutex, K_FOREVER);
        snapshot     = g_TempData;
        g_SseLastGen = g_TempGen;
        (void)k_mutex_unlock(&g_TempMutex);

        dataLen = Webserver_BuildSseData(&snapshot);
        if (dataLen < 0) {
            LOG_ERR("SSE-Puffer zu klein: %d", dataLen);
            return dataLen;
        }

        response_ctx->status       = HTTP_200_OK;
        response_ctx->headers      = k_SseHeaders;
        response_ctx->header_count = ARRAY_SIZE(k_SseHeaders);
        response_ctx->body         = g_SseBuf;
        response_ctx->body_len     = (size_t)dataLen;
        response_ctx->final_chunk  = false;

        g_SseStreaming = true;
        return 0;
    }

    /* Folgeaufrufe: auf Wertaenderung warten (TMP-REQ-03), sonst Keepalive.
     * k_condvar_wait gibt g_TempMutex waehrend des Wartens frei. */
    (void)k_mutex_lock(&g_TempMutex, K_FOREVER);
    while (g_TempGen == g_SseLastGen) {
        rc = k_condvar_wait(&g_TempCondvar, &g_TempMutex, K_SECONDS(15));
        if (rc == -EAGAIN) {
            break; /* Timeout → Keepalive */
        }
    }
    changed = (g_TempGen != g_SseLastGen);
    if (changed) {
        snapshot     = g_TempData;
        g_SseLastGen = g_TempGen;
    }
    (void)k_mutex_unlock(&g_TempMutex);

    if (changed) {
        dataLen = Webserver_BuildSseData(&snapshot);
        if (dataLen < 0) {
            LOG_ERR("SSE-Puffer zu klein: %d", dataLen);
            return dataLen;
        }
        response_ctx->body     = g_SseBuf;
        response_ctx->body_len = (size_t)dataLen;
    } else {
        response_ctx->body     = (const uint8_t *)k_SseKeepAlive;
        response_ctx->body_len = sizeof(k_SseKeepAlive) - 1U;
    }
    response_ctx->final_chunk = false;

    return 0;
}

static struct http_resource_detail_dynamic g_EventsDetail = {
    .common = {
        .bitmask_of_supported_http_methods = BIT(HTTP_GET),
        .type                              = HTTP_RESOURCE_TYPE_DYNAMIC,
    },
    .cb        = Webserver_EventsCb,
    .user_data = NULL,
};

/* WEB-REQ-08: maximal 3 gleichzeitige Clients, Backlog 3 fuer wartende Verbindungen */
HTTP_SERVICE_DEFINE(grill_buddy_svc, NULL, &g_HttpPort, 3, 3, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(index_resource, grill_buddy_svc, "/", &g_IndexDetail);
/* WEB-REQ-06: SSE-Endpoint */
HTTP_RESOURCE_DEFINE(events_resource, grill_buddy_svc, "/events", &g_EventsDetail);

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
