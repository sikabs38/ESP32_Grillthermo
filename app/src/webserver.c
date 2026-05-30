/* WEB-REQ-01..09, TMP-REQ-02, TMP-REQ-03, DSP-REQ-01..06 */
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

/* WEB-NFR-02: Statischer Antwortpuffer fuer die HTML-Seite.
 * Schrittweise erhoeht: 3072 → 6144 (DSP-REQ-01..05: Bloecke + JS)
 *                         → 8192 (DSP-REQ-06: Gas-Anzeige + erweitertes JS,
 *                                  inkl. Reserve fuer kuenftige Anzeigen). */
#define HTML_BUF_SIZE (8192U)
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

/* DSP-REQ-01..04: Layout fuer vier Zonenbloecke, Profilauswahl und Farbbalken. */
static const char k_HtmlCss[] =
    "</title>"
    "<style>"
    "body{font-family:sans-serif;margin:0;padding:12px;background:#fafafa;color:#222;}"
    ".hdr{text-align:center;background:#f4f4f4;padding:12px;"
    "border:2px solid #000;border-radius:6px;}"
    /* DSP-REQ-07: pro Zone, im Block — kompakter als die alte globale Leiste */
    ".pr{display:flex;gap:4px;justify-content:center;flex-wrap:wrap;margin:8px 0 0;}"
    ".ch{padding:6px 14px;border:1px solid #888;border-radius:14px;"
    "background:#fff;color:#222;cursor:pointer;font:inherit;}"
    ".ch.a{background:#0a7;border-color:#0a7;color:#fff;}"
    /* DSP-REQ-01: zweispaltiges Raster (2 x 2) fuer Landscape-Geraete; */
    /* Fallback auf eine Spalte unterhalb 600 px Breite. */
    ".gr{display:grid;grid-template-columns:repeat(2,1fr);gap:10px;}"
    "@media(max-width:600px){.gr{grid-template-columns:1fr;}}"
    ".bl{border:1px solid #ccc;border-radius:6px;padding:10px;"
    "background:#fff;}"
    ".zh{font-weight:bold;margin-bottom:8px;text-align:center;color:#555;}"
    ".ds{display:flex;gap:14px;}"
    ".dp{flex:1;text-align:center;min-width:0;}"
    ".dt{font-size:11px;color:#888;text-transform:uppercase;letter-spacing:.5px;}"
    ".vl{font-size:28px;font-weight:bold;margin:2px 0;}"
    ".br{position:relative;height:16px;border-radius:8px;background:#ddd;"
    "margin:8px 0 4px;overflow:visible;}"
    ".in{position:absolute;top:-3px;width:3px;height:22px;background:#000;"
    "left:0%;transform:translateX(-50%);transition:left .25s;}"
    ".zn{margin-top:4px;font-size:13px;color:#222;min-height:1em;}"
    ".lg{font-size:11px;color:#666;margin-top:6px;}"
    ".dot{display:inline-block;width:8px;height:8px;border-radius:50%;"
    "margin:0 3px 0 8px;vertical-align:middle;}"
    /* DSP-REQ-06: Gasflaschen-Anzeige zentriert unterhalb der Bloecke */
    ".gs{margin:14px auto 0;max-width:480px;border:1px solid #ccc;"
    "border-radius:6px;padding:10px;background:#fff;text-align:center;}"
    "</style></head><body>"
    "<div class=\"hdr\"><h1>";

static const char k_HtmlAfterH1[] = "</h1></div>";

/* DSP-REQ-01: Grid-Container und Block-Fragmente (ein Block je Grillzone) */
static const char k_GridOpen[]   = "<div class=\"gr\">";
static const char k_GridClose[]  = "</div>";
static const char k_BlockOpen[]  = "<div class=\"bl\"><div class=\"zh\">Zone ";
static const char k_BlockMid[]   = "</div><div class=\"ds\">";
static const char k_DsClose[]    = "</div>";              /* schliesst .ds */
static const char k_BlockClose[] = "</div>";              /* schliesst .bl */

/* DSP-REQ-07: Profil-Auswahlleiste pro Zone. Zwischen A und B wird die
 * 0-basierte Zonenziffer ('0'..'3') eingefuegt; das JS liest data-z. */
static const char k_ZoneProfA[] = "<div class=\"pr\" data-z=\"";
static const char k_ZoneProfB[] =
    "\"><button class=\"ch\" data-p=\"rind\">Rind</button>"
    "<button class=\"ch\" data-p=\"schwein\">Schwein</button>"
    "<button class=\"ch\" data-p=\"gefluegel\">Gefl&uuml;gel</button>"
    "<button class=\"ch\" data-p=\"fisch\">Fisch</button></div>";

/* DSP-REQ-02 / DSP-REQ-03: Anzeige-Fragmente.
 * id-Praefix b -> Garraum, c -> Kern; angehaengte Ziffer 1..4 = Zonenindex. */
static const char k_DispOpenA[]  = "<div class=\"dp\" id=\"";  /* + "bN" / "cN" */
static const char k_DispOpenB[]  = "\"><div class=\"dt\">";    /* + Label */
static const char k_DispMid[]    = "</div><div class=\"vl\">"; /* + Wert oder "--" */
static const char k_DispBar[]    =
    "</div><div class=\"br\"><div class=\"in\"></div></div>"
    "<div class=\"zn\"></div>";
static const char k_DispLegend[] = "<div class=\"lg\"></div>";
static const char k_DispClose[]  = "</div>";

static const char k_LblGarraum[] = "Garraum";
static const char k_LblKern[]    = "Kern";
static const char k_NotAvail[]   = "--";
static const char k_DegUnit[]    = "&nbsp;&deg;C";

/* DSP-REQ-06: Gasflaschen-Anzeige (Numerik + Farbbalken, 0..100 %) */
static const char k_GasOpen[]  =
    "<div class=\"gs\" id=\"g\"><div class=\"zh\">Gasflasche</div>"
    "<div class=\"vl\">";
static const char k_GasClose[] =
    "</div><div class=\"br\"><div class=\"in\"></div></div></div>";
static const char k_PctUnit[]  = "&nbsp;%";

/* DSP-REQ-02..07 + WEB-REQ-07: clientseitige Logik
 *   GR     = Garraum-Konfiguration (Bereich 0..450 °C, Farbbaender, Zonennamen)
 *   PR     = Grillgut-Profile (Rind, Schwein, Gefluegel, Fisch) — Datenkatalog (DSP-REQ-04)
 *   cp[z]  = je Zone (0..3) aktives Profil (DSP-REQ-07; Default 'rind')
 *   LD     = letzte SSE-Werte, damit Profilwechsel Kernanzeige sofort neu rendert
 *   up()   : aktualisiert Wert, Indikatorposition und Zonen-/Garstufenname.
 *            Klemmt unterhalb p.mn auf 0%, oberhalb p.mx auf 100% mit letzter Stufe.
 *   applyPr(z,n): nur visuell — Skala, Legende, Chip-Aktivzustand, Indikator (Zone z).
 *   setPr(z,n):   applyPr + localStorage-Persistenz (DSP-REQ-07, Schluessel gt_profile_zN). */
static const char k_HtmlScript[] =
    "<script>"
    "var GR={mn:0,mx:450,"
    "bd:[{f:0,t:149,c:'#3080ff'},{f:150,t:299,c:'#ff9030'},"
    "{f:300,t:450,c:'#e03030'}],"
    "zn:[{f:0,t:99,n:'Aufheizen'},{f:100,t:179,n:'Low & Slow'},"
    "{f:180,t:279,n:'Direktes Grillen'},{f:280,t:399,n:'Searing'},"
    "{f:400,t:450,n:'Maximale Hitze'}]};"
    "var PR={"
    "rind:{mn:48,mx:75,s:[{f:48,t:54,c:'#3080ff',n:'Rare'},"
    "{f:54,t:58,c:'#30b040',n:'Medium Rare'},"
    "{f:58,t:63,c:'#ff9030',n:'Medium'},"
    "{f:63,t:75,c:'#e03030',n:'Well Done'}]},"
    "schwein:{mn:60,mx:85,s:[{f:60,t:65,c:'#30b040',n:'Rosa'},"
    "{f:65,t:75,c:'#ff9030',n:'Durch'},"
    "{f:75,t:85,c:'#e03030',n:'Sehr durch'}]},"
    "gefluegel:{mn:74,mx:90,s:[{f:74,t:82,c:'#30b040',n:'Ziel'},"
    "{f:82,t:90,c:'#ff9030',n:'Sicher'}]},"
    "fisch:{mn:45,mx:72,s:[{f:45,t:52,c:'#3080ff',n:'Glasig'},"
    "{f:52,t:62,c:'#30b040',n:'Durch'},"
    "{f:62,t:72,c:'#e03030',n:'Zu durch'}]}};"
    /* DSP-REQ-06: Gasflasche — 0..100 %, drei Farbzonen (Rot/Gelb/Gruen) */
    "var GS={mn:0,mx:100,u:'&nbsp;%',"
    "bd:[{f:0,t:5,c:'#e03030'},{f:5,t:10,c:'#ffd040'},"
    "{f:10,t:100,c:'#30b040'}]};"
    /* DSP-REQ-07: Profilauswahl pro Zone (Index 0..3) */
    "var cp=['rind','rind','rind','rind'];"
    "var LD={b:[null,null,null,null],c:[null,null,null,null]};"
    "function grad(mn,mx,a){var p=[],i,b,p1,p2;"
    "for(i=0;i<a.length;i++){b=a[i];"
    "p1=((b.f-mn)/(mx-mn)*100).toFixed(1);"
    "p2=((b.t-mn)/(mx-mn)*100).toFixed(1);"
    "p.push(b.c+' '+p1+'%',b.c+' '+p2+'%');}"
    "return 'linear-gradient(to right,'+p.join(',')+')';}"
    "function nm(v,a){var i;for(i=0;i<a.length;i++){"
    "if(v>=a[i].f&&v<=a[i].t)return a[i].n;}return '';}"
    /* up(): generisch fuer Temp- und Gas-Anzeigen; .zn und arr (p.s/p.zn) optional */
    "function up(id,e,p){var c=document.getElementById(id);if(!c)return;"
    "var vl=c.querySelector('.vl'),ind=c.querySelector('.in'),"
    "zo=c.querySelector('.zn');"
    "if(!e||!e.ok){vl.innerHTML='--';ind.style.left='0%';"
    "if(zo)zo.textContent='';return;}"
    "vl.innerHTML=e.v+(p.u||'&nbsp;&deg;C');"
    "var arr=p.s||p.zn,pos,sn='';"
    "if(e.v<p.mn){pos=0;}"
    "else if(e.v>p.mx){pos=100;if(arr)sn=arr[arr.length-1].n;}"
    "else{pos=(e.v-p.mn)/(p.mx-p.mn)*100;if(arr)sn=nm(e.v,arr);}"
    "ind.style.left=pos+'%';if(zo)zo.textContent=sn;}"
    /* DSP-REQ-07: nur visuell — kein localStorage-Write (fuer Initial-Render) */
    "function applyPr(z,n){if(!PR[n])return;cp[z]=n;"
    "var row=document.querySelector('.pr[data-z=\"'+z+'\"]');"
    "if(row){var ch=row.querySelectorAll('.ch'),i;"
    "for(i=0;i<ch.length;i++)ch[i].classList.toggle('a',ch[i].dataset.p===n);}"
    "var p=PR[n],j,bar,lg,h;"
    "bar=document.querySelector('#c'+(z+1)+' .br');"
    "bar.style.background=grad(p.mn,p.mx,p.s);"
    "lg=document.querySelector('#c'+(z+1)+' .lg');h='';"
    "for(j=0;j<p.s.length;j++)"
    "h+='<span class=\"dot\" style=\"background:'+p.s[j].c+'\"></span>'+p.s[j].n;"
    "lg.innerHTML=h;"
    "if(LD.c[z])up('c'+(z+1),LD.c[z],p);}"
    /* DSP-REQ-07: applyPr + Persistenz (Browser-lokal, kein Server-State) */
    "function setPr(z,n){applyPr(z,n);"
    "try{localStorage.setItem('gt_profile_z'+z,n);}catch(e){}}"
    /* Garraum-Baender und Gas-Band einmalig faerben */
    "for(var i=1;i<=4;i++)"
    "document.querySelector('#b'+i+' .br').style.background=grad(GR.mn,GR.mx,GR.bd);"
    "document.querySelector('#g .br').style.background=grad(GS.mn,GS.mx,GS.bd);"
    /* DSP-REQ-07: Profile aus localStorage laden, fehlende/ungueltige -> 'rind' */
    "for(var z=0;z<4;z++){var v=null;"
    "try{v=localStorage.getItem('gt_profile_z'+z);}catch(e){}"
    "if(v&&PR[v])cp[z]=v;applyPr(z,cp[z]);}"
    /* Per-Chip-Handler: ermittelt Zone aus dem umschliessenden .pr[data-z] */
    "var allCh=document.querySelectorAll('.pr .ch');"
    "for(var k=0;k<allCh.length;k++)allCh[k].onclick=function(){"
    "var row=this.closest('.pr');if(!row)return;"
    "setPr(parseInt(row.dataset.z,10),this.dataset.p);};"
    "var es=new EventSource('/events');"
    "es.onmessage=function(e){var d=JSON.parse(e.data),i;"
    "for(i=0;i<4;i++){LD.b[i]=d.burner[i];LD.c[i]=d.core[i];"
    "up('b'+(i+1),d.burner[i],GR);"
    "up('c'+(i+1),d.core[i],PR[cp[i]]);}"
    "if(d.gas)up('g',d.gas,GS);};"
    "</script></body></html>";

/* WEB-REQ-06: SSE-Rahmen und JSON-Fragmente (kompaktes Format).
 * DSP-REQ-06: das gas-Feld ist ein Objekt (kein Array). */
static const char k_SseDataPre[]  = "data: {\"burner\":[";
static const char k_SseCore[]     = "],\"core\":[";
static const char k_SseGas[]      = "],\"gas\":";
static const char k_SseDataPost[] = "}\n\n";
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
/* Anzeige (DSP-REQ-02 Garraum / DSP-REQ-03 Kern)                      */
/* ------------------------------------------------------------------ */

/* group: 'b' = Garraum, 'c' = Kern. zoneCh: ASCII-Ziffer '1'..'4'.
 * withLegend = true bei Kernanzeige (DSP-REQ-03: Legende mit Garstufen). */
static void Html_AppendDisplay(BufCtx_t *ctx, char group, char zoneCh,
                               const char *label, size_t labelLen,
                               const Temp_Entry_t *entry, bool withLegend)
{
    Buf_Append(ctx, k_DispOpenA, sizeof(k_DispOpenA) - 1U);
    Buf_Append(ctx, &group, 1U);
    Buf_Append(ctx, &zoneCh, 1U);
    Buf_Append(ctx, k_DispOpenB, sizeof(k_DispOpenB) - 1U);
    Buf_Append(ctx, label, labelLen);
    Buf_Append(ctx, k_DispMid, sizeof(k_DispMid) - 1U);

    if (entry->valid) {
        Buf_AppendInt16(ctx, entry->value);
        Buf_Append(ctx, k_DegUnit, sizeof(k_DegUnit) - 1U);
    } else {
        Buf_Append(ctx, k_NotAvail, sizeof(k_NotAvail) - 1U);
    }

    Buf_Append(ctx, k_DispBar, sizeof(k_DispBar) - 1U);
    if (withLegend) {
        Buf_Append(ctx, k_DispLegend, sizeof(k_DispLegend) - 1U);
    }
    Buf_Append(ctx, k_DispClose, sizeof(k_DispClose) - 1U);
}

/* DSP-REQ-06: Gasflaschen-Anzeige (id "g") — Wert in %, Farbbalken mit Indikator. */
static void Html_AppendGas(BufCtx_t *ctx, const Temp_Entry_t *entry)
{
    Buf_Append(ctx, k_GasOpen, sizeof(k_GasOpen) - 1U);

    if (entry->valid) {
        Buf_AppendInt16(ctx, entry->value);
        Buf_Append(ctx, k_PctUnit, sizeof(k_PctUnit) - 1U);
    } else {
        Buf_Append(ctx, k_NotAvail, sizeof(k_NotAvail) - 1U);
    }

    Buf_Append(ctx, k_GasClose, sizeof(k_GasClose) - 1U);
}

/* ------------------------------------------------------------------ */
/* Vollstaendige Seite aufbauen        WEB-REQ-02ff, DSP-REQ-01..06    */
/* ------------------------------------------------------------------ */

static int Webserver_BuildHtml(const char *hostname, const Temp_Data_t *data)
{
    BufCtx_t ctx = { .buf = g_HtmlBuf, .size = HTML_BUF_SIZE,
                     .pos = 0U, .overflow = false };
    size_t   hLen = strlen(hostname);
    uint8_t  i;
    char     zoneCh;

    Buf_Append(&ctx, k_HtmlA,       sizeof(k_HtmlA)       - 1U);
    Buf_Append(&ctx, hostname,      hLen);
    Buf_Append(&ctx, k_HtmlCss,     sizeof(k_HtmlCss)     - 1U);
    Buf_Append(&ctx, hostname,      hLen);
    Buf_Append(&ctx, k_HtmlAfterH1, sizeof(k_HtmlAfterH1) - 1U);

    /* DSP-REQ-01: zweispaltiges Raster (2 x 2) fuer die vier Zonenbloecke */
    Buf_Append(&ctx, k_GridOpen, sizeof(k_GridOpen) - 1U);

    /* DSP-REQ-01: vier Zonenbloecke, je mit Garraum-, Kernanzeige und
     * DSP-REQ-07-Profilauswahl. zoneCh ist die 1-basierte Anzeigeziffer
     * (Heading + DOM-IDs), zoneIdx0Ch die 0-basierte Indexziffer fuer data-z. */
    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        char zoneIdx0Ch;

        zoneCh     = (char)('0' + (char)(i + 1U));
        zoneIdx0Ch = (char)('0' + (char)i);

        Buf_Append(&ctx, k_BlockOpen, sizeof(k_BlockOpen) - 1U);
        Buf_Append(&ctx, &zoneCh, 1U);
        Buf_Append(&ctx, k_BlockMid, sizeof(k_BlockMid) - 1U);

        /* DSP-REQ-02: Garraum (id "bN") */
        Html_AppendDisplay(&ctx, 'b', zoneCh,
                           k_LblGarraum, sizeof(k_LblGarraum) - 1U,
                           &data->burner[i], false);
        /* DSP-REQ-03: Kern (id "cN") mit Legende */
        Html_AppendDisplay(&ctx, 'c', zoneCh,
                           k_LblKern, sizeof(k_LblKern) - 1U,
                           &data->core[i], true);

        /* .ds schliessen, dann Per-Zone-Chip-Bar (DSP-REQ-07), dann .bl schliessen */
        Buf_Append(&ctx, k_DsClose,    sizeof(k_DsClose)    - 1U);
        Buf_Append(&ctx, k_ZoneProfA,  sizeof(k_ZoneProfA)  - 1U);
        Buf_Append(&ctx, &zoneIdx0Ch, 1U);
        Buf_Append(&ctx, k_ZoneProfB,  sizeof(k_ZoneProfB)  - 1U);
        Buf_Append(&ctx, k_BlockClose, sizeof(k_BlockClose) - 1U);
    }

    Buf_Append(&ctx, k_GridClose, sizeof(k_GridClose) - 1U);

    /* DSP-REQ-06: Gasflaschen-Anzeige zentriert unterhalb des Rasters */
    Html_AppendGas(&ctx, &data->gas);

    /* WEB-REQ-07 + DSP-REQ-04/05/06: clientseitige Logik */
    Buf_Append(&ctx, k_HtmlScript, sizeof(k_HtmlScript) - 1U);

    if (ctx.overflow) {
        return -ENOMEM;
    }

    return (int)ctx.pos;
}

/* ------------------------------------------------------------------ */
/* SSE-Datenframe (JSON)                                  WEB-REQ-06   */
/* ------------------------------------------------------------------ */

/* Einzelner Eintrag als {"v":<wert>,"ok":<0|1>}-Objekt */
static void Sse_AppendEntry(BufCtx_t *ctx, const Temp_Entry_t *entry)
{
    Buf_Append(ctx, k_JsonEntryPre, sizeof(k_JsonEntryPre) - 1U);
    Buf_AppendInt16(ctx, entry->value);
    if (entry->valid) {
        Buf_Append(ctx, k_JsonEntryOk1, sizeof(k_JsonEntryOk1) - 1U);
    } else {
        Buf_Append(ctx, k_JsonEntryOk0, sizeof(k_JsonEntryOk0) - 1U);
    }
}

/* Eine Gruppe als JSON-Array von {"v":<wert>,"ok":<0|1>}-Objekten */
static void Sse_AppendGroup(BufCtx_t *ctx, const Temp_Entry_t *arr)
{
    uint8_t i;

    for (i = 0U; i < (uint8_t)TEMP_ZONE_COUNT; i++) {
        if (i > 0U) {
            Buf_Append(ctx, ",", 1U);
        }
        Sse_AppendEntry(ctx, &arr[i]);
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
    Buf_Append(&ctx, k_SseGas, sizeof(k_SseGas) - 1U);
    Sse_AppendEntry(&ctx, &data->gas);
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
HTTP_SERVICE_DEFINE(esp32_grillthermo_svc, NULL, &g_HttpPort, 3, 3, NULL, NULL, NULL);
HTTP_RESOURCE_DEFINE(index_resource, esp32_grillthermo_svc, "/", &g_IndexDetail);
/* WEB-REQ-06: SSE-Endpoint */
HTTP_RESOURCE_DEFINE(events_resource, esp32_grillthermo_svc, "/events", &g_EventsDetail);

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
