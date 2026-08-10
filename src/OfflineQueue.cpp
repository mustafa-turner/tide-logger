#include "OfflineQueue.h"

#include <LittleFS.h>
#include <stddef.h>

namespace tide {
namespace {
constexpr char QUEUE_FILE_PATH[] = "/measurements.bin";
constexpr uint32_t QUEUE_HEADER_MAGIC = 0x54494445UL;  // TIDE
constexpr uint32_t RECORD_MAGIC = 0x574C564CUL;        // WLVL
constexpr uint16_t QUEUE_SCHEMA_VERSION = 1;
constexpr uint16_t RECORD_SCHEMA_VERSION = 1;
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
  record.schemaVersion = RECORD_SCHEMA_VERSION;
  record.checksum = checksumBytes(&record, offsetof(MeasurementRecord, checksum));
}

bool OfflineQueue::isRecordValid(const MeasurementRecord &record) {
  return record.magic == RECORD_MAGIC &&
         record.schemaVersion == RECORD_SCHEMA_VERSION &&
         record.checksum ==
             checksumBytes(&record, offsetof(MeasurementRecord, checksum));
}

uint32_t OfflineQueue::recordOffset(uint32_t index) {
  return HEADER_COPY_COUNT * sizeof(QueueHeader) +
         index * sizeof(MeasurementRecord);
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
  if (LittleFS.totalBytes() < recordOffset(OFFLINE_QUEUE_CAPACITY)) {
    return false;
  }

  if (!LittleFS.exists(QUEUE_FILE_PATH)) {
    if (!initializeFile()) {
      return false;
    }
  } else if (!loadHeader()) {
    LittleFS.remove(QUEUE_FILE_PATH);
    if (!initializeFile()) {
      return false;
    }
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
