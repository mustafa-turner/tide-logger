#pragma once

#include <stdint.h>

namespace tide {

enum class TelemetryDestination : uint16_t {
  BLYNK = 1U << 0,
  MQTT = 1U << 1,
};

constexpr uint16_t TELEMETRY_DELIVERY_MASK =
    static_cast<uint16_t>(TelemetryDestination::BLYNK) |
    static_cast<uint16_t>(TelemetryDestination::MQTT);

constexpr uint16_t telemetryDestinationBit(
    TelemetryDestination destination) {
  return static_cast<uint16_t>(destination);
}

constexpr bool telemetrySequenceAfter(uint32_t sequence, uint32_t cursor) {
  return cursor == 0 || static_cast<int32_t>(sequence - cursor) > 0;
}

constexpr bool telemetrySequenceDelivered(uint32_t sequence,
                                          uint32_t cursor) {
  return cursor != 0 && !telemetrySequenceAfter(sequence, cursor);
}

constexpr uint32_t telemetryPendingOffset(uint32_t headSequence,
                                          uint32_t cursor) {
  return cursor - headSequence + (cursor < headSequence ? 0U : 1U);
}

enum class DeliveryAcknowledgeResult : uint8_t {
  ERROR,
  RECORDED,
  RECORD_COMPLETE,
};

}  // namespace tide
