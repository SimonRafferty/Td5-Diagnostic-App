/*
 * wifi_server.h - WiFi SoftAP + TCP server that carries the ELM327 session.
 *
 * The phone joins the ESP32's access point and opens a TCP socket to
 * 192.168.0.10:35000 (Torque's WiFi OBD default). Bytes are buffered until a
 * carriage return, then the assembled command line is handed to Elm327 and the
 * framed reply is written straight back.
 *
 * A single client is supported (ELM327 is a single-session device). A new
 * connection replaces any stale one.
 */

#ifndef TD5_TORQUE_WIFI_SERVER_H
#define TD5_TORQUE_WIFI_SERVER_H

#include <Arduino.h>
#include <WiFi.h>
#include "elm327.h"

class WifiElmServer {
public:
  WifiElmServer(Elm327& elm, uint16_t port) : _elm(elm), _server(port), _port(port) {}

  // Start the SoftAP and the TCP server. Returns true on success.
  bool begin();

  // Pump the socket: accept/replace client, read bytes, dispatch complete lines.
  void poll();

  bool hasClient() const { return _hasClient; }

private:
  Elm327&    _elm;
  WiFiServer _server;
  WiFiClient _client;
  uint16_t   _port;
  bool       _hasClient = false;
  String     _rx;          // accumulates bytes until CR

  void processByte(char c);
};

#endif // TD5_TORQUE_WIFI_SERVER_H
