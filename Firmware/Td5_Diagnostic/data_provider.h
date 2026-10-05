/*
 * data_provider.h - Abstract source of vehicle data.
 *
 * This is THE seam of the whole project. Phase 1 (SimProvider) and Phase 2
 * (Td5Provider) both implement this interface; the ELM327/OBD/WiFi stack above
 * it never changes. Swapping phases is a one-line change in the sketch.
 */

#ifndef TD5_TORQUE_DATA_PROVIDER_H
#define TD5_TORQUE_DATA_PROVIDER_H

#include "vehicle_data.h"

// Outcome of a Clear-Codes (mode 04) request.
enum class ClearResult : uint8_t {
  Ok,            // ECU acknowledged the clear (71 DD)
  Rejected,      // ECU sent a negative response (7F 31 nrc)
  NoReply,       // request sent but no valid answer - the ECU may or may not have cleared
  NotConnected   // no ECU session, nothing was sent
};

class DataProvider {
public:
  virtual ~DataProvider() {}

  // Called once from setup(). Return true on success.
  virtual bool begin() = 0;

  // Called every loop() iteration. Refreshes live values in the snapshot.
  // Implementations must be non-blocking (or minimally blocking) so the WiFi
  // server stays responsive.
  virtual void poll() = 0;

  // Read-only access to the current snapshot.
  virtual const VehicleData& data() const = 0;

  // Refresh the DTC list in the snapshot. Returns the number of active codes,
  // or -1 on failure. Called on demand when the app requests mode 03.
  virtual int readDTCs() = 0;

  // Clear all stored fault codes (mode 04). May block briefly (a few hundred ms,
  // ~3.5 s worst case) while the ECU answers, so the result is the ECU's real
  // verdict rather than "request queued".
  virtual ClearResult clearDTCs() = 0;

  // ECU negative-response code from the last Rejected clear (0 if none).
  virtual uint8_t lastClearNrc() const { return 0; }

  // Is the underlying data source currently live?
  virtual bool connected() const = 0;
};

#endif // TD5_TORQUE_DATA_PROVIDER_H
