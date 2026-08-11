#pragma once

#include <Arduino.h>
#include <espMqttClient.h>

#include "TelemetryDelivery.h"
#include "TelemetryPayload.h"

namespace tide {

class MqttTelemetry {
 public:
  bool begin(const char *deviceId, const char *host, uint16_t port,
             uint64_t chipId);
  void service(bool wifiConnected);
  bool publish(uint32_t sequence, const char *payload, size_t payloadLength);
  bool takeAcknowledgedSequence(uint32_t &sequence);
  bool awaitingPuback() const;
  bool connected() const;
  bool disconnected() const;
  void disconnectForSleep();

  const char *deviceId() const { return deviceId_; }
  const char *topic() const { return topic_; }
  const char *clientId() const { return clientId_; }

 private:
  static constexpr uint32_t INITIAL_RECONNECT_BACKOFF_MS = 1000;
  static constexpr uint32_t MAX_RECONNECT_BACKOFF_MS = 30000;

  void onConnected(bool sessionPresent);
  void onDisconnected(espMqttClientTypes::DisconnectReason reason);
  void onPublished(uint16_t packetId);
  bool reconnectIsDue(uint32_t now) const;

  espMqttClient client_;
  char deviceId_[TELEMETRY_DEVICE_ID_CAPACITY] = {};
  char topic_[TELEMETRY_TOPIC_CAPACITY] = {};
  char clientId_[TELEMETRY_CLIENT_ID_CAPACITY] = {};
  const char *host_ = nullptr;
  uint16_t port_ = 0;
  bool initialized_ = false;
  bool wifiConnected_ = false;
  uint32_t nextConnectAttemptAt_ = 0;
  uint32_t reconnectBackoffMs_ = INITIAL_RECONNECT_BACKOFF_MS;

  mutable portMUX_TYPE deliveryMux_ = portMUX_INITIALIZER_UNLOCKED;
  TelemetryDeliveryTracker delivery_;
  uint16_t earlyPubackPacketId_ = 0;
};

}  // namespace tide
