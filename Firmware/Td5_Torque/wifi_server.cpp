/*
 * wifi_server.cpp - WiFi SoftAP + TCP server implementation.
 * See wifi_server.h for the overview.
 */

#include "config.h"
#if ENABLE_WIFI_ELM
#include "wifi_server.h"

bool WifiElmServer::begin() {
  WiFi.mode(WIFI_AP);

  IPAddress apIp(ELM_AP_IP_0, ELM_AP_IP_1, ELM_AP_IP_2, ELM_AP_IP_3);
  IPAddress gateway = apIp;                 // the ESP32 is its own gateway
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, gateway, subnet);

  const char* pass = (strlen(AP_PASS) > 0) ? AP_PASS : nullptr;
  bool ok = WiFi.softAP(AP_SSID, pass, AP_CHANNEL, 0 /*hidden*/, AP_MAX_CLIENTS);

  _server.begin();
  _server.setNoDelay(true);

#if DEBUG_SERIAL
  Serial.printf("[WiFi] SoftAP \"%s\" %s\n", AP_SSID, ok ? "up" : "FAILED");
  Serial.print("[WiFi] Connect the phone to that network, then point the OBD app at ");
  Serial.print(WiFi.softAPIP());
  Serial.printf(":%u\n", _port);
#endif
  return ok;
}

void WifiElmServer::poll() {
  // Always check for an incoming connection so a reconnect is never starved by
  // a stale half-open socket that still reads connected()==true. On a car
  // SoftAP the phone's WiFi flaps and the abandoned socket can linger for a
  // long time; if we gated the accept on the old client we'd never dequeue the
  // backlog and Torque would "connect" (TCP handshake OK) yet receive nothing.
  WiFiClient nc = _server.available();
  if (nc && nc.connected()) {
    if (_hasClient) _client.stop();       // pre-empt any existing session
    _client = nc;
    _client.setNoDelay(true);
    _hasClient = true;
    _rx = "";
#if DEBUG_SERIAL
    Serial.println("[WiFi] ELM client connected");
#endif
  } else if (_hasClient && !_client.connected()) {
    // Nothing new waiting and the current client is genuinely gone: clean up.
    _client.stop();
    _hasClient = false;
    _rx = "";
  }

  // Service the live client.
  if (_hasClient && _client.connected()) {
    while (_client.available()) {
      processByte((char)_client.read());
    }
  }
}

void WifiElmServer::processByte(char c) {
  if (c == '\r') {
    String cmd = _rx;
    _rx = "";
    String resp = _elm.handleLine(cmd);
#if DEBUG_SERIAL
    {
      String vis = resp;
      vis.replace("\r", "\\r");
      vis.replace("\n", "\\n");
      Serial.printf("[ELM] <%s> -> %s\n", cmd.c_str(), vis.c_str());
    }
#endif
    _client.print(resp);
  } else if (c == '\n') {
    // Ignore stray line feeds; commands are CR-terminated.
  } else if (c == '\b') {
    if (_rx.length()) _rx.remove(_rx.length() - 1);
  } else {
    // Guard against runaway input; ELM327 commands are short.
    if (_rx.length() < 64) _rx += c;
  }
}

#endif // ENABLE_WIFI_ELM
