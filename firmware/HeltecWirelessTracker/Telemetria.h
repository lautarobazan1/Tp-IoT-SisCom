#pragma once

#include <stddef.h>
#include <stdint.h>

// Copia identica en ambos sketches para abrirlos directamente en Arduino IDE.
// Formato binario explicito: no depende del padding ni del tamanio de float.
namespace Telemetria {
constexpr size_t PACKET_SIZE = 19;
constexpr uint8_t SENSOR_VALID = 0x01;
constexpr uint8_t BATTERY_VALID = 0x02;

struct Sample {
  uint8_t nodeId;
  uint8_t flags;
  uint32_t sequence;
  int16_t temperatureCentiC;
  uint16_t humidityCentiPct;
  uint32_t pressurePa;
  uint16_t batteryMv;
};

inline void write16(uint8_t* out, uint16_t value) {
  out[0] = static_cast<uint8_t>(value);
  out[1] = static_cast<uint8_t>(value >> 8);
}

inline uint16_t read16(const uint8_t* in) {
  return static_cast<uint16_t>(in[0]) |
         (static_cast<uint16_t>(in[1]) << 8);
}

inline void write32(uint8_t* out, uint32_t value) {
  for (uint8_t i = 0; i < 4; ++i) {
    out[i] = static_cast<uint8_t>(value >> (8 * i));
  }
}

inline uint32_t read32(const uint8_t* in) {
  uint32_t value = 0;
  for (uint8_t i = 0; i < 4; ++i) {
    value |= static_cast<uint32_t>(in[i]) << (8 * i);
  }
  return value;
}

inline bool valid(const Sample& s) {
  if (s.nodeId == 0 || (s.flags & ~(SENSOR_VALID | BATTERY_VALID)) != 0) {
    return false;
  }
  if ((s.flags & SENSOR_VALID) &&
      (s.temperatureCentiC < -4000 || s.temperatureCentiC > 8500 ||
       s.humidityCentiPct > 10000 || s.pressurePa < 30000 ||
       s.pressurePa > 110000)) {
    return false;
  }
  if ((s.flags & BATTERY_VALID) && (s.batteryMv < 2000 || s.batteryMv > 4500)) {
    return false;
  }
  return true;
}

inline void encode(const Sample& s, uint8_t* out) {
  out[0] = 'S';
  out[1] = 'C';
  out[2] = 1;  // Version del protocolo.
  out[3] = s.nodeId;
  out[4] = s.flags;
  write32(out + 5, s.sequence);
  write16(out + 9, static_cast<uint16_t>(s.temperatureCentiC));
  write16(out + 11, s.humidityCentiPct);
  write32(out + 13, s.pressurePa);
  write16(out + 17, s.batteryMv);
}

inline bool decode(const uint8_t* data, size_t size, Sample& out) {
  if (size != PACKET_SIZE || data[0] != 'S' || data[1] != 'C' || data[2] != 1) {
    return false;
  }
  Sample s = {};
  s.nodeId = data[3];
  s.flags = data[4];
  s.sequence = read32(data + 5);
  const uint16_t rawTemperature = read16(data + 9);
  s.temperatureCentiC = static_cast<int16_t>(
      rawTemperature < 0x8000 ? static_cast<int32_t>(rawTemperature)
                             : static_cast<int32_t>(rawTemperature) - 65536L);
  s.humidityCentiPct = read16(data + 11);
  s.pressurePa = read32(data + 13);
  s.batteryMv = read16(data + 17);
  if (!valid(s)) {
    return false;
  }
  out = s;
  return true;
}

// Aproximacion por tension EN REPOSO para una Li-ion de una celda.
// No es una medicion de capacidad; carga, temperatura y consumo la afectan.
inline uint8_t batteryPercent(uint16_t mv) {
  const uint16_t voltages[] = {3300, 3500, 3600, 3700, 3750, 3800,
                               3850, 3900, 4000, 4100, 4200};
  const uint8_t percentages[] = {0, 5, 10, 20, 30, 40, 50, 60, 75, 90, 100};
  if (mv <= voltages[0]) return 0;
  for (uint8_t i = 1; i < sizeof(percentages); ++i) {
    if (mv <= voltages[i]) {
      return percentages[i - 1] +
             static_cast<uint32_t>(mv - voltages[i - 1]) *
                 (percentages[i] - percentages[i - 1]) /
                 (voltages[i] - voltages[i - 1]);
    }
  }
  return 100;
}
}
