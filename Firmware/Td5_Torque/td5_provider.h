/*
 * td5_provider.h - PHASE 2 data source: the real Td5 ECU over K-Line.
 *
 * Wraps the lifted Td5Comm library. Connects/authenticates, round-robin polls
 * the ECU PIDs into the shared VehicleData snapshot, and implements DTC
 * read/clear via Td5Comm's getFaultCodes()/getFaultCode()/resetFaults(),
 * translating each fault's X-Y position to a description + OBD code via
 * td5_dtc_table.h.
 *
 * Only compiled when DATA_SOURCE_SIM == 0 (the whole file is guarded), so the
 * bench/BLE build is unaffected.
 */

#ifndef TD5_TORQUE_TD5_PROVIDER_H
#define TD5_TORQUE_TD5_PROVIDER_H

#include "config.h"
#if !DATA_SOURCE_SIM

#include "data_provider.h"
#include "td5comm.h"

class Td5Provider : public DataProvider {
public:
  bool begin() override;
  void poll() override;
  const VehicleData& data() const override { return _d; }
  int  readDTCs() override;
  bool clearDTCs() override;
  bool connected() const override { return _d.ecuConnected; }

private:
  Td5Comm     _td5;
  VehicleData _d {};

  // Demo fallback: if no real ECU has ever answered and we've waited a bit,
  // serve synthetic data so the app/BLE path can be exercised without a vehicle.
  // Once a real ECU connects, demo is disabled for the rest of the session.
  bool          _realEver     = false;
  bool          _demoActive   = false;
  bool          _demoFaultsCleared = false;
  void          fillDemo();

  uint8_t       _pollIdx      = 0;
  unsigned long _lastAttempt  = 0;
  unsigned long _lastKeepAlive = 0;
  unsigned long _lastGood     = 0;   // last successful K-line transaction
  unsigned long _connectMs    = 0;   // millis() when the ECU session began
  unsigned long _lastDtcPoll  = 0;   // last fault-code buffer refresh
  bool          _clearPending = false; // Clear-Codes requested by the app

  // The K-line runs in its OWN FreeRTOS task (own core) so blocking K-line I/O
  // never stalls the BLE/OBD side - the app always reads the buffer instantly.
  TaskHandle_t  _task = nullptr;
  static void   taskEntry(void* arg);
  void          runLoop();       // task loop: pollStep() forever
  void          pollStep();      // one iteration of connect/keep-alive/poll

  void pollNext();               // poll one live-data PID into the buffer
  bool refreshDtcBuffer();       // read fault codes into the buffer (paced)
  bool doClear();                // transmit clear-faults (paced)
  void buildDtc(int rawIndex, DtcEntry& e);
};

#endif // !DATA_SOURCE_SIM
#endif // TD5_TORQUE_TD5_PROVIDER_H
