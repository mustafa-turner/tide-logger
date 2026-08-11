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

// Schema v2 packs acquired and inlier counts as two 12-bit integers into the
// three legacy uint8_t sample fields. This preserves the durable 66-byte queue
// layout while allowing a full timed acquisition to exceed 255 samples.
inline void setMeasurementSampleCounts(MeasurementRecord &record,
                                       uint16_t acquired,
                                       uint16_t inliers) {
  record.schemaVersion = 2;
  record.acquiredSamples = static_cast<uint8_t>(acquired & 0xFFU);
  record.usedSamples = static_cast<uint8_t>(((acquired >> 8U) & 0x0FU) |
                                             ((inliers & 0x0FU) << 4U));
  record.outlierSamples = static_cast<uint8_t>((inliers >> 4U) & 0xFFU);
}

inline uint16_t measurementAcquiredSamples(const MeasurementRecord &record) {
  if (record.schemaVersion < 2) return record.acquiredSamples;
  return static_cast<uint16_t>(record.acquiredSamples) |
         (static_cast<uint16_t>(record.usedSamples & 0x0FU) << 8U);
}

inline uint16_t measurementInlierSamples(const MeasurementRecord &record) {
  if (record.schemaVersion < 2) {
    return record.acquiredSamples >= record.outlierSamples
               ? record.acquiredSamples - record.outlierSamples
               : 0;
  }
  return static_cast<uint16_t>((record.usedSamples >> 4U) & 0x0FU) |
         (static_cast<uint16_t>(record.outlierSamples) << 4U);
}

inline uint16_t measurementUsedSamples(const MeasurementRecord &record) {
  if (record.schemaVersion < 2) return record.usedSamples;
  const uint16_t inliers = measurementInlierSamples(record);
  return inliers - 2U * (inliers / 10U);
}

inline uint16_t measurementOutlierSamples(const MeasurementRecord &record) {
  const uint16_t acquired = measurementAcquiredSamples(record);
  const uint16_t inliers = measurementInlierSamples(record);
  return acquired >= inliers ? acquired - inliers : 0;
}

}  // namespace tide
