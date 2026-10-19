// BLE GATT server (NimBLE). Commands arrive on the BLE host task and are queued;
// tick() runs them in the network task, so BLE and HTTP share one code path.
#pragma once

namespace ble {
void begin();
void tick();   // network task only
}
