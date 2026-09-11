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

  // Clear all stored fault codes. Returns true on success. (Mode 04.)
  virtual bool clearDTCs() = 0;

  // Is the underlying data source currently live?
  virtual bool connected() const = 0;
};

#endif // TD5_TORQUE_DATA_PROVIDER_H
