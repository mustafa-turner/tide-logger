#include <unity.h>

#include <cstdio>
#include <cstring>

#ifdef ARDUINO
#include <Arduino.h>
#endif

#include "TelemetryDelivery.h"
#include "TelemetryDeliveryState.h"
#include "MqttHostConfig.h"
#include "TelemetryPayload.h"

using tide::MeasurementRecord;

namespace {

void test_identifier_and_mqtt_names_are_sanitized() {
  char device[tide::TELEMETRY_DEVICE_ID_CAPACITY] = {};
  char topic[tide::TELEMETRY_TOPIC_CAPACITY] = {};
  char client[tide::TELEMETRY_CLIENT_ID_CAPACITY] = {};

  TEST_ASSERT_TRUE(tide::sanitizeTelemetryIdentifier(
      "  Tide Station/01  ", device, sizeof(device)));
  TEST_ASSERT_EQUAL_STRING("tide-station-01", device);
  TEST_ASSERT_TRUE(tide::buildTelemetryTopic(device, topic, sizeof(topic)));
  TEST_ASSERT_EQUAL_STRING("telemetry/tide_sensor/tide-station-01", topic);
  TEST_ASSERT_TRUE(
      tide::buildTelemetryClientId(device, 0x12345678ULL, client,
                                   sizeof(client)));
  TEST_ASSERT_EQUAL_STRING("tide-tide-station-01-345678", client);
}

void test_complete_payload_uses_original_timestamp_and_canonical_fields() {
  MeasurementRecord record = {};
  record.flags = tide::RECORD_TIME_VALID | tide::RECORD_BATTERY_VALID |
                 tide::RECORD_SOLAR_VALID | tide::RECORD_SYSTEM_VALID |
                 tide::RECORD_DISTANCE_VALID | tide::RECORD_SHT40_VALID;
  record.sequence = 125;
  record.timestampMs = 1786406400000ULL;
  record.waterLevelMm = 1749;
  record.distanceMm = 1251;
  record.batteryVoltage = 4.012f;
  record.solarVoltage = 18.4f;
  record.systemVoltage = 5.016f;
  record.systemCurrent = 0.35f;
  record.temperatureC = 28.5f;
  record.humidityPercent = 76.2f;
  record.quality = 2;
  record.acquiredSamples = 50;
  record.usedSamples = 44;
  record.outlierSamples = 2;
  record.distanceMadMm = 4.0f;
  record.acquisitionDurationMs = 5100;

  const tide::TelemetryPayloadContext context = {
      "tide-station-01", 3, 0, "1.4.0", -61, 38};
  char payload[tide::TELEMETRY_PAYLOAD_CAPACITY] = {};
  size_t written = 0;
  TEST_ASSERT_TRUE(tide::serializeTelemetryPayload(
      record, context, payload, sizeof(payload), written));
  TEST_ASSERT_EQUAL_UINT(std::strlen(payload), written);
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"schema\":\"tide-logger-v1\""));
  TEST_ASSERT_NOT_NULL(
      std::strstr(payload, "\"measured_at_ms\":1786406400000"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"water_level_m\":1.749"));
  TEST_ASSERT_NOT_NULL(
      std::strstr(payload, "\"distance_to_water_mm\":1251"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"sensor_height_m\":3.000"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"battery_voltage_v\":4.012"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"measurement_quality\":2"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"distance_mad_mm\":4.00"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"pending_records\":3"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"wifi_rssi_dbm\":-61"));

  const char *canonicalNames[] = {
      "schema",          "device_id",          "device_type",
      "measured_at_ms", "sequence",           "water_level_m",
      "distance_to_water_mm",                    "sensor_height_m",
      "battery_voltage_v",                       "solar_voltage_v",
      "system_voltage_v",                        "system_current_a",
      "temperature_c",    "humidity_percent",  "measurement_quality",
      "samples_acquired", "samples_used",      "mad_outliers",
      "distance_mad_mm",  "acquisition_duration_ms",
      "pending_records",  "dropped_records",   "firmware_version",
      "wifi_rssi_dbm",    "uptime_sec"};
  for (const char *name : canonicalNames) {
    char quoted[64] = {};
    std::snprintf(quoted, sizeof(quoted), "\"%s\":", name);
    TEST_ASSERT_NOT_NULL_MESSAGE(std::strstr(payload, quoted), name);
  }
}

void test_invalid_optional_values_are_omitted_but_diagnostics_remain() {
  MeasurementRecord record = {};
  record.sequence = 9;
  record.quality = 0;
  record.acquiredSamples = 12;
  record.usedSamples = 0;
  record.outlierSamples = 0;
  record.acquisitionDurationMs = 20000;

  const tide::TelemetryPayloadContext context = {
      "tide-station-01", 1, 4, "1.4.0", -72, 22};
  char payload[tide::TELEMETRY_PAYLOAD_CAPACITY] = {};
  size_t written = 0;
  TEST_ASSERT_TRUE(tide::serializeTelemetryPayload(
      record, context, payload, sizeof(payload), written));
  TEST_ASSERT_NULL(std::strstr(payload, "measured_at_ms"));
  TEST_ASSERT_NULL(std::strstr(payload, "water_level_m"));
  TEST_ASSERT_NULL(std::strstr(payload, "battery_voltage_v"));
  TEST_ASSERT_NULL(std::strstr(payload, "distance_mad_mm"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"measurement_quality\":0"));
  TEST_ASSERT_NOT_NULL(std::strstr(payload, "\"samples_acquired\":12"));
  TEST_ASSERT_NOT_NULL(
      std::strstr(payload, "\"acquisition_duration_ms\":20000"));
}

void test_only_matching_puback_transitions_delivery() {
  tide::TelemetryDeliveryTracker tracker;
  TEST_ASSERT_TRUE(tracker.begin(125, 44));
  TEST_ASSERT_FALSE(tracker.acknowledge(45));
  uint32_t sequence = 0;
  TEST_ASSERT_FALSE(tracker.takeAcknowledged(sequence));
  TEST_ASSERT_TRUE(tracker.acknowledge(44));
  TEST_ASSERT_TRUE(tracker.takeAcknowledged(sequence));
  TEST_ASSERT_EQUAL_UINT32(125, sequence);
  TEST_ASSERT_FALSE(tracker.pending());
}

void test_dual_destination_cursors_advance_independently() {
  uint32_t blynkCursor = 0;
  uint32_t mqttCursor = 0;

  TEST_ASSERT_TRUE(tide::telemetrySequenceAfter(125, blynkCursor));
  TEST_ASSERT_TRUE(tide::telemetrySequenceAfter(125, mqttCursor));

  mqttCursor = 125;
  TEST_ASSERT_FALSE(tide::telemetrySequenceDelivered(125, blynkCursor));
  TEST_ASSERT_TRUE(tide::telemetrySequenceDelivered(125, mqttCursor));
  TEST_ASSERT_TRUE(tide::telemetrySequenceAfter(126, mqttCursor));

  blynkCursor = 125;
  TEST_ASSERT_TRUE(tide::telemetrySequenceDelivered(125, blynkCursor));
  TEST_ASSERT_TRUE(tide::telemetrySequenceDelivered(125, mqttCursor));

  TEST_ASSERT_TRUE(tide::telemetrySequenceAfter(1, UINT32_MAX));
  TEST_ASSERT_TRUE(tide::telemetrySequenceDelivered(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT32(1,
                           tide::telemetryPendingOffset(UINT32_MAX, UINT32_MAX));
  TEST_ASSERT_EQUAL_UINT32(
      3, tide::telemetryPendingOffset(UINT32_MAX - 1U, 1));
}

void test_mqtt_runtime_host_requires_unicast_ipv4() {
  TEST_ASSERT_TRUE(tide::isValidMqttIpv4("192.168.1.50"));
  TEST_ASSERT_TRUE(tide::isValidMqttIpv4("172.27.111.115"));
  TEST_ASSERT_TRUE(tide::isValidMqttIpv4("1.2.3.4"));

  TEST_ASSERT_FALSE(tide::isValidMqttIpv4(nullptr));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4(""));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("mqtt.local"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("192.168.1"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("192.168.1.256"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("192.168.1.50 extra"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("0.0.0.0"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("224.0.0.1"));
  TEST_ASSERT_FALSE(tide::isValidMqttIpv4("255.255.255.255"));
}

}  // namespace

int runTelemetryTests() {
  UNITY_BEGIN();
  RUN_TEST(test_identifier_and_mqtt_names_are_sanitized);
  RUN_TEST(test_complete_payload_uses_original_timestamp_and_canonical_fields);
  RUN_TEST(test_invalid_optional_values_are_omitted_but_diagnostics_remain);
  RUN_TEST(test_only_matching_puback_transitions_delivery);
  RUN_TEST(test_dual_destination_cursors_advance_independently);
  RUN_TEST(test_mqtt_runtime_host_requires_unicast_ipv4);
  return UNITY_END();
}

#ifdef ARDUINO
void setup() {
  delay(2000);
  runTelemetryTests();
}
void loop() {}
#else
int main(int, char **) {
  return runTelemetryTests();
}
#endif
