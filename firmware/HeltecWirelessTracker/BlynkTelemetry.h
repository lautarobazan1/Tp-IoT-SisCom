#pragma once

#include "Telemetria.h"

namespace BlynkTelemetry {
void begin();
void enqueue(const Telemetria::Sample& sample, float rssi, uint32_t receivedMs);
}
