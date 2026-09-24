/*
 * web_server.h - WiFi SoftAP + captive portal + hand-rolled HTTP server that
 * hosts the browser web app and bridges ELM commands over plain HTTP.
 *
 * Why HTTP (not WebSocket): OBD is pure request/response, so one POST per ELM
 * command is enough - no framing, no extra library. This is the transport that
 * iOS Safari (which has no Web Bluetooth) and any browser can use.
 *
 * Routes (port WEBAPP_HTTP_PORT):
 *   GET  /        -> the gzipped web app (App/www/index.html, ble-shim stripped) from PROGMEM
 *   POST /elm     -> claim the WiFi session, run Elm327::handleLine(body), return the reply
 *   GET  <other>  -> small no-JS captive landing page pointing at the app (mini-browsers can't run the SPA)
 *
 * A wildcard DNS server (:53) points every lookup at the AP so the phone's
 * captive-portal probe lands here and pops the "sign in" sheet.
 *
 * The HTTP handler is NON-BLOCKING (a bounded pass per poll()) so the main loop's
 * deep-sleep check keeps running. Connection: close per request (no keep-alive)
 * keeps the parser trivial; the app issues one short POST per command.
 */
#ifndef TD5_WEB_SERVER_H
#define TD5_WEB_SERVER_H

#include "config.h"
#if ENABLE_WIFI_WEBAPP

#include <Arduino.h>
#include <WiFi.h>
#include <DNSServer.h>
#include "elm327.h"
#include "transport_arbiter.h"

class WebAppServer {
public:
  WebAppServer(Elm327& elm, TransportArbiter& arb, uint16_t port)
    : _elm(elm), _arb(arb), _http(port), _port(port) {}

  bool begin();       // SoftAP + wildcard DNS + HTTP listener
  void poll();        // service DNS + one HTTP client (non-blocking)
  void shutdown();    // stop DNS/HTTP + radio off (called when BLE wins the session)
  bool started() const { return _started; }

private:
  Elm327&           _elm;
  TransportArbiter& _arb;
  WiFiServer        _http;
  DNSServer         _dns;
  uint16_t          _port;
  bool              _started = false;

  // Per-connection request scratch (one client serviced at a time).
  WiFiClient        _client;
  bool              _busy     = false;
  uint32_t          _reqStart = 0;
  String            _req;

  void serviceClient();
  void handleRequest(const String& method, const String& path, const String& body);
  void sendApp();      // GET / -> gzipped app
  void sendCaptive();  // catch-all -> launcher page
  void sendText(int code, const char* status, const char* ctype, const String& body);
};

#endif // ENABLE_WIFI_WEBAPP
#endif // TD5_WEB_SERVER_H
