# Requirements: Webserver

## 1. Übersicht

Der Webserver stellt eine HTTP-Schnittstelle bereit, über die das ESP32 Grillthermo im lokalen Netzwerk erreichbar ist. Die Startseite zeigt den Gerätenamen sowie die aktuellen Grill-Messwerte (Brenner und Thermometer) in einem Browser an, ohne dass zusätzliche Software installiert werden muss.

---

## 2. Funktionale Anforderungen

### WEB-REQ-01

#### Beschreibung

Der Webserver soll beim Systemstart initialisiert werden und nach erfolgreicher WiFi-Verbindung HTTP-Anfragen auf Port 80 entgegennehmen. Solange keine WiFi-Verbindung besteht, bleibt der Server inaktiv. Nach Verbindungsaufbau wird der Server automatisch gestartet.

**Der Webserver wird nur gestartet, wenn der konfigurierte Netzwerkmodus `webserver` ist (CFG-REQ-07, ADR-001).** Im Modus `mqtt` bleibt der Webserver dauerhaft inaktiv.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/webserver.c:Webserver_Start()`, `Webserver_Stop()`; Modusprüfung in `app/src/main.c`: `Webserver_Start()` nur bei `cfg.networkMode == CFG_NETWORK_WEBSERVER` |

#### Abhängigkeiten

- WIF-REQ-01 (WiFi-Initialisierung)
- WIF-REQ-03 (WiFi-Statusmeldungen — Verbindungsaufbau als Startsignal)
- CFG-REQ-07 (Netzwerkmodus — Webserver nur aktiv bei `networkMode = webserver`)

#### Abnahmekriterien

- Nach erfolgreicher WiFi-Verbindung ist `http://<ip>/` im Browser erreichbar, **sofern** der Netzwerkmodus `webserver` ist
- Vor der WiFi-Verbindung gibt der Server keine Antworten zurück
- Ist der Netzwerkmodus `mqtt`, startet der Webserver nicht — auch nicht nach erfolgreicher WiFi-Verbindung
- Der Server läuft auf Port 80 (HTTP)
- Der Server blockiert nicht den WiFi-Thread oder die Shell

---

### WEB-REQ-02

#### Beschreibung

Die Startseite des Webservers soll eine Titelzeile anzeigen, die den konfigurierten Hostnamen des Geräts enthält. Der Hostname wird sowohl im HTML-`<title>`-Element (Browser-Tab) als auch als sichtbare Überschrift (`<h1>`) auf der Seite ausgegeben. Ist kein Hostname konfiguriert, wird der Zephyr-Standardhostname verwendet.

Die Überschrift wird in einer Box dargestellt, die die volle Fensterbreite einnimmt. Der Text ist innerhalb der Box horizontal zentriert. Die Box ist mit einem schwarzen Rahmen von 2 pt Breite umgeben.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/webserver.c:Webserver_IndexCb()`, `Webserver_BuildHtml()`; Hostname via `net_hostname_get()` |

#### Abhängigkeiten

- WEB-REQ-01 (Webserver-Start)
- WIF-REQ-06 (Konfigurierbarer Hostname)

#### Abnahmekriterien

- Der Browser-Tab zeigt den Hostnamen als Seitentitel (`<title>`)
- Die Seite enthält eine `<h1>`-Überschrift mit dem Hostnamen
- Die Überschrift ist in einer Box (`<div>`) mit `width: 100%` und `box-sizing: border-box` eingebettet
- Der Text ist innerhalb der Box horizontal zentriert (`text-align: center`)
- Die Box nimmt die volle Breite des Browserfensters ein und passt sich bei Größenänderung des Fensters an (kein fixer Pixelwert)
- Die Box ist mit einem schwarzen Rahmen von 2 pt Breite umgeben (`border: 2px solid black`)
- Ist `wifiHostname` in der Konfiguration gesetzt, wird dieser angezeigt
- Ist kein Hostname konfiguriert, wird der Rückgabewert von `net_hostname_get()` verwendet
- Die Titelzeile wird bei jeder HTTP-Anfrage aktuell aus dem Hostname-API gelesen

---

### WEB-REQ-03

#### Beschreibung

Unterhalb der Titelzeile soll eine Reihe mit vier Temperaturanzeigen für die Brennerzonen des Grills dargestellt werden. Die Zellen sind mit 1 bis 4 nummeriert und zeigen die Brennertemperatur an. Oberhalb der Reihe erscheint die Überschrift „Brennertemperatur". Solange keine Bluetooth-Verbindung besteht, werden Platzhalterwerte angezeigt.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:k_HtmlC` (statisches HTML, Platzhalterwerte `-- °C`) |

#### Abhängigkeiten

- WEB-REQ-01 (Webserver-Start)
- WEB-REQ-02 (Seitenstruktur)

#### Abnahmekriterien

- Die Überschrift „Brennertemperatur" erscheint über der Reihe
- Vier Zellen, nummeriert 1–4, werden in einer horizontalen Reihe dargestellt
- Die Zellen passen sich in der Breite gleichmäßig dem Browserfenster an (`flex: 1`)
- Ohne aktive Bluetooth-Verbindung wird `-- °C` als Platzhalterwert angezeigt

---

### WEB-REQ-04

#### Beschreibung

Unterhalb der Brennertemperatur-Reihe soll eine weitere Reihe mit vier Temperaturanzeigen für die Fleischthermometer dargestellt werden. Die Zellen sind mit 1 bis 4 nummeriert und zeigen die Kerntemperatur an. Oberhalb der Reihe erscheint die Überschrift „Kerntemperatur".

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:k_HtmlC` (statisches HTML, Platzhalterwerte `-- °C`) |

#### Abhängigkeiten

- WEB-REQ-03 (Brennertemperatur-Reihe)

#### Abnahmekriterien

- Die Überschrift „Kerntemperatur" erscheint über der Reihe
- Vier Zellen, nummeriert 1–4, werden in einer horizontalen Reihe dargestellt
- Ohne aktive Bluetooth-Verbindung wird `-- °C` als Platzhalterwert angezeigt

---

### WEB-REQ-05

> **Abgelöst durch DSP-REQ-01..04** (siehe `doc/requirements/display.md`). Die separate Zieltemperatur-Reihe entfällt: An ihre Stelle treten die vier Zonenblöcke mit profilabhängiger Farbskala je Kerntemperatur-Anzeige. Das Feld `target[]` wurde aus `Temp_Data_t` entfernt; die Shell-Befehle `temp set target` / `temp clear target` existieren nicht mehr.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Abgelöst | — (ersetzt durch DSP-REQ-01..04) |

---

### WEB-REQ-06

#### Beschreibung

Der Webserver soll einen Server-Sent-Events-Endpoint (`/events`) bereitstellen, über den die aktuellen Messwerte als `text/event-stream` an verbundene Browser gesendet werden. Beim Verbindungsaufbau wird der aktuelle Stand einmalig übertragen (initiale Synchronisierung). Danach sendet der Server einen neuen Event ausschließlich dann, wenn sich die Messwerte tatsächlich geändert haben — es findet kein zeitgesteuertes Senden in festen Intervallen statt.

Die Erkennung neuer Messwerte erfolgt über ein Änderungssignal des Temperaturdaten-Moduls: Der schreibende Erzeuger (künftig das Bluetooth-Modul) signalisiert nach jeder Aktualisierung von `g_TempData`, dass neue Werte vorliegen. Der SSE-Handler wartet auf dieses Signal, liest die Daten unter Mutex-Schutz und sendet sie.

Die Werte werden als JSON-Objekt übertragen. Die beiden Gruppen `burner` und `core` enthalten je vier Einträge (Temperaturwert in °C, Gültigkeitsflag), das Feld `gas` enthält einen einzelnen Eintrag (Füllstand in %, DSP-REQ-06). Format (kompakt): `{"burner":[{"v":20,"ok":1},…×4],"core":[…],"gas":{"v":75,"ok":1}}` — `v` = Wert, `ok` = 1 (gültig) bzw. 0 (`--`). Die Gruppe `target` wurde mit der Ablösung von WEB-REQ-05 durch DSP-REQ-01..04 entfernt.

> **Einschränkung (bewusst akzeptiert):** Der Zephyr-HTTP-Server arbeitet einthreadig; der SSE-Handler blockiert während des Streamens auf der Bedingungsvariablen und belegt diesen Thread dauerhaft. Dadurch ist effektiv nur **ein** `/events`-Client gleichzeitig bedienbar — ein zweiter Browser erhält erst nach Abbau der ersten SSE-Verbindung eine Antwort. Dies weicht vom 3-Client-Ziel aus WEB-REQ-08 ab und wurde so entschieden.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:Webserver_EventsCb()`, `Webserver_BuildSseData()`, `Sse_AppendGroup()`; `HTTP_RESOURCE_DEFINE(events_resource, …, "/events", …)`; blockiert auf `g_TempCondvar` (TMP-REQ-03) |

#### Abhängigkeiten

- WEB-REQ-01 (Webserver-Start)
- TMP-REQ-02 (Temperaturdaten unter Mutex)
- TMP-REQ-03 (Änderungssignal des Temperaturdaten-Moduls)

#### Abnahmekriterien

- `GET /events` liefert die Antwort mit `Content-Type: text/event-stream`
- Beim Verbindungsaufbau wird der aktuelle Messwert-Stand einmalig gesendet
- Ein neuer `data:`-Event wird nur bei tatsächlicher Wertänderung gesendet (kein festes Sende-Intervall ohne Änderung)
- Das JSON enthält für `burner[4]`, `core[4]` und `gas` je Wert (`value`) und Gültigkeit (`valid`)
- Der Zugriff auf `g_TempData` erfolgt zwischen `k_mutex_lock` und `k_mutex_unlock`
- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free`
- Bei längerer Inaktivität (keine Wertänderung) wird ein SSE-Kommentar (`: ka`) als Keepalive gesendet; eine abgebrochene Verbindung wird beim nächsten Sendeversuch erkannt und der Stream-Zustand zurückgesetzt
- Wegen des einthreadigen Servers ist nur ein `/events`-Client gleichzeitig aktiv (siehe Einschränkung oben)

---

### WEB-REQ-07

#### Beschreibung

Die Startseite soll JavaScript enthalten, das beim Laden eine `EventSource`-Verbindung zum `/events`-Endpoint (WEB-REQ-06) öffnet und bei jedem eintreffenden Event die acht Temperaturanzeigen (Garraum und Kern je Zone) direkt im DOM aktualisiert, ohne die gesamte Seite neu zu laden. Ungültige Werte (`valid = false`) werden weiterhin als `--` dargestellt, gültige Werte als `<Wert> °C`.

Damit die Aktualisierung gezielt erfolgen kann, erhält jeder Anzeigeblock einen stabilen, eindeutigen Bezeichner (`id`-Attribut): `b1`–`b4` (Garraum), `c1`–`c4` (Kern).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:k_HtmlScript` (EventSource-Client + `up()`-Funktion), `Html_AppendDisplay()` (IDs `b1`–`b4`, `c1`–`c4`) |

#### Abhängigkeiten

- WEB-REQ-06 (SSE-Endpoint)
- DSP-REQ-02, DSP-REQ-03 (Garraum- und Kerntemperatur-Anzeige)

#### Abnahmekriterien

- Die Seite öffnet beim Laden automatisch eine `EventSource` auf `/events`
- Eintreffende Events aktualisieren die betroffenen Zellen ohne vollständigen Seiten-Reload
- `valid = false` wird als `--` dargestellt, sonst `<Wert>&nbsp;°C`
- Jede Zelle ist über einen stabilen, eindeutigen Selektor adressierbar
- Bei Verbindungsabbruch baut der Browser die Verbindung selbsttätig wieder auf (Standardverhalten von `EventSource`)
- Ohne JavaScript bleibt die initial gelieferte Seite mit den zuletzt gesendeten Werten lesbar (progressive Verschlechterung)

---

### WEB-REQ-08

#### Beschreibung

Der Webserver soll maximal drei gleichzeitige HTTP-Verbindungen (Clients) zulassen. Weitere Verbindungsversuche werden bis zum Freiwerden eines Slots im Backlog gehalten bzw. abgewiesen. Diese Begrenzung schützt die begrenzten Ressourcen des ESP32 (Sockets, Netzwerkpuffer, RAM).

Da eine dauerhaft offene SSE-Verbindung (WEB-REQ-06) je Browser einen Client-Slot belegt, begrenzt dieser Wert zugleich die Anzahl gleichzeitig live aktualisierter Browser-Sitzungen.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:HTTP_SERVICE_DEFINE(... , 3, 3, ...)`; `CONFIG_HTTP_SERVER_MAX_CLIENTS=3` in `app/prj.conf` |

#### Abhängigkeiten

- WEB-REQ-01 (Webserver-Start)

#### Abnahmekriterien

- `HTTP_SERVICE_DEFINE` ist mit `concurrent = 3` definiert
- `CONFIG_HTTP_SERVER_MAX_CLIENTS` ist auf `3` gesetzt (Bedingung `concurrent <= MAX_CLIENTS` erfüllt)
- Bis zu drei Browser können gleichzeitig verbunden sein und die Seite abrufen
- Ein vierter gleichzeitiger Verbindungsversuch erhält erst nach Freiwerden eines Slots Zugriff
- `CONFIG_ZVFS_POLL_MAX` ist groß genug für `1 + NUM_SERVICES + MAX_CLIENTS` (= 5) Poll-Einträge

---

### WEB-REQ-10

#### Beschreibung

Die Titelzeile der Webseite soll die Softwareversionsnummer enthalten. Der angezeigte Text lautet `<Hostname> Version X.Y` — der Hostname gefolgt von einem Leerzeichen, dem Wort „Version" und der aktuellen Versionsnummer aus `ESP32_GRILLTHERMO_VERSION_STRING`. Dieser Text erscheint sowohl im HTML-`<title>`-Tag (Browser-Tab) als auch im `<h1>`-Element des sichtbaren Seitenkopfes.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Niedrig   | Umgesetzt | `app/src/webserver.c:Webserver_BuildHtml()`, `k_VersionSuffix` |

#### Abhängigkeiten

- WEB-REQ-02 (Titelzeile mit Hostname)

#### Abnahmekriterien

- Der Browser-Tab zeigt `<Hostname> Version X.Y`
- Die sichtbare Überschrift der Seite zeigt `<Hostname> Version X.Y`
- Der Versionsstring entspricht `ESP32_GRILLTHERMO_VERSION_STRING` aus `version.h`
- Das Format ist `<net_hostname_get()> Version <ESP32_GRILLTHERMO_VERSION_STRING>` (ein Leerzeichen vor und nach „Version")

---

## 3. Nicht-funktionale Anforderungen

### WEB-NFR-01

#### Beschreibung

Die Webserver-Implementierung soll den MISRA-C-Coding-Standards des Projekts entsprechen.

| Priorität | Kategorie   | Status | Implementierung |
|-----------|-------------|--------|-----------------|
| Hoch      | Wartbarkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Verstoß gegen MISRA C 2023 bei Prüfung mit `cppcheck --addon=misra`
- Abweichungen sind mit Kommentar und Begründung dokumentiert

---

### WEB-NFR-02

#### Beschreibung

Der Webserver soll ausschließlich statischen Speicher verwenden. Dynamische Speicherverwaltung (`malloc`, `free`) ist nicht erlaubt.

| Priorität | Kategorie       | Status | Implementierung |
|-----------|-----------------|--------|-----------------|
| Hoch      | Zuverlässigkeit | Umgesetzt | `app/src/webserver.c` — statische Puffer für HTML und SSE-Ausgabe; kein `malloc`/`free` |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im Webserver-Quellcode
- HTTP-Antwortpuffer sind als Compile-Zeit-Konstanten dimensioniert

---

## 4. Offene Punkte / Annahmen

- [ ] Temperaturwerte (WEB-REQ-03/04) zeigen aktuell Platzhalterwerte; Echtdaten abhängig von der Bluetooth-Implementierung
- [x] **HTTPS / TLS ist nicht erforderlich.** Das Gerät wird ausschließlich im vertrauten Heimnetzwerk betrieben, überträgt keine sensiblen Daten (nur Grill-Messwerte) und ist nicht öffentlich erreichbar. Der Sicherheitsgewinn wäre gering, die Kosten dagegen hoch: ~60 KB mbedTLS-Heap plus TLS-Kontext je Verbindung (konkurriert mit WiFi-Stack und Netzwerkpuffern), zusätzlicher Flash-Bedarf sowie dauerhafte Browser-Warnungen mangels vertrauenswürdigem Zertifikat im LAN. Entscheidung: bewusst nur HTTP auf Port 80.
- [x] Gleichzeitige HTTP-Verbindungen auf maximal 3 begrenzt (siehe WEB-REQ-08)
- [x] Änderungssignal des Temperaturdaten-Moduls umgesetzt (TMP-REQ-03: `g_TempGen`, `g_TempCondvar`, `Temp_Set`, `Temp_NotifyChanged`)
- [x] **SSE bedient nur einen Client gleichzeitig** — bewusst akzeptiert. Der einthreadige Zephyr-HTTP-Server ruft dynamische Handler synchron in einer `do-while(!final_chunk)`-Schleife auf; ein blockierender SSE-Handler belegt den einzigen Server-Thread. Echte Mehrfach-Live-Updates erforderten einen Worker-/Poll-basierten Ansatz oder mehrere Server-Threads.
- [x] JSON-Format festgelegt: `{"burner":[{"v":<int>,"ok":<0|1>}…],"core":[…],"gas":{…}}` (`burner`/`core` je 4 Einträge, `gas` Einzeleintrag; `target` entfernt mit WEB-REQ-05/09)
- [ ] Echtdaten erst mit der Bluetooth-Implementierung; bis dahin Test über Shell-Befehl `temp set <burner|core> …` / `temp clear <burner|core> …`

### WEB-REQ-09

> **Abgelöst durch DSP-REQ-03/04** (siehe `doc/requirements/display.md`). Die Differenz-basierte Hintergrundfärbung der Kerntemperatur-Zelle entfällt: An ihre Stelle tritt der profilabhängige Farbbalken der Kerntemperatur-Anzeige mit beweglichem Indikator und Garstufen-Legende.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Abgelöst | — (ersetzt durch DSP-REQ-03/04) |

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-28 |       | Erstellt: WEB-REQ-01 (Serverstart), WEB-REQ-02 (Titelzeile mit Hostname) |
| 1.1     | 2026-05-28 |       | WEB-REQ-02 erweitert: Titelzeile in zentrierter Vollbreite-Box |
| 1.2     | 2026-05-28 |       | WEB-REQ-01 und WEB-REQ-02 umgesetzt |
| 1.3     | 2026-05-28 |       | WEB-REQ-02 erweitert: schwarzer 2pt-Rahmen; WEB-REQ-03/04/05 ergänzt und umgesetzt: Brenner-, Kern- und Zieltemperatur-Reihen |
| 1.4     | 2026-05-28 |       | WEB-REQ-06 (SSE-Endpoint, Push bei Wertänderung) und WEB-REQ-07 (clientseitige Aktualisierung via EventSource) ergänzt |
| 1.5     | 2026-05-28 |       | Entscheidung dokumentiert: TLS im Heimnetzwerk nicht erforderlich (nur HTTP) |
| 1.6     | 2026-05-28 |       | WEB-REQ-08 ergänzt und umgesetzt: maximal 3 gleichzeitige HTTP-Verbindungen |
| 1.7     | 2026-05-29 |       | WEB-REQ-06/07 umgesetzt: SSE-Endpoint `/events` mit Push bei Wertänderung, clientseitige Aktualisierung via `EventSource`; JSON-Format und Ein-Client-Einschränkung dokumentiert |
| 1.8     | 2026-05-29 |       | WEB-REQ-09 ergänzt: farbliche Hervorhebung der Kerntemperatur-Zellen in Abhängigkeit von der Zieltemperatur |
| 1.9     | 2026-05-29 |       | WEB-REQ-09 umgesetzt: `col()`-Funktion im JavaScript-Client (`k_HtmlScript`) |
| 1.10    | 2026-05-29 |       | WEB-REQ-05 und WEB-REQ-09 abgelöst durch DSP-REQ-01..04; SSE-Feld `target` entfernt; WEB-REQ-07 auf `b1`–`b4` / `c1`–`c4` und Implementierungsverweis auf `Html_AppendDisplay()` aktualisiert |
| 1.11    | 2026-05-29 |       | SSE-Feld `gas` (Einzeleintrag) ergänzt für DSP-REQ-06 |
| 1.12    | 2026-06-14 |       | WEB-REQ-10 ergänzt: Versionsnummer in Titelzeile (`<Hostname> Version X.Y`) |
| 1.13    | 2026-06-15 |       | Status-Update: WEB-NFR-02 → Umgesetzt |
| 1.14    | 2026-06-16 |       | WEB-REQ-01 erweitert: Webserver nur aktiv bei Netzwerkmodus `webserver` (ADR-001, CFG-REQ-07) |
| 1.15    | 2026-06-16 |       | WEB-REQ-01: Status → Umgesetzt; Modusprüfung in main.c implementiert |
