#include "TelemetryPayload.h"

#include <inttypes.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace tide {
namespace {

class JsonWriter {
 public:
  JsonWriter(char *output, size_t capacity)
      : output_(output), capacity_(capacity) {
    if (capacity_ > 0) output_[0] = '\0';
  }

  bool begin() { return appendRaw("{"); }
  bool end() { return appendRaw("}"); }
  bool ok() const { return ok_; }
  size_t length() const { return length_; }

  bool stringField(const char *name, const char *value) {
    if (!fieldPrefix(name) || !appendRaw("\"")) return false;
    for (const unsigned char *p =
             reinterpret_cast<const unsigned char *>(value);
         *p != '\0'; ++p) {
      if (*p == '\"' || *p == '\\') {
        if (!appendFormat("\\%c", *p)) return false;
      } else if (*p < 0x20) {
        if (!appendFormat("\\u%04x", static_cast<unsigned>(*p))) return false;
      } else if (!appendFormat("%c", *p)) {
        return false;
      }
    }
    return appendRaw("\"");
  }

  bool uintField(const char *name, uint32_t value) {
    return fieldPrefix(name) && appendFormat("%" PRIu32, value);
  }

  bool uint64Field(const char *name, uint64_t value) {
    return fieldPrefix(name) && appendFormat("%" PRIu64, value);
  }

  bool intField(const char *name, int32_t value) {
    return fieldPrefix(name) && appendFormat("%" PRId32, value);
  }

  bool floatField(const char *name, double value, uint8_t decimals) {
    return isfinite(value) && fieldPrefix(name) &&
           appendFormat("%.*f", static_cast<int>(decimals), value);
  }

 private:
  bool fieldPrefix(const char *name) {
    if (!first_ && !appendRaw(",")) return false;
    first_ = false;
    return appendFormat("\"%s\":", name);
  }

  bool appendRaw(const char *text) { return appendFormat("%s", text); }

  bool appendFormat(const char *format, ...) {
    if (!ok_ || capacity_ == 0 || length_ >= capacity_) {
      ok_ = false;
      return false;
    }
    va_list args;
    va_start(args, format);
    const int result =
        vsnprintf(output_ + length_, capacity_ - length_, format, args);
    va_end(args);
    if (result < 0 || static_cast<size_t>(result) >= capacity_ - length_) {
      output_[capacity_ - 1] = '\0';
      ok_ = false;
      return false;
    }
    length_ += static_cast<size_t>(result);
    return true;
  }

  char *output_;
  size_t capacity_;
  size_t length_ = 0;
  bool first_ = true;
  bool ok_ = true;
};

bool validFloat(float value) { return isfinite(static_cast<double>(value)); }

}  // namespace

bool sanitizeTelemetryIdentifier(const char *input, char *output,
                                 size_t outputSize) {
  if (input == nullptr || output == nullptr || outputSize < 2) return false;

  size_t length = 0;
  bool dashPending = false;
  for (const unsigned char *p =
           reinterpret_cast<const unsigned char *>(input);
       *p != '\0'; ++p) {
    const bool asciiLetter = (*p >= 'A' && *p <= 'Z') ||
                             (*p >= 'a' && *p <= 'z');
    const bool asciiDigit = *p >= '0' && *p <= '9';
    if (asciiLetter || asciiDigit) {
      if (dashPending && length > 0) {
        if (length + 1 >= outputSize) return false;
        output[length++] = '-';
      }
      dashPending = false;
      if (length + 1 >= outputSize) return false;
      output[length++] = *p >= 'A' && *p <= 'Z'
                             ? static_cast<char>(*p - 'A' + 'a')
                             : static_cast<char>(*p);
    } else if ((*p == '-' || *p == '_') && length > 0) {
      dashPending = true;
    } else if (length > 0) {
      dashPending = true;
    }
  }
  if (length == 0) return false;
  output[length] = '\0';
  return true;
}

bool buildTelemetryTopic(const char *sanitizedDeviceId, char *output,
                         size_t outputSize) {
  if (sanitizedDeviceId == nullptr || output == nullptr) return false;
  const int result = snprintf(output, outputSize, "telemetry/tide_sensor/%s",
                              sanitizedDeviceId);
  return result > 0 && static_cast<size_t>(result) < outputSize;
}

bool buildTelemetryClientId(const char *sanitizedDeviceId,
                            uint64_t chipId, char *output,
                            size_t outputSize) {
  if (sanitizedDeviceId == nullptr || output == nullptr) return false;
  const unsigned long suffix =
      static_cast<unsigned long>(chipId & 0xFFFFFFULL);
  const int result = snprintf(output, outputSize, "tide-%s-%06lx",
                              sanitizedDeviceId, suffix);
  return result > 0 && static_cast<size_t>(result) < outputSize;
}

bool serializeTelemetryPayload(const MeasurementRecord &record,
                               const TelemetryPayloadContext &context,
                               char *output, size_t outputSize,
                               size_t &written) {
  written = 0;
  if (context.deviceId == nullptr || context.firmwareVersion == nullptr ||
      output == nullptr || outputSize == 0) {
    return false;
  }

  JsonWriter json(output, outputSize);
  json.begin();
  json.stringField("schema", "tide-logger-v1");
  json.stringField("device_id", context.deviceId);
  json.stringField("device_type", "tide_sensor");
  if ((record.flags & RECORD_TIME_VALID) != 0 && record.timestampMs != 0) {
    json.uint64Field("measured_at_ms", record.timestampMs);
  }
  json.uintField("sequence", record.sequence);

  if ((record.flags & RECORD_DISTANCE_VALID) != 0) {
    json.floatField("water_level_m", record.waterLevelMm / 1000.0, 3);
    json.uintField("distance_to_water_mm", record.distanceMm);
    const int64_t sensorHeightMm =
        static_cast<int64_t>(record.waterLevelMm) + record.distanceMm;
    json.floatField("sensor_height_m", sensorHeightMm / 1000.0, 3);
  }
  if ((record.flags & RECORD_BATTERY_VALID) != 0 &&
      validFloat(record.batteryVoltage)) {
    json.floatField("battery_voltage_v", record.batteryVoltage, 3);
  }
  if ((record.flags & RECORD_SOLAR_VALID) != 0 &&
      validFloat(record.solarVoltage)) {
    json.floatField("solar_voltage_v", record.solarVoltage, 3);
  }
  if ((record.flags & RECORD_SYSTEM_VALID) != 0) {
    if (validFloat(record.systemVoltage)) {
      json.floatField("system_voltage_v", record.systemVoltage, 3);
    }
    if (validFloat(record.systemCurrent)) {
      json.floatField("system_current_a", record.systemCurrent, 3);
    }
  }
  if ((record.flags & RECORD_SHT40_VALID) != 0) {
    if (validFloat(record.temperatureC)) {
      json.floatField("temperature_c", record.temperatureC, 2);
    }
    if (validFloat(record.humidityPercent)) {
      json.floatField("humidity_percent", record.humidityPercent, 2);
    }
  }

  json.uintField("measurement_quality", record.quality);
  json.uintField("samples_acquired", record.acquiredSamples);
  json.uintField("samples_used", record.usedSamples);
  json.uintField("mad_outliers", record.outlierSamples);
  // Schema v1 has no separate MAD-valid bit. Acquisition reaches the MAD
  // calculation only at 30 samples, so omit it below that threshold.
  if (record.acquiredSamples >= 30 && validFloat(record.distanceMadMm)) {
    json.floatField("distance_mad_mm", record.distanceMadMm, 2);
  }
  json.uintField("acquisition_duration_ms", record.acquisitionDurationMs);
  json.uintField("pending_records", context.pendingRecords);
  json.uintField("dropped_records", context.droppedRecords);
  json.stringField("firmware_version", context.firmwareVersion);
  json.intField("wifi_rssi_dbm", context.wifiRssiDbm);
  json.uintField("uptime_sec", context.uptimeSec);
  json.end();

  if (!json.ok()) return false;
  written = json.length();
  return written > 0 && written < outputSize;
}

}  // namespace tide
