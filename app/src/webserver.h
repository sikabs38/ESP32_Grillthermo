/* WEB-REQ-01, WEB-REQ-02 */
#ifndef APP_WEBSERVER_H
#define APP_WEBSERVER_H

/* WEB-REQ-01: Server nach WiFi-Verbindungsaufbau starten */
void Webserver_Start(void);

/* WEB-REQ-01: Server bei Verbindungsverlust stoppen */
void Webserver_Stop(void);

#endif /* APP_WEBSERVER_H */
