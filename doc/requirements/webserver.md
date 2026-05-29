# Requirements: Webserver

## 1. Übersicht

Der Webserver stellt eine HTTP-Schnittstelle bereit, über die der Grill Buddy im lokalen Netzwerk erreichbar ist. Die Startseite zeigt den Gerätenamen sowie die aktuellen Grill-Messwerte (Brenner und Thermometer) in einem Browser an, ohne dass zusätzliche Software installiert werden muss.

---

## 2. Funktionale Anforderungen

### WEB-REQ-01

#### Beschreibung

Der Webserver soll beim Systemstart initialisiert werden und nach erfolgreicher WiFi-Verbindung HTTP-Anfragen auf Port 80 entgegennehmen. Solange keine WiFi-Verbindung besteht, bleibt der Server inaktiv. Nach Verbindungsaufbau wird der Server automatisch gestartet.

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Hoch      | Umgesetzt | `app/src/webserver.c:Webserver_Start()`, `Webserver_Stop()`; gerufen aus `app/src/wifi.c:Wifi_IpCallback()` bzw. `Wifi_EventCallback()` |

#### Abhängigkeiten

- WIF-REQ-01 (WiFi-Initialisierung)
- WIF-REQ-03 (WiFi-Statusmeldungen — Verbindungsaufbau als Startsignal)

#### Abnahmekriterien

- Nach erfolgreicher WiFi-Verbindung ist `http://<ip>/` im Browser erreichbar
- Vor der WiFi-Verbindung gibt der Server keine Antworten zurück
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

#### Beschreibung

Unterhalb der Kerntemperatur-Reihe soll eine Reihe mit vier Feldern für die Zieltemperatur der einzelnen Zonen dargestellt werden. Die Felder sind mit 1 bis 4 nummeriert. Oberhalb der Reihe erscheint die Überschrift „Zieltemperatur".

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:k_HtmlC` (statisches HTML, Platzhalterwerte `-- °C`) |

#### Abhängigkeiten

- WEB-REQ-03 (Brennertemperatur-Reihe)

#### Abnahmekriterien

- Die Überschrift „Zieltemperatur" erscheint über der Reihe
- Vier Felder, nummeriert 1–4, werden in einer horizontalen Reihe dargestellt
- Ohne aktive Konfiguration wird `-- °C` als Platzhalterwert angezeigt

---

### WEB-REQ-06

#### Beschreibung

Der Webserver soll einen Server-Sent-Events-Endpoint (`/events`) bereitstellen, über den die aktuellen Messwerte als `text/event-stream` an verbundene Browser gesendet werden. Beim Verbindungsaufbau wird der aktuelle Stand einmalig übertragen (initiale Synchronisierung). Danach sendet der Server einen neuen Event ausschließlich dann, wenn sich die Messwerte tatsächlich geändert haben — es findet kein zeitgesteuertes Senden in festen Intervallen statt.

Die Erkennung neuer Messwerte erfolgt über ein Änderungssignal des Temperaturdaten-Moduls: Der schreibende Erzeuger (künftig das Bluetooth-Modul) signalisiert nach jeder Aktualisierung von `g_TempData`, dass neue Werte vorliegen. Der SSE-Handler wartet auf dieses Signal, liest die Daten unter Mutex-Schutz und sendet sie.

Die Werte werden als JSON-Objekt übertragen, das für die drei Gruppen `burner`, `core` und `target` je vier Einträge mit Temperaturwert und Gültigkeitsflag enthält. Format (kompakt): `{"burner":[{"v":20,"ok":1},…×4],"core":[…],"target":[…]}` — `v` = Wert in °C, `ok` = 1 (gültig) bzw. 0 (`--`).

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
- Das JSON enthält für `burner[4]`, `core[4]` und `target[4]` je Wert (`value`) und Gültigkeit (`valid`)
- Der Zugriff auf `g_TempData` erfolgt zwischen `k_mutex_lock` und `k_mutex_unlock`
- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free`
- Bei längerer Inaktivität (keine Wertänderung) wird ein SSE-Kommentar (`: ka`) als Keepalive gesendet; eine abgebrochene Verbindung wird beim nächsten Sendeversuch erkannt und der Stream-Zustand zurückgesetzt
- Wegen des einthreadigen Servers ist nur ein `/events`-Client gleichzeitig aktiv (siehe Einschränkung oben)

---

### WEB-REQ-07

#### Beschreibung

Die Startseite soll JavaScript enthalten, das beim Laden eine `EventSource`-Verbindung zum `/events`-Endpoint (WEB-REQ-06) öffnet und bei jedem eintreffenden Event die zwölf Temperaturzellen (Brenner, Kern, Ziel) direkt im DOM aktualisiert, ohne die gesamte Seite neu zu laden. Ungültige Werte (`valid = false`) werden weiterhin als `--` dargestellt, gültige Werte als `<Wert> °C`.

Damit die Aktualisierung gezielt erfolgen kann, erhält jede Temperaturzelle einen stabilen, eindeutigen Bezeichner (`id`-Attribut): `b1`–`b4` (Brenner), `c1`–`c4` (Kern), `t1`–`t4` (Ziel).

| Priorität | Status | Implementierung |
|-----------|--------|-----------------|
| Mittel    | Umgesetzt | `app/src/webserver.c:k_HtmlScript` (EventSource-Client), `Html_AppendCell()` (Zell-IDs `b1`…`t4`) |

#### Abhängigkeiten

- WEB-REQ-06 (SSE-Endpoint)
- WEB-REQ-03, WEB-REQ-04, WEB-REQ-05 (anzuzeigende Zellen)

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
| Hoch      | Zuverlässigkeit | Offen  |                 |

#### Abhängigkeiten

Keine.

#### Abnahmekriterien

- Kein Aufruf von `malloc`, `calloc`, `realloc` oder `free` im Webserver-Quellcode
- HTTP-Antwortpuffer sind als Compile-Zeit-Konstanten dimensioniert

---

## 4. Offene Punkte / Annahmen

- [ ] Temperaturwerte (WEB-REQ-03/04/05) zeigen aktuell Platzhalterwerte; Echtdaten abhängig von der Bluetooth-Implementierung
- [x] **HTTPS / TLS ist nicht erforderlich.** Das Gerät wird ausschließlich im vertrauten Heimnetzwerk betrieben, überträgt keine sensiblen Daten (nur Grill-Messwerte) und ist nicht öffentlich erreichbar. Der Sicherheitsgewinn wäre gering, die Kosten dagegen hoch: ~60 KB mbedTLS-Heap plus TLS-Kontext je Verbindung (konkurriert mit WiFi-Stack und Netzwerkpuffern), zusätzlicher Flash-Bedarf sowie dauerhafte Browser-Warnungen mangels vertrauenswürdigem Zertifikat im LAN. Entscheidung: bewusst nur HTTP auf Port 80.
- [x] Gleichzeitige HTTP-Verbindungen auf maximal 3 begrenzt (siehe WEB-REQ-08)
- [x] Änderungssignal des Temperaturdaten-Moduls umgesetzt (TMP-REQ-03: `g_TempGen`, `g_TempCondvar`, `Temp_Set`, `Temp_NotifyChanged`)
- [x] **SSE bedient nur einen Client gleichzeitig** — bewusst akzeptiert. Der einthreadige Zephyr-HTTP-Server ruft dynamische Handler synchron in einer `do-while(!final_chunk)`-Schleife auf; ein blockierender SSE-Handler belegt den einzigen Server-Thread. Echte Mehrfach-Live-Updates erforderten einen Worker-/Poll-basierten Ansatz oder mehrere Server-Threads.
- [x] JSON-Format festgelegt: `{"burner":[{"v":<int>,"ok":<0|1>}…],"core":[…],"target":[…]}` (kompakt, je 4 Einträge)
- [ ] Echtdaten erst mit der Bluetooth-Implementierung; bis dahin Test über Shell-Befehl `temp set`/`temp clear`

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
