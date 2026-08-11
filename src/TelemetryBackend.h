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

constexpr bool telemetryUsesBlynk() {
  return TELEMETRY_BACKEND_MODE == TelemetryBackend::BLYNK_ONLY ||
         TELEMETRY_BACKEND_MODE == TelemetryBackend::BOTH;
}

constexpr bool telemetryUsesMqtt() {
  return TELEMETRY_BACKEND_MODE == TelemetryBackend::MQTT_ONLY ||
         TELEMETRY_BACKEND_MODE == TelemetryBackend::BOTH;
}

constexpr uint16_t telemetryRequiredDeliveryMask() {
  return (telemetryUsesBlynk()
              ? tide::telemetryDestinationBit(
                    tide::TelemetryDestination::BLYNK)
              : 0U) |
         (telemetryUsesMqtt()
              ? tide::telemetryDestinationBit(
                    tide::TelemetryDestination::MQTT)
              : 0U);
}
