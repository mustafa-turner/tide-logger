#pragma once

#include "device_config.h"

enum class TelemetryBackend { BLYNK_ONLY, MQTT_ONLY, BOTH };

#define TIDE_BACKEND_ENUM_(value) TelemetryBackend::value
#define TIDE_BACKEND_ENUM(value) TIDE_BACKEND_ENUM_(value)

#ifndef TELEMETRY_BACKEND
#error "TELEMETRY_BACKEND must be defined in include/device_config.h"
#endif

constexpr TelemetryBackend TELEMETRY_BACKEND_MODE =
    TIDE_BACKEND_ENUM(TELEMETRY_BACKEND);

// BOTH needs durable per-destination acknowledgement bits. Keeping the v1
// queue record unchanged is safer than pretending that an in-RAM bit survives
// reset and then deleting a record after only one destination acknowledged it.
static_assert(TELEMETRY_BACKEND_MODE != TelemetryBackend::BOTH,
              "BOTH is not supported until durable per-destination queue state is implemented");

constexpr bool telemetryUsesBlynk() {
  return TELEMETRY_BACKEND_MODE == TelemetryBackend::BLYNK_ONLY ||
         TELEMETRY_BACKEND_MODE == TelemetryBackend::BOTH;
}

constexpr bool telemetryUsesMqtt() {
  return TELEMETRY_BACKEND_MODE == TelemetryBackend::MQTT_ONLY ||
         TELEMETRY_BACKEND_MODE == TelemetryBackend::BOTH;
}
