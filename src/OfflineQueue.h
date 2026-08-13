#pragma once

#include <Arduino.h>
#include "MeasurementRecord.h"
#include "TelemetryDeliveryState.h"

namespace tide {

constexpr uint32_t OFFLINE_QUEUE_CAPACITY = 28UL * 24UL * 12UL;

class OfflineQueue {
 public:
  bool begin();
  bool enqueue(MeasurementRecord &record);
  bool peek(MeasurementRecord &record);
  bool pop(uint32_t expectedSequence);
  bool peekPendingDelivery(TelemetryDestination destination,
                           MeasurementRecord &record, bool &hasPending);
  DeliveryAcknowledgeResult acknowledgeDelivery(
      uint32_t expectedSequence, TelemetryDestination destination,
      uint16_t requiredMask);
  bool reconcileDeliveries(uint16_t requiredMask, uint32_t &removedRecords);
  bool clear();
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

  struct __attribute__((packed)) DeliveryHeader {
    uint32_t magic;
    uint16_t schemaVersion;
    uint16_t reserved;
    uint32_t generation;
    uint32_t blynkCursor;
    uint32_t mqttCursor;
    uint32_t checksum;
  };
  static_assert(sizeof(DeliveryHeader) == 24,
                "DeliveryHeader layout changed unexpectedly");

  bool initializeFile();
  bool initializeDeliveryState();
  bool loadHeader();
  bool loadDeliveryState();
  bool saveHeader(QueueHeader nextHeader);
  bool saveDeliveryState(DeliveryHeader nextHeader);
  bool deliveryHeaderIsValid(const DeliveryHeader &header) const;
  uint32_t deliveryCursor(TelemetryDestination destination) const;
  bool sequenceDeliveredToRequired(uint32_t sequence,
                                   const DeliveryHeader &delivery,
                                   uint16_t requiredMask) const;
  bool trimDeliveredRecords(uint16_t requiredMask,
                            uint32_t &removedRecords);
  bool readRecord(uint32_t index, MeasurementRecord &record);
  bool writeRecord(uint32_t index, const MeasurementRecord &record);
  bool clearRecord(uint32_t index);
  bool headerIsValid(const QueueHeader &header) const;
  static uint32_t checksumBytes(const void *data, size_t length);
  static uint32_t recordOffset(uint32_t index);
  static uint32_t deliveryHeaderOffset(uint8_t index);

  QueueHeader header_ = {};
  DeliveryHeader delivery_ = {};
  uint8_t activeHeaderIndex_ = 0;
  uint8_t activeDeliveryHeaderIndex_ = 0;
  bool ready_ = false;
};

}  // namespace tide
