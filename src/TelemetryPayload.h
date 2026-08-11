#pragma once

#include <stddef.h>
#include <stdint.h>

#include "MeasurementRecord.h"

namespace tide {

constexpr size_t TELEMETRY_PAYLOAD_CAPACITY = 1024;
constexpr size_t TELEMETRY_DEVICE_ID_CAPACITY = 48;
constexpr size_t TELEMETRY_TOPIC_CAPACITY = 96;
constexpr size_t TELEMETRY_CLIENT_ID_CAPACITY = 80;

static_assert(TELEMETRY_PAYLOAD_CAPACITY >= 768,
              "MQTT JSON buffer is too small for the v1 payload");

struct TelemetryPayloadContext {
  const char *deviceId;
  uint32_t pendingRecords;
  uint32_t droppedRecords;
  const char *firmwareVersion;
  int32_t wifiRssiDbm;
  uint32_t uptimeSec;
};

// Canonicalizes to lowercase ASCII letters/digits separated by single dashes;
// leading/trailing separators are removed. Returns false instead of truncating.
bool sanitizeTelemetryIdentifier(const char *input, char *output,
                                 size_t outputSize);
bool buildTelemetryTopic(const char *sanitizedDeviceId, char *output,
                         size_t outputSize);
bool buildTelemetryClientId(const char *sanitizedDeviceId,
                            uint64_t chipId, char *output,
                            size_t outputSize);

// Writes a null-terminated JSON object. Optional values are omitted when their
// record validity bit is absent or the stored floating-point value is invalid.
bool serializeTelemetryPayload(const MeasurementRecord &record,
                               const TelemetryPayloadContext &context,
                               char *output, size_t outputSize,
                               size_t &written);

}  // namespace tide
