#pragma once

#include "device_config.h"
#include "TelemetryDeliveryState.h"

enum class TelemetryBackend { BLYNK_ONLY, MQTT_ONLY, BOTH };

#define TIDE_BACKEND_ENUM_(value) TelemetryBackend::value
#define TIDE_BACKEND_ENUM(value) TIDE_BACKEND_ENUM_(value)

#ifndef TELEMETRY_BACKEND
#error "TELEMETRY_BACKEND must be defined in include/device_config.h"
#endif

constexpr TelemetryBackend TELEMETRY_BACKEND_MODE =
    TIDE_BACKEND_ENUM(TELEMETRY_BACKEND);

constexpr bool telemetryBackendUsesBlynk(TelemetryBackend backend) {
  return backend == TelemetryBackend::BLYNK_ONLY ||
         backend == TelemetryBackend::BOTH;
}

constexpr bool telemetryBackendUsesMqtt(TelemetryBackend backend) {
  return backend == TelemetryBackend::MQTT_ONLY ||
         backend == TelemetryBackend::BOTH;
}

constexpr uint16_t telemetryBackendDeliveryMask(TelemetryBackend backend) {
  return (telemetryBackendUsesBlynk(backend)
              ? tide::telemetryDestinationBit(
                    tide::TelemetryDestination::BLYNK)
              : 0U) |
         (telemetryBackendUsesMqtt(backend)
              ? tide::telemetryDestinationBit(
                    tide::TelemetryDestination::MQTT)
              : 0U);
}
