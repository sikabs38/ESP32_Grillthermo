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
- [ ] HTTPS / TLS nicht vorgesehen (lokales Netzwerk, kein öffentlicher Zugriff)
- [ ] Mehrere gleichzeitige HTTP-Verbindungen noch nicht spezifiziert

---

## 5. Änderungshistorie

| Version | Datum      | Autor | Änderung |
|---------|------------|-------|----------|
| 1.0     | 2026-05-28 |       | Erstellt: WEB-REQ-01 (Serverstart), WEB-REQ-02 (Titelzeile mit Hostname) |
| 1.1     | 2026-05-28 |       | WEB-REQ-02 erweitert: Titelzeile in zentrierter Vollbreite-Box |
| 1.2     | 2026-05-28 |       | WEB-REQ-01 und WEB-REQ-02 umgesetzt |
| 1.3     | 2026-05-28 |       | WEB-REQ-02 erweitert: schwarzer 2pt-Rahmen; WEB-REQ-03/04/05 ergänzt und umgesetzt: Brenner-, Kern- und Zieltemperatur-Reihen |
