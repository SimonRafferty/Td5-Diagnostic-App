/*
 * ble_server.cpp - BLE ELM327 transport (NimBLE 2.x). See ble_server.h.
 *
 * Commands are answered SYNCHRONOUSLY in the write callback (onRxBytes), the
 * way the proven ESP32-S3 emulators do it - no ring buffer, no main-loop poll.
 */

#include "ble_server.h"
#if ENABLE_BLE_ELM

// De-facto BLE ELM327 UUIDs (OBDLink CX / generic clone layout).
static const char* SVC_UUID    = "FFF0";
static const char* NOTIFY_UUID = "FFF1";
static const char* WRITE_UUID  = "FFF2";

// ---------------------------------------------------------------------------
// NimBLE callback shims -> forward into the BleElmServer instance
// ---------------------------------------------------------------------------
class BleWriteCallbacks : public NimBLECharacteristicCallbacks {
public:
  explicit BleWriteCallbacks(BleElmServer* s) : _s(s) {}
  void onWrite(NimBLECharacteristic* c, NimBLEConnInfo& /*connInfo*/) override {
    NimBLEAttValue v = c->getValue();
    if (v.length()) _s->onRxBytes(v.data(), v.length());
  }
private:
  BleElmServer* _s;
};

class BleServerCallbacks : public NimBLEServerCallbacks {
public:
  explicit BleServerCallbacks(BleElmServer* s) : _s(s) {}
  void onConnect(NimBLEServer* /*p*/, NimBLEConnInfo& ci) override { _s->onConnectEvt(ci.getConnHandle()); }
  void onDisconnect(NimBLEServer* /*p*/, NimBLEConnInfo& ci, int /*reason*/) override { _s->onDisconnectEvt(ci.getConnHandle()); }
  void onMTUChange(uint16_t mtu, NimBLEConnInfo& /*ci*/) override { _s->onMtu(mtu); }
private:
  BleElmServer* _s;
};

// ---------------------------------------------------------------------------

bool BleElmServer::begin() {
  NimBLEDevice::init(BLE_DEVICE_NAME);
  NimBLEDevice::setMTU(517);                 // allow the phone to negotiate up

  _server = NimBLEDevice::createServer();
  _server->setCallbacks(new BleServerCallbacks(this));

  NimBLEService* svc = _server->createService(SVC_UUID);
  _notify = svc->createCharacteristic(NOTIFY_UUID, NIMBLE_PROPERTY::NOTIFY);
  NimBLECharacteristic* wr = svc->createCharacteristic(
      WRITE_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR);
  wr->setCallbacks(new BleWriteCallbacks(this));
  svc->start();

  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->addServiceUUID(SVC_UUID);             // some apps filter by service UUID
  adv->setName(BLE_DEVICE_NAME);
  adv->enableScanResponse(true);
  NimBLEDevice::startAdvertising();

#if DEBUG_SERIAL
  Serial.printf("[BLE] Advertising as \"%s\"  svc FFF0 / notify FFF1 / write FFF2\n",
                BLE_DEVICE_NAME);
#endif
  return true;
}

// --- BLE-task context: receive, assemble, ANSWER INLINE --------------------

void BleElmServer::onRxBytes(const uint8_t* data, size_t len) {
  if (!_connected) return;
  // The first real command claims the session for BLE. A transient discovery
  // connection never writes, so it won't lock out WiFi. If WiFi already won this
  // session, drop this central (idempotent once BLE owns).
  if (!_arb.claim(T_BLE)) {
    if (_server && _curConn != 0xFFFF) _server->disconnect(_curConn);
    return;
  }
  for (size_t i = 0; i < len; i++) {
    char ch = (char)data[i];
    if (ch == '\r') {
      String cmd = _line;
      _line = "";
      String resp = _elm.handleLine(cmd);      // synchronous: reads buffered data only
#if DEBUG_SERIAL
      {
        String vis = resp;
        vis.replace("\r", "\\r");
        vis.replace("\n", "\\n");
        Serial.printf("[BLE] <%s> -> %s\n", cmd.c_str(), vis.c_str());
      }
#endif
      notifyChunked(resp);                     // reply BEFORE the next byte/line is handled
    } else if (ch == '\n') {
      // ignore stray line feeds
    } else if (ch == '\b') {
      if (_line.length()) _line.remove(_line.length() - 1);
    } else {
      if (_line.length() < 128) _line += ch;   // guard against runaway input
    }
  }
}

void BleElmServer::onConnectEvt(uint16_t connHandle) {
  // Single-session model: NimBLE stops advertising automatically on connect, so
  // only one central is ever attached. We do NOT restart advertising here and do
  // NOT force-disconnect anything - phones often open a transient second GATT
  // connection during discovery, and interfering with it breaks the real session.
  // To switch apps, disconnect/force-stop the first; onDisconnect re-advertises.
  _curConn   = connHandle;
  _mtu       = 23;
  _line      = "";
  _elm.reset();                               // fresh ELM327 state per session
  _connected = true;

  // Demand a FAST, low-latency connection (7.5-15 ms interval, no slave latency,
  // 4 s supervision). Diff of a WORKING app (EOBD-Facile) vs Car Scanner showed
  // identical requests/replies - the only difference is Car Scanner sets ATST00
  // (near-zero timeout) and reads almost immediately, while the phone's default
  // slow interval makes our reply cross BLE ~2 intervals late (60-100 ms). That
  // overruns Car Scanner's read window so it pairs each reply to the NEXT request
  // (alternating "all sensors" values; single-graph hides it). Forcing a tight
  // interval from our side gets the reply back inside that window regardless of
  // the app's timeout.
  if (_server) _server->updateConnParams(connHandle, 6, 12, 0, 400);
#if DEBUG_SERIAL
  Serial.printf("[BLE] central connected (handle %u); requested fast conn params\n", connHandle);
#endif
}

void BleElmServer::onDisconnectEvt(uint16_t connHandle) {
  _connected = false;
  _curConn   = 0xFFFF;
  if (!_disableAutoReAdvertise) NimBLEDevice::startAdvertising();   // become connectable again
#if DEBUG_SERIAL
  Serial.printf("[BLE] central disconnected (handle %u)%s\n",
                connHandle, _disableAutoReAdvertise ? "" : "; advertising");
#endif
}

void BleElmServer::onMtu(uint16_t mtu) {
  _mtu = mtu;
#if DEBUG_SERIAL
  Serial.printf("[BLE] MTU = %u\n", mtu);
#endif
}

// --- Main-task context -----------------------------------------------------

void BleElmServer::poll() {
  // Nothing to do: replies are sent inline from onRxBytes(). Kept so the main
  // loop's call site is unchanged.
}

// Main-task context: called once WiFi has won the session. Stop advertising, drop
// any attached central, and latch _disableAutoReAdvertise so onDisconnect doesn't
// resurrect it. Idempotent - the main loop calls this every pass while WiFi owns.
void BleElmServer::stop() {
  if (_disableAutoReAdvertise) return;
  _disableAutoReAdvertise = true;
  NimBLEDevice::stopAdvertising();
  if (_server && _curConn != 0xFFFF) _server->disconnect(_curConn);
#if DEBUG_SERIAL
  Serial.println("[BLE] stopped (WiFi won the session)");
#endif
}

void BleElmServer::notifyChunked(const String& s) {
  if (!_notify || !_connected) return;

  uint16_t mtu = _mtu;                         // snapshot (may change on BLE task)
  size_t chunk = (mtu > 3) ? (size_t)(mtu - 3) : 20;
  if (chunk < 20) chunk = 20;                  // never below the safe default

  size_t len = s.length();
  const char* p = s.c_str();
  size_t off = 0;
  while (off < len && _connected) {
    size_t n = len - off;
    if (n > chunk) n = chunk;
    _notify->setValue((const uint8_t*)(p + off), n);
    _notify->notify();                         // single shot per chunk, like the reference
    off += n;
  }
}

#endif // ENABLE_BLE_ELM
