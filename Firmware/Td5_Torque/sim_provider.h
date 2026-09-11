/*
 * sim_provider.h - PHASE 1 data source: synthetic vehicle data + fake DTCs.
 *
 * Produces smoothly-varying, plausible values so Torque's gauges move, and a
 * small set of stored fault codes so Read/Clear can be exercised end to end.
 * No ECU or K-Line hardware required.
 */

#ifndef TD5_TORQUE_SIM_PROVIDER_H
#define TD5_TORQUE_SIM_PROVIDER_H

#include "data_provider.h"

class SimProvider : public DataProvider {
public:
  bool begin() override;
  void poll() override;
  const VehicleData& data() const override { return _d; }
  int  readDTCs() override;
  bool clearDTCs() override;
  bool connected() const override { return _d.ecuConnected; }

private:
  VehicleData _d {};
  bool        _faultsPresent = true;   // cleared by clearDTCs()
};

#endif // TD5_TORQUE_SIM_PROVIDER_H
