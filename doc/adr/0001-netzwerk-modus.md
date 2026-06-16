# [ADR-0001] Webserver und MQTT-Client gegenseitig ausschließen

**Status**: Accepted
**Last Updated**: 2026-06-16
**Author**: Siegfried Kamlah
**Approved by**: Siegfried Kamlah

## Context

Das ESP32 Grillthermo stellt Messdaten über zwei Netzwerkdienste bereit:

- **Webserver** (HTTP, Port 80): Browser-Zugriff auf eine HTML-Seite mit Live-Daten via Server-Sent Events
- **MQTT-Client**: Verbindung zu einem konfigurierten Broker; Messwerte werden alle 10 Sekunden als Topics publiziert

Beide Dienste liefen nach dem WiFi-Verbindungsaufbau gleichzeitig. In der Praxis hat sich gezeigt, dass Webserver und MQTT-Client nicht stabil nebeneinander betrieben werden können. Ursache sind die begrenzten Netzwerkressourcen des Zephyr-Netzwerk-Stacks auf dem ESP32-S3:

- **`CONFIG_NET_MAX_CONTEXTS`**: Anzahl der verfügbaren Netzwerk-Kontexte ist begrenzt; Webserver (3 gleichzeitige Verbindungen, WEB-REQ-08) und MQTT-Client (1 Verbindung plus DNS-Auflösung) erschöpfen diesen Pool gemeinsam.
- **Socket-Puffer**: Beide Dienste belegen gleichzeitig Empfangs- und Sendepuffer im Netzwerk-Stack.
- **Startverhalten**: Webserver und MQTT-Client konkurrierten um den Startzeitpunkt nach DHCP-Zuweisung, was zu Initialisierungsfehlern führte (Issue #4).

Eine Erhöhung von `CONFIG_NET_MAX_CONTEXTS` auf 10 (Commit `6b5e4cd`) linderte das Problem, beseitigte es aber nicht dauerhaft — der Speicherbedarf steigt linear mit der Anzahl der Kontexte, und bei zwei gleichzeitig aktiven SSE-Verbindungen plus MQTT-Reconnect treten erneut Konflikte auf.

## Decision

**Webserver und MQTT-Client arbeiten im laufenden Betrieb nicht gleichzeitig.** Beim Start aktiviert die Firmware genau einen der beiden Dienste — abhängig vom im Konfigurationsspeicher (NVS) hinterlegten Netzwerkmodus (CFG-REQ-07).

Die Auswahl erfolgt über die Shell (SHL-REQ-11):

```
config mode webserver   → Webserver aktiv, MQTT-Client inaktiv
config mode mqtt        → MQTT-Client aktiv, Webserver inaktiv
```

Der gewählte Modus wird sofort in den NVS geschrieben. Ein anschließender Neustart des Geräts aktiviert den neuen Modus. **Standardmodus nach Erstinbetriebnahme und Factory Reset: `webserver`.**

Der bisherige Laufzeit-Schalter `mqtt enable` / `mqtt disable` (MQT-REQ-05) wird durch diesen Mechanismus ersetzt und entfällt.

## Consequences

**Positiv:**
- Keine Ressourcenkonflikte im Netzwerk-Stack; `CONFIG_NET_MAX_CONTEXTS` kann auf den für den jeweiligen Dienst minimal nötigen Wert reduziert werden.
- Deterministisches Startverhalten: genau ein Dienst initialisiert sich nach DHCP-Zuweisung.
- Einfachere Fehlersuche: entweder der Webserver oder der MQTT-Client ist aktiv.
- MQT-REQ-05 (`mqtt enable`/`mqtt disable`) entfällt; die Moduswahl übernimmt diese Funktion vollständig.

**Negativ:**
- Webserver und MQTT können nicht gleichzeitig genutzt werden.
- Ein Moduswechsel erfordert einen Neustart des Geräts.
- Neuer Shell-Befehl `config mode` muss implementiert und dokumentiert werden.
- Der Standard-Netzwerkmodus (`webserver`) muss beim Factory Reset korrekt wiederhergestellt werden.

## Alternatives Considered

**1. Beide Dienste gleichzeitig mit erhöhten Ressourcen:**
`CONFIG_NET_MAX_CONTEXTS` und Puffergrößen weiter erhöhen, um beide Dienste stabil parallel zu betreiben. Abgelehnt: Der ESP32-S3 hat 512 KB SRAM; jeder zusätzliche Netzwerk-Kontext belegt mehrere Hundert Bytes. Stabilität bei Last (mehrere Browser + MQTT-Reconnect) ist nicht garantiert; Testaufwand übersteigt den Nutzen.

**2. Kconfig-Auswahl zur Compile-Zeit:**
Webserver oder MQTT als Kconfig-`choice` — das aktive Subsystem wird fest in das Binary eingebaut. Abgelehnt: Erfordert erneutes Flashen für jeden Wechsel. Da der Nutzer den Modus im Betrieb wechseln können soll, ist ein compilezeitiger Ansatz zu unflexibel.

**3. MQTT nach Webserver-Bedarf drosseln:**
MQTT nur dann verbinden, wenn der Webserver keine aktiven Verbindungen hat. Abgelehnt: Zu komplex, nicht deterministisch, schwer testbar.

## Compliance

- Einhaltung durch Modusprüfung in `Webserver_Start()` und `Mqtt_Thread()` gegen `Config_Data_t.networkMode` beim Start.
- `config show` gibt den aktuellen Modus aus — manuell verifizierbar.
- Abnahmekriterien in WEB-REQ-01, MQT-REQ-01, CFG-REQ-07 und SHL-REQ-11 decken das korrekte Verhalten ab.

## Related ADRs

Keine.
