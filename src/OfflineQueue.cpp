#include "OfflineQueue.h"

#include <LittleFS.h>
#include <stddef.h>

namespace tide {
namespace {
constexpr char QUEUE_FILE_PATH[] = "/measurements.bin";
constexpr char DELIVERY_FILE_PATH[] = "/delivery.bin";
constexpr uint32_t QUEUE_HEADER_MAGIC = 0x54494445UL;     // TIDE
constexpr uint32_t DELIVERY_HEADER_MAGIC = 0x444C5652UL;  // DLVR
constexpr uint32_t RECORD_MAGIC = 0x574C564CUL;           // WLVL
constexpr uint16_t QUEUE_SCHEMA_VERSION = 1;
constexpr uint16_t DELIVERY_SCHEMA_VERSION = 1;
constexpr uint16_t RECORD_SCHEMA_VERSION = 2;
constexpr uint8_t HEADER_COPY_COUNT = 2;
}  // namespace

uint32_t OfflineQueue::checksumBytes(const void *data, size_t length) {
  const uint8_t *bytes = static_cast<const uint8_t *>(data);
  uint32_t hash = 2166136261UL;
  for (size_t i = 0; i < length; ++i) {
    hash ^= bytes[i];
    hash *= 16777619UL;
  }
  return hash;
}

void OfflineQueue::finalizeRecord(MeasurementRecord &record) {
  record.magic = RECORD_MAGIC;
  // Preserve schema v1 when an old queued record only needs a new checksum.
  // New records opt into v2 through setMeasurementSampleCounts().
  if (record.schemaVersion != 1 &&
      record.schemaVersion != RECORD_SCHEMA_VERSION) {
    record.schemaVersion = RECORD_SCHEMA_VERSION;
  }
  record.checksum = checksumBytes(&record, offsetof(MeasurementRecord, checksum));
}

bool OfflineQueue::isRecordValid(const MeasurementRecord &record) {
  return record.magic == RECORD_MAGIC &&
         (record.schemaVersion == 1 ||
          record.schemaVersion == RECORD_SCHEMA_VERSION) &&
         record.checksum ==
             checksumBytes(&record, offsetof(MeasurementRecord, checksum));
}

uint32_t OfflineQueue::recordOffset(uint32_t index) {
  return HEADER_COPY_COUNT * sizeof(QueueHeader) +
         index * sizeof(MeasurementRecord);
}

uint32_t OfflineQueue::deliveryHeaderOffset(uint8_t index) {
  return index * sizeof(DeliveryHeader);
}

bool OfflineQueue::headerIsValid(const QueueHeader &header) const {
  return header.magic == QUEUE_HEADER_MAGIC &&
         header.schemaVersion == QUEUE_SCHEMA_VERSION &&
         header.head < OFFLINE_QUEUE_CAPACITY &&
         header.count <= OFFLINE_QUEUE_CAPACITY &&
         header.nextSequence != 0 &&
         header.checksum ==
             checksumBytes(&header, offsetof(QueueHeader, checksum));
}

bool OfflineQueue::initializeFile() {
  File file = LittleFS.open(QUEUE_FILE_PATH, FILE_WRITE);
  if (!file) {
    return false;
  }

  QueueHeader first = {};
  first.magic = QUEUE_HEADER_MAGIC;
  first.schemaVersion = QUEUE_SCHEMA_VERSION;
  first.generation = 1;
  first.nextSequence = 1;
  first.checksum = checksumBytes(&first, offsetof(QueueHeader, checksum));

  QueueHeader second = first;
  second.generation = 0;
  second.checksum = checksumBytes(&second, offsetof(QueueHeader, checksum));

  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&first), sizeof(first)) ==
          sizeof(first) &&
      file.write(reinterpret_cast<const uint8_t *>(&second), sizeof(second)) ==
          sizeof(second);
  file.flush();
  file.close();
  if (!written) {
    return false;
  }

  header_ = first;
  activeHeaderIndex_ = 0;
  return true;
}

bool OfflineQueue::initializeDeliveryState() {
  File file = LittleFS.open(DELIVERY_FILE_PATH, FILE_WRITE);
  if (!file) {
    return false;
  }

  DeliveryHeader first = {};
  first.magic = DELIVERY_HEADER_MAGIC;
  first.schemaVersion = DELIVERY_SCHEMA_VERSION;
  first.generation = 1;
  first.checksum =
      checksumBytes(&first, offsetof(DeliveryHeader, checksum));

  DeliveryHeader second = first;
  second.generation = 0;
  second.checksum =
      checksumBytes(&second, offsetof(DeliveryHeader, checksum));

  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&first), sizeof(first)) ==
          sizeof(first) &&
      file.write(reinterpret_cast<const uint8_t *>(&second), sizeof(second)) ==
          sizeof(second);
  file.flush();
  file.close();
  if (!written) {
    return false;
  }

  delivery_ = first;
  activeDeliveryHeaderIndex_ = 0;
  return true;
}

bool OfflineQueue::loadHeader() {
  File file = LittleFS.open(QUEUE_FILE_PATH, FILE_READ);
  if (!file) {
    return false;
  }

  QueueHeader copies[HEADER_COPY_COUNT] = {};
  bool valid[HEADER_COPY_COUNT] = {};
  for (uint8_t i = 0; i < HEADER_COPY_COUNT; ++i) {
    if (file.read(reinterpret_cast<uint8_t *>(&copies[i]),
                  sizeof(QueueHeader)) == sizeof(QueueHeader)) {
      valid[i] = headerIsValid(copies[i]);
    }
  }
  file.close();

  if (!valid[0] && !valid[1]) {
    return false;
  }

  if (valid[0] && valid[1]) {
    activeHeaderIndex_ =
        static_cast<int32_t>(copies[1].generation - copies[0].generation) > 0
            ? 1
            : 0;
  } else {
    activeHeaderIndex_ = valid[1] ? 1 : 0;
  }
  header_ = copies[activeHeaderIndex_];
  return true;
}

bool OfflineQueue::loadDeliveryState() {
  File file = LittleFS.open(DELIVERY_FILE_PATH, FILE_READ);
  if (!file) {
    return false;
  }

  DeliveryHeader copies[HEADER_COPY_COUNT] = {};
  bool valid[HEADER_COPY_COUNT] = {};
  for (uint8_t i = 0; i < HEADER_COPY_COUNT; ++i) {
    if (file.read(reinterpret_cast<uint8_t *>(&copies[i]),
                  sizeof(DeliveryHeader)) == sizeof(DeliveryHeader)) {
      valid[i] = deliveryHeaderIsValid(copies[i]);
    }
  }
  file.close();

  if (!valid[0] && !valid[1]) {
    return false;
  }

  if (valid[0] && valid[1]) {
    activeDeliveryHeaderIndex_ =
        static_cast<int32_t>(copies[1].generation - copies[0].generation) > 0
            ? 1
            : 0;
  } else {
    activeDeliveryHeaderIndex_ = valid[1] ? 1 : 0;
  }
  delivery_ = copies[activeDeliveryHeaderIndex_];
  return true;
}

bool OfflineQueue::saveHeader(QueueHeader nextHeader) {
  nextHeader.magic = QUEUE_HEADER_MAGIC;
  nextHeader.schemaVersion = QUEUE_SCHEMA_VERSION;
  nextHeader.generation = header_.generation + 1;
  nextHeader.checksum =
      checksumBytes(&nextHeader, offsetof(QueueHeader, checksum));

  const uint8_t targetHeaderIndex = 1U - activeHeaderIndex_;
  File file = LittleFS.open(QUEUE_FILE_PATH, "r+");
  if (!file ||
      !file.seek(targetHeaderIndex * sizeof(QueueHeader), SeekSet)) {
    if (file) file.close();
    return false;
  }

  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&nextHeader),
                 sizeof(nextHeader)) == sizeof(nextHeader);
  file.flush();
  file.close();
  if (!written) {
    return false;
  }

  header_ = nextHeader;
  activeHeaderIndex_ = targetHeaderIndex;
  return true;
}

bool OfflineQueue::saveDeliveryState(DeliveryHeader nextHeader) {
  nextHeader.magic = DELIVERY_HEADER_MAGIC;
  nextHeader.schemaVersion = DELIVERY_SCHEMA_VERSION;
  nextHeader.generation = delivery_.generation + 1;
  nextHeader.checksum =
      checksumBytes(&nextHeader, offsetof(DeliveryHeader, checksum));

  const uint8_t targetHeaderIndex = 1U - activeDeliveryHeaderIndex_;
  File file = LittleFS.open(DELIVERY_FILE_PATH, "r+");
  if (!file ||
      !file.seek(deliveryHeaderOffset(targetHeaderIndex), SeekSet)) {
    if (file) file.close();
    return false;
  }

  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&nextHeader),
                 sizeof(nextHeader)) == sizeof(nextHeader);
  file.flush();
  file.close();
  if (!written) {
    return false;
  }

  delivery_ = nextHeader;
  activeDeliveryHeaderIndex_ = targetHeaderIndex;
  return true;
}

bool OfflineQueue::readRecord(uint32_t index, MeasurementRecord &record) {
  if (index >= OFFLINE_QUEUE_CAPACITY) {
    return false;
  }
  File file = LittleFS.open(QUEUE_FILE_PATH, FILE_READ);
  if (!file || !file.seek(recordOffset(index), SeekSet)) {
    if (file) file.close();
    return false;
  }
  const bool read =
      file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) ==
      sizeof(record);
  file.close();
  return read && isRecordValid(record);
}

bool OfflineQueue::writeRecord(uint32_t index,
                               const MeasurementRecord &record) {
  if (index >= OFFLINE_QUEUE_CAPACITY) {
    return false;
  }
  File file = LittleFS.open(QUEUE_FILE_PATH, "r+");
  if (!file || !file.seek(recordOffset(index), SeekSet)) {
    if (file) file.close();
    return false;
  }
  const bool written =
      file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) ==
      sizeof(record);
  file.flush();
  file.close();
  return written;
}

bool OfflineQueue::clearRecord(uint32_t index) {
  MeasurementRecord empty = {};
  return writeRecord(index, empty);
}

bool OfflineQueue::begin() {
  ready_ = false;
  if (!LittleFS.begin(true)) {
    return false;
  }
  if (LittleFS.totalBytes() < recordOffset(OFFLINE_QUEUE_CAPACITY) +
                                  HEADER_COPY_COUNT * sizeof(DeliveryHeader)) {
    return false;
  }

  bool queueInitialized = false;
  if (!LittleFS.exists(QUEUE_FILE_PATH)) {
    if (!initializeFile()) {
      return false;
    }
    queueInitialized = true;
  } else if (!loadHeader()) {
    LittleFS.remove(QUEUE_FILE_PATH);
    if (!initializeFile()) {
      return false;
    }
    queueInitialized = true;
  }

  // The cursor file is independent from the v1 record layout. A newly created
  // queue must never inherit cursors from an older queue file.
  if (queueInitialized) {
    LittleFS.remove(DELIVERY_FILE_PATH);
  }
  if ((queueInitialized || !loadDeliveryState()) &&
      !initializeDeliveryState()) {
    return false;
  }

  ready_ = true;
  return true;
}

bool OfflineQueue::enqueue(MeasurementRecord &record) {
  if (!ready_) {
    return false;
  }

  if (header_.count == OFFLINE_QUEUE_CAPACITY) {
    QueueHeader reduced = header_;
    reduced.head = (reduced.head + 1) % OFFLINE_QUEUE_CAPACITY;
    --reduced.count;
    ++reduced.droppedRecords;
    if (!saveHeader(reduced)) {
      return false;
    }
  }

  record.sequence = header_.nextSequence;
  finalizeRecord(record);
  const uint32_t tail =
      (header_.head + header_.count) % OFFLINE_QUEUE_CAPACITY;
  if (!writeRecord(tail, record)) {
    return false;
  }

  QueueHeader expanded = header_;
  ++expanded.count;
  ++expanded.nextSequence;
  if (expanded.nextSequence == 0) {
    expanded.nextSequence = 1;
  }
  return saveHeader(expanded);
}

bool OfflineQueue::peek(MeasurementRecord &record) {
  return ready_ && header_.count > 0 && readRecord(header_.head, record);
}

bool OfflineQueue::pop(uint32_t expectedSequence) {
  if (!ready_ || header_.count == 0) {
    return false;
  }

  MeasurementRecord record = {};
  if (!readRecord(header_.head, record) ||
      record.sequence != expectedSequence) {
    return false;
  }

  const uint32_t previousHead = header_.head;
  QueueHeader reduced = header_;
  reduced.head = (reduced.head + 1) % OFFLINE_QUEUE_CAPACITY;
  --reduced.count;
  if (!saveHeader(reduced)) {
    return false;
  }

  // Header sudah aman terlebih dahulu. Kegagalan mengosongkan slot hanya
  // meninggalkan data basi di luar rentang antrean dan tidak mengulang upload.
  clearRecord(previousHead);
  return true;
}

bool OfflineQueue::deliveryHeaderIsValid(
    const DeliveryHeader &header) const {
  return header.magic == DELIVERY_HEADER_MAGIC &&
         header.schemaVersion == DELIVERY_SCHEMA_VERSION &&
         header.checksum ==
             checksumBytes(&header, offsetof(DeliveryHeader, checksum));
}

uint32_t OfflineQueue::deliveryCursor(
    TelemetryDestination destination) const {
  return destination == TelemetryDestination::BLYNK ? delivery_.blynkCursor
                                                     : delivery_.mqttCursor;
}

bool OfflineQueue::peekPendingDelivery(TelemetryDestination destination,
                                       MeasurementRecord &record,
                                       bool &hasPending) {
  hasPending = false;
  if (!ready_ ||
      (telemetryDestinationBit(destination) & TELEMETRY_DELIVERY_MASK) == 0) {
    return false;
  }

  if (header_.count == 0) {
    return true;
  }

  const uint32_t cursor = deliveryCursor(destination);
  MeasurementRecord head = {};
  if (!readRecord(header_.head, head)) {
    return false;
  }
  if (telemetrySequenceAfter(head.sequence, cursor)) {
    record = head;
    hasPending = true;
    return true;
  }

  // Records are contiguous except that sequence zero is deliberately skipped.
  // Correct that one-value gap when this live range crosses UINT32_MAX -> 1.
  const uint32_t offset = telemetryPendingOffset(head.sequence, cursor);
  if (offset >= header_.count) {
    return true;
  }
  const uint32_t index =
      (header_.head + offset) % OFFLINE_QUEUE_CAPACITY;
  MeasurementRecord candidate = {};
  if (!readRecord(index, candidate) ||
      !telemetrySequenceAfter(candidate.sequence, cursor)) {
    return false;
  }
  record = candidate;
  hasPending = true;
  return true;
}

bool OfflineQueue::sequenceDeliveredToRequired(
    uint32_t sequence, const DeliveryHeader &delivery,
    uint16_t requiredMask) const {
  if (requiredMask == 0 ||
      (requiredMask & ~TELEMETRY_DELIVERY_MASK) != 0) {
    return false;
  }
  if ((requiredMask & telemetryDestinationBit(TelemetryDestination::BLYNK)) !=
          0 &&
      !telemetrySequenceDelivered(sequence, delivery.blynkCursor)) {
    return false;
  }
  if ((requiredMask & telemetryDestinationBit(TelemetryDestination::MQTT)) !=
          0 &&
      !telemetrySequenceDelivered(sequence, delivery.mqttCursor)) {
    return false;
  }
  return true;
}

bool OfflineQueue::trimDeliveredRecords(uint16_t requiredMask,
                                        uint32_t &removedRecords) {
  removedRecords = 0;
  while (header_.count > 0) {
    MeasurementRecord head = {};
    if (!readRecord(header_.head, head)) {
      return false;
    }
    if (!sequenceDeliveredToRequired(head.sequence, delivery_, requiredMask)) {
      return true;
    }
    if (!pop(head.sequence)) {
      return false;
    }
    ++removedRecords;
  }
  return true;
}

bool OfflineQueue::reconcileDeliveries(uint16_t requiredMask,
                                       uint32_t &removedRecords) {
  if (!ready_) {
    removedRecords = 0;
    return false;
  }
  return trimDeliveredRecords(requiredMask, removedRecords);
}

DeliveryAcknowledgeResult OfflineQueue::acknowledgeDelivery(
    uint32_t expectedSequence, TelemetryDestination destination,
    uint16_t requiredMask) {
  if (!ready_ || header_.count == 0 ||
      (requiredMask & ~TELEMETRY_DELIVERY_MASK) != 0 || requiredMask == 0) {
    return DeliveryAcknowledgeResult::ERROR;
  }

  const uint16_t destinationBit = telemetryDestinationBit(destination);
  if ((requiredMask & destinationBit) == 0) {
    return DeliveryAcknowledgeResult::ERROR;
  }

  MeasurementRecord pending = {};
  bool hasPending = false;
  if (!peekPendingDelivery(destination, pending, hasPending) || !hasPending ||
      pending.sequence != expectedSequence) {
    return DeliveryAcknowledgeResult::ERROR;
  }

  DeliveryHeader next = delivery_;
  if (destination == TelemetryDestination::BLYNK) {
    next.blynkCursor = expectedSequence;
  } else {
    next.mqttCursor = expectedSequence;
  }
  const bool recordComplete =
      sequenceDeliveredToRequired(expectedSequence, next, requiredMask);
  if (!saveDeliveryState(next)) {
    return DeliveryAcknowledgeResult::ERROR;
  }

  uint32_t removedRecords = 0;
  if (!trimDeliveredRecords(requiredMask, removedRecords)) {
    return DeliveryAcknowledgeResult::ERROR;
  }
  return recordComplete ? DeliveryAcknowledgeResult::RECORD_COMPLETE
                        : DeliveryAcknowledgeResult::RECORDED;
}

bool OfflineQueue::discardCorruptHead() {
  if (!ready_ || header_.count == 0) {
    return false;
  }

  const uint32_t previousHead = header_.head;
  QueueHeader reduced = header_;
  reduced.head = (reduced.head + 1) % OFFLINE_QUEUE_CAPACITY;
  --reduced.count;
  ++reduced.droppedRecords;
  if (!saveHeader(reduced)) {
    return false;
  }
  clearRecord(previousHead);
  return true;
}

bool OfflineQueue::updateNewestTimestamp(uint32_t expectedSequence,
                                         uint64_t timestampMs) {
  if (!ready_ || header_.count == 0 || timestampMs == 0) {
    return false;
  }

  const uint32_t newestIndex =
      (header_.head + header_.count - 1) % OFFLINE_QUEUE_CAPACITY;
  MeasurementRecord record = {};
  if (!readRecord(newestIndex, record) ||
      record.sequence != expectedSequence) {
    return false;
  }

  record.timestampMs = timestampMs;
  record.flags |= RECORD_TIME_VALID;
  finalizeRecord(record);
  return writeRecord(newestIndex, record);
}

bool OfflineQueue::replaceNewest(const MeasurementRecord &record) {
  if (!ready_ || header_.count == 0 || !isRecordValid(record)) {
    return false;
  }

  const uint32_t newestIndex =
      (header_.head + header_.count - 1) % OFFLINE_QUEUE_CAPACITY;
  MeasurementRecord stored = {};
  if (!readRecord(newestIndex, stored) ||
      stored.sequence != record.sequence) {
    return false;
  }
  return writeRecord(newestIndex, record);
}

}  // namespace tide
