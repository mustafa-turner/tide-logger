#include "MqttTelemetry.h"

#include <WiFi.h>
#include <string.h>

namespace tide {

bool MqttTelemetry::begin(const char *deviceId, const char *host,
                          uint16_t port, uint64_t chipId) {
  if (host == nullptr || host[0] == '\0' || strcmp(host, "0.0.0.0") == 0 ||
      port == 0 ||
      !sanitizeTelemetryIdentifier(deviceId, deviceId_, sizeof(deviceId_)) ||
      !buildTelemetryTopic(deviceId_, topic_, sizeof(topic_)) ||
      !buildTelemetryClientId(deviceId_, chipId, clientId_,
                              sizeof(clientId_))) {
    Serial.println("ERROR MQTT: konfigurasi host/device/client tidak valid.");
    return false;
  }

  host_ = host;
  port_ = port;
  client_.setServer(host_, port_)
      .setClientId(clientId_)
      .setCleanSession(true)
      .setKeepAlive(15)
      .setTimeout(5)
      .onConnect([this](bool sessionPresent) {
        onConnected(sessionPresent);
      })
      .onDisconnect([this](espMqttClientTypes::DisconnectReason reason) {
        onDisconnected(reason);
      })
      .onPublish([this](uint16_t packetId) { onPublished(packetId); });

  initialized_ = true;
  Serial.printf("MQTT siap: host=%s port=%u device=%s topic=%s client=%s "
                "qos=1 retain=false TLS=off auth=none.\n",
                host_, port_, deviceId_, topic_, clientId_);
  return true;
}

bool MqttTelemetry::reconnectIsDue(uint32_t now) const {
  return static_cast<int32_t>(now - nextConnectAttemptAt_) >= 0;
}

void MqttTelemetry::service(bool wifiConnected) {
  if (!initialized_) return;
  wifiConnected_ = wifiConnected;

  if (!wifiConnected_) {
    if (!client_.disconnected()) client_.disconnect(true);
    return;
  }

  const uint32_t now = millis();
  if (client_.disconnected() && reconnectIsDue(now)) {
    Serial.printf("MQTT menghubungkan ke %s:%u sebagai %s.\n", host_, port_,
                  clientId_);
    if (!client_.connect()) {
      nextConnectAttemptAt_ = now + reconnectBackoffMs_;
      reconnectBackoffMs_ =
          min(reconnectBackoffMs_ * 2, MAX_RECONNECT_BACKOFF_MS);
      Serial.printf("MQTT connect belum dapat dimulai; retry %lu ms.\n",
                    static_cast<unsigned long>(reconnectBackoffMs_));
    } else {
      // Also bounds repeated connect() calls while the transport task starts.
      nextConnectAttemptAt_ = now + reconnectBackoffMs_;
    }
  }
}

bool MqttTelemetry::publish(uint32_t sequence, const char *payload,
                            size_t payloadLength) {
  if (!initialized_ || !client_.connected() || sequence == 0 ||
      payload == nullptr || payloadLength == 0 ||
      payloadLength >= TELEMETRY_PAYLOAD_CAPACITY) {
    return false;
  }

  portENTER_CRITICAL(&deliveryMux_);
  const bool alreadyPending = delivery_.pending();
  portEXIT_CRITICAL(&deliveryMux_);
  if (alreadyPending) return false;

  const uint16_t packetId = client_.publish(
      topic_, 1, false, reinterpret_cast<const uint8_t *>(payload),
      payloadLength);
  if (packetId == 0) {
    Serial.printf("MQTT publish gagal diantrikan: topic=%s sequence=%lu.\n",
                  topic_, static_cast<unsigned long>(sequence));
    return false;
  }

  bool acknowledgedEarly = false;
  portENTER_CRITICAL(&deliveryMux_);
  const bool tracked = delivery_.begin(sequence, packetId);
  if (tracked && earlyPubackPacketId_ == packetId) {
    acknowledgedEarly = delivery_.acknowledge(packetId);
  }
  if (tracked) earlyPubackPacketId_ = 0;
  portEXIT_CRITICAL(&deliveryMux_);
  if (!tracked) return false;

  Serial.printf("MQTT PUBLISH: topic=%s packet=%u sequence=%lu bytes=%u%s.\n",
                topic_, packetId, static_cast<unsigned long>(sequence),
                static_cast<unsigned>(payloadLength),
                acknowledgedEarly ? " PUBACK awal" : "");
  return true;
}

bool MqttTelemetry::takeAcknowledgedSequence(uint32_t &sequence) {
  portENTER_CRITICAL(&deliveryMux_);
  const bool acknowledged = delivery_.takeAcknowledged(sequence);
  portEXIT_CRITICAL(&deliveryMux_);
  return acknowledged;
}

bool MqttTelemetry::awaitingPuback() const {
  portENTER_CRITICAL(&deliveryMux_);
  const bool pending = delivery_.pending();
  portEXIT_CRITICAL(&deliveryMux_);
  return pending;
}

bool MqttTelemetry::connected() const { return client_.connected(); }

bool MqttTelemetry::disconnected() const { return client_.disconnected(); }

void MqttTelemetry::disable() {
  if (!initialized_) return;
  wifiConnected_ = false;
  if (!client_.disconnected()) client_.disconnect(true);
  portENTER_CRITICAL(&deliveryMux_);
  delivery_.clear();
  earlyPubackPacketId_ = 0;
  portEXIT_CRITICAL(&deliveryMux_);
  nextConnectAttemptAt_ = 0;
  reconnectBackoffMs_ = INITIAL_RECONNECT_BACKOFF_MS;
  Serial.println("MQTT disabled; Blynk-only delivery is active.");
}

void MqttTelemetry::disconnectForSleep() {
  if (!initialized_ || client_.disconnected()) return;
  const bool force = awaitingPuback();
  Serial.printf("MQTT disconnect sebelum deep sleep%s.\n",
                force ? " (paksa: deadline dengan PUBACK tertunda)" : "");
  client_.disconnect(force);
}

void MqttTelemetry::onConnected(bool sessionPresent) {
  reconnectBackoffMs_ = INITIAL_RECONNECT_BACKOFF_MS;
  nextConnectAttemptAt_ = 0;
  Serial.printf("MQTT connected: host=%s:%u client=%s session=%s.\n", host_,
                port_, clientId_, sessionPresent ? "present" : "new");
}

void MqttTelemetry::onDisconnected(
    espMqttClientTypes::DisconnectReason reason) {
  if (wifiConnected_) {
    nextConnectAttemptAt_ = millis() + reconnectBackoffMs_;
    reconnectBackoffMs_ =
        min(reconnectBackoffMs_ * 2, MAX_RECONNECT_BACKOFF_MS);
  }
  Serial.printf("MQTT disconnected: %s; retry backoff=%lu ms.\n",
                espMqttClientTypes::disconnectReasonToString(reason),
                static_cast<unsigned long>(reconnectBackoffMs_));
}

void MqttTelemetry::onPublished(uint16_t packetId) {
  bool matched = false;
  uint32_t sequence = 0;
  portENTER_CRITICAL(&deliveryMux_);
  if (delivery_.pending()) {
    sequence = delivery_.sequence();
    matched = delivery_.acknowledge(packetId);
  } else {
    earlyPubackPacketId_ = packetId;
  }
  portEXIT_CRITICAL(&deliveryMux_);

  Serial.printf("MQTT PUBACK: packet=%u sequence=%lu result=%s.\n", packetId,
                static_cast<unsigned long>(sequence),
                matched ? "match" : "ignored");
}

}  // namespace tide
