#pragma once

#include <stdint.h>

namespace tide {

enum MeasurementRecordFlag : uint16_t {
  RECORD_BATTERY_VALID = 1U << 0,
  RECORD_SOLAR_VALID = 1U << 1,
  RECORD_SYSTEM_VALID = 1U << 2,
  RECORD_DISTANCE_VALID = 1U << 3,
  RECORD_SHT40_VALID = 1U << 4,
  RECORD_TIME_VALID = 1U << 5,
};

// This packed layout is persisted verbatim in LittleFS. Do not change it
// without bumping the queue/record schemas and implementing migration.
struct __attribute__((packed)) MeasurementRecord {
  uint32_t magic;
  uint16_t schemaVersion;
  uint16_t flags;
  uint32_t sequence;
  uint64_t timestampMs;
  float batteryVoltage;
  float solarVoltage;
  float systemVoltage;
  float systemCurrent;
  uint16_t distanceMm;
  int32_t waterLevelMm;
  float temperatureC;
  float humidityPercent;
  float distanceMadMm;
  uint32_t acquisitionDurationMs;
  uint8_t acquiredSamples;
  uint8_t usedSamples;
  uint8_t outlierSamples;
  uint8_t quality;
  uint32_t checksum;
};

static_assert(sizeof(MeasurementRecord) == 66,
              "MeasurementRecord layout changed unexpectedly");

}  // namespace tide
