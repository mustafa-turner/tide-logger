#pragma once

#include <Arduino.h>

namespace tide {

constexpr uint32_t OFFLINE_QUEUE_CAPACITY = 28UL * 24UL * 12UL;

enum MeasurementRecordFlag : uint16_t {
  RECORD_BATTERY_VALID = 1U << 0,
  RECORD_SOLAR_VALID = 1U << 1,
  RECORD_SYSTEM_VALID = 1U << 2,
  RECORD_DISTANCE_VALID = 1U << 3,
  RECORD_SHT40_VALID = 1U << 4,
  RECORD_TIME_VALID = 1U << 5,
};

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

class OfflineQueue {
 public:
  bool begin();
  bool enqueue(MeasurementRecord &record);
  bool peek(MeasurementRecord &record);
  bool pop(uint32_t expectedSequence);
  bool discardCorruptHead();
  bool updateNewestTimestamp(uint32_t expectedSequence,
                             uint64_t timestampMs);
  bool replaceNewest(const MeasurementRecord &record);

  bool ready() const { return ready_; }
  uint32_t count() const { return header_.count; }
  uint32_t droppedRecords() const { return header_.droppedRecords; }
  static constexpr uint32_t capacity() { return OFFLINE_QUEUE_CAPACITY; }

  static void finalizeRecord(MeasurementRecord &record);
  static bool isRecordValid(const MeasurementRecord &record);

 private:
  struct __attribute__((packed)) QueueHeader {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t reserved;
    uint32_t generation;
    uint32_t head;
    uint32_t count;
    uint32_t nextSequence;
    uint32_t droppedRecords;
    uint32_t checksum;
  };
  static_assert(sizeof(QueueHeader) == 32,
                "QueueHeader layout changed unexpectedly");

  bool initializeFile();
  bool loadHeader();
  bool saveHeader(QueueHeader nextHeader);
  bool readRecord(uint32_t index, MeasurementRecord &record);
  bool writeRecord(uint32_t index, const MeasurementRecord &record);
  bool clearRecord(uint32_t index);
  bool headerIsValid(const QueueHeader &header) const;
  static uint32_t checksumBytes(const void *data, size_t length);
  static uint32_t recordOffset(uint32_t index);

  QueueHeader header_ = {};
  uint8_t activeHeaderIndex_ = 0;
  bool ready_ = false;
};

static_assert(sizeof(MeasurementRecord) == 66,
              "MeasurementRecord layout changed unexpectedly");

}  // namespace tide
