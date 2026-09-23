/*
 * web_server.cpp - see web_server.h for the overview.
 */
#include "config.h"
#if ENABLE_WIFI_WEBAPP
#include "web_server.h"
#include "webapp_html.h"   // WEBAPP_HTML_GZ[] (gzipped app) + WEBAPP_HTML_GZ_LEN

// Captive-portal landing page. The restricted captive mini-browsers (Android/iOS)
// limit JavaScript, so they can't render the full SPA (it goes blank) - serve a small
// no-JS page that points the user at the app in their real browser instead.
static const char CAPTIVE_HTML[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content=\"width=device-width,initial-scale=1\">"
  "<title>Td5 Diagnostics</title><style>"
  "body{font-family:system-ui,sans-serif;background:#0f1115;color:#eee;text-align:center;padding:2.2em 1.1em;margin:0}"
  "h2{margin:.1em 0 .4em}p{opacity:.85;max-width:21em;margin:.5em auto;line-height:1.5}"
  "a.btn{display:inline-block;margin:1.1em 0 .3em;padding:.85em 1.9em;background:#2a9d6b;color:#fff;"
  "text-decoration:none;border-radius:10px;font-size:1.15em}"
  "code{background:#222;padding:.15em .5em;border-radius:5px;font-size:1.1em}"
  "</style></head><body>"
  "<h2>Td5 Diagnostics</h2>"
  "<p>You're connected to the dongle.</p>"
  "<a class=btn href=\"http://192.168.73.1/\">Open the app</a>"
  "<p>If this stays blank or the button does nothing, open your normal browser and go to <code>192.168.73.1</code></p>"
  "</body></html>";

// Case-insensitive Content-Length parse from the header block. -1 if absent.
static int parseContentLength(const String& headers) {
  String h = headers; h.toLowerCase();
  int i = h.indexOf("content-length:");
  if (i < 0) return -1;
  i += 15;
  while (i < (int)h.length() && h[i] == ' ') i++;
  int v = 0; bool any = false;
  while (i < (int)h.length() && h[i] >= '0' && h[i] <= '9') { v = v * 10 + (h[i] - '0'); i++; any = true; }
  return any ? v : -1;
}

bool WebAppServer::begin() {
  WiFi.mode(WIFI_AP);
  IPAddress apIp(ELM_AP_IP_0, ELM_AP_IP_1, ELM_AP_IP_2, ELM_AP_IP_3);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(apIp, apIp, subnet);

  const char* pass = (strlen(AP_PASS) > 0) ? AP_PASS : nullptr;
  bool ok = WiFi.softAP(AP_SSID, pass, AP_CHANNEL, 0 /*hidden*/, AP_MAX_CLIENTS);

#if ENABLE_CAPTIVE_PORTAL
  _dns.setErrorReplyCode(DNSReplyCode::NoError);
  _dns.start(53, "*", apIp);            // every lookup -> the dongle, triggers the captive sheet
#endif

  _http.begin();
  _http.setNoDelay(true);
  _started = true;

#if DEBUG_SERIAL
  Serial.printf("[WEB] SoftAP \"%s\" %s @ %s  (app: http://%s/)\n",
                AP_SSID, ok ? "up" : "FAILED",
                WiFi.softAPIP().toString().c_str(),
                WiFi.softAPIP().toString().c_str());
#endif
  return ok;
}

void WebAppServer::poll() {
  if (!_started) return;

#if ENABLE_CAPTIVE_PORTAL
  _dns.processNextRequest();            // cheap, returns immediately
#endif

  if (!_busy) {
    WiFiClient c = _http.available();
    if (!c) return;
    _client   = c;
    _client.setNoDelay(true);
    _busy     = true;
    _reqStart = millis();
    _req      = "";
  }
  serviceClient();
}

void WebAppServer::serviceClient() {
  if (!_client.connected()) { _client.stop(); _busy = false; _req = ""; return; }

  // Drain whatever is available this pass (bounded); do NOT block waiting for more.
  while (_client.available() && _req.length() < 4096) _req += (char)_client.read();

  int he = _req.indexOf("\r\n\r\n");
  if (he < 0) {                          // headers not complete yet
    if (millis() - _reqStart > 1500) { _client.stop(); _busy = false; _req = ""; }
    return;
  }

  int eol = _req.indexOf("\r\n");
  String reqLine = _req.substring(0, eol);
  int sp1 = reqLine.indexOf(' ');
  int sp2 = (sp1 >= 0) ? reqLine.indexOf(' ', sp1 + 1) : -1;
  String method = (sp1 > 0) ? reqLine.substring(0, sp1) : reqLine;
  String path   = (sp1 > 0 && sp2 > sp1) ? reqLine.substring(sp1 + 1, sp2) : String("/");

  String body = "";
  if (method == "POST") {
    int cl = parseContentLength(_req.substring(0, he));
    if (cl < 0) cl = 0;
    int have = (int)_req.length() - (he + 4);
    if (have < cl) {                     // body still arriving
      if (millis() - _reqStart > 1500) { _client.stop(); _busy = false; _req = ""; }
      return;
    }
    body = _req.substring(he + 4, he + 4 + cl);
  }

  handleRequest(method, path, body);
  _client.stop();
  _busy = false;
  _req  = "";
}

void WebAppServer::handleRequest(const String& method, const String& path, const String& body) {
  if (method == "POST" && path == "/elm") {
    if (!_arb.claim(T_WIFI)) {           // BLE already owns this session
      sendText(503, "Service Unavailable", "text/plain", "BUSY");
      return;
    }
    String cmd = body; cmd.trim();       // app sends the bare command; match how BLE strips CR
    String resp = _elm.handleLine(cmd);  // reply already ends with the '>' prompt
#if DEBUG_SERIAL
    { String vis = resp; vis.replace("\r", "\\r"); vis.replace("\n", "\\n");
      Serial.printf("[WEB] <%s> -> %s\n", cmd.c_str(), vis.c_str()); }
#endif
    sendText(200, "OK", "text/plain", resp);
    return;
  }

  if (method == "GET" && (path == "/" || path == "/index.html")) { sendApp(); return; }

#if ENABLE_CAPTIVE_PORTAL
  if (method == "GET") { sendCaptive(); return; }
#endif

  sendText(404, "Not Found", "text/plain", "404");
}

void WebAppServer::sendApp() {
  _client.print(F("HTTP/1.1 200 OK\r\n"));
  _client.print(F("Content-Type: text/html\r\n"));
  _client.print(F("Content-Encoding: gzip\r\n"));
  _client.printf("Content-Length: %u\r\n", (unsigned)WEBAPP_HTML_GZ_LEN);
  _client.print(F("Cache-Control: no-cache\r\n"));
  _client.print(F("Connection: close\r\n\r\n"));

  const size_t CHUNK = 512;
  uint8_t buf[CHUNK];
  size_t off = 0;
  while (off < WEBAPP_HTML_GZ_LEN) {
    size_t n = WEBAPP_HTML_GZ_LEN - off;
    if (n > CHUNK) n = CHUNK;
    memcpy_P(buf, WEBAPP_HTML_GZ + off, n);
    _client.write(buf, n);
    off += n;
  }
}

void WebAppServer::sendCaptive() {
  // Small no-JS landing page for the restricted captive browser (see CAPTIVE_HTML).
  // 200-with-HTML (not a redirect) still triggers the OS "sign in" sheet, and unlike
  // the JS SPA this actually renders in the captive mini-browser.
  size_t len = strlen_P(CAPTIVE_HTML);
  _client.print(F("HTTP/1.1 200 OK\r\n"));
  _client.print(F("Content-Type: text/html\r\n"));
  _client.printf("Content-Length: %u\r\n", (unsigned)len);
  _client.print(F("Connection: close\r\n\r\n"));
  const size_t CHUNK = 256;
  char buf[CHUNK];
  size_t off = 0;
  while (off < len) {
    size_t n = len - off; if (n > CHUNK) n = CHUNK;
    memcpy_P(buf, CAPTIVE_HTML + off, n);
    _client.write((const uint8_t*)buf, n);
    off += n;
  }
}

void WebAppServer::sendText(int code, const char* status, const char* ctype, const String& body) {
  _client.printf("HTTP/1.1 %d %s\r\n", code, status);
  _client.printf("Content-Type: %s\r\n", ctype);
  _client.printf("Content-Length: %u\r\n", (unsigned)body.length());
  _client.print(F("Connection: close\r\n\r\n"));
  _client.print(body);
}

void WebAppServer::shutdown() {
  if (!_started) return;
#if ENABLE_CAPTIVE_PORTAL
  _dns.stop();
#endif
  if (_busy) { _client.stop(); _busy = false; }
  _http.end();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  _started = false;
#if DEBUG_SERIAL
  Serial.println("[WEB] shut down (BLE won the session)");
#endif
}

#endif // ENABLE_WIFI_WEBAPP
