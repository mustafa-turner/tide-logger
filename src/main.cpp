#include <Arduino.h>
#include "OfflineQueue.h"
#include "secrets.h"

#define BLYNK_FIRMWARE_VERSION "1.3.0"
#define BLYNK_PRINT Serial
#include <DNSServer.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <Update.h>
#include <WebServer.h>
#include <WiFiClientSecure.h>
#include <BlynkEdgent.h>
#include <Wire.h>
#include <WiFi.h>
#include <driver/gpio.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

namespace {
constexpr uint8_t INA3221_ADDRESS = 0x40;
constexpr uint8_t SHT40_ADDRESS = 0x44;
constexpr uint8_t REG_CONFIG = 0x00;
constexpr uint8_t REG_CH1_BUS_VOLTAGE = 0x02;
constexpr uint8_t REG_CH2_BUS_VOLTAGE = 0x04;
constexpr uint8_t REG_CH3_SHUNT_VOLTAGE = 0x05;
constexpr uint8_t REG_CH3_BUS_VOLTAGE = 0x06;

// Sesuaikan dengan nilai resistor shunt channel 3 pada modul Anda.
// Modul INA3221 yang umum menggunakan shunt 0,1 ohm (R100).
constexpr float CH3_SHUNT_RESISTANCE_OHM = 0.1f;

// A02YYUW: TX sensor dihubungkan ke D7 (RX XIAO ESP32S3).
constexpr uint8_t A02YYUW_RX_PIN = D7;
constexpr uint8_t A02YYUW_TX_PIN = D6;
// D0 HIGH mengaktifkan PN2222A dan BS250 sehingga catu A02YYUW menyala.
constexpr uint8_t A02YYUW_POWER_PIN = D0;
constexpr uint32_t A02YYUW_WARMUP_MS = 1000;
constexpr uint8_t A02YYUW_DISCARD_SAMPLES = 5;
constexpr uint8_t A02YYUW_TARGET_SAMPLES = 50;
constexpr uint8_t A02YYUW_MIN_VALID_SAMPLES = 30;
constexpr uint32_t A02YYUW_ACQUISITION_TIMEOUT_MS = 20000;
constexpr float A02YYUW_MIN_OUTLIER_LIMIT_MM = 20.0f;
constexpr float A02YYUW_GOOD_MAX_MAD_MM = 30.0f;

constexpr uint32_t MEASUREMENT_INTERVAL_SECONDS = 5UL * 60UL;
constexpr uint32_t MEASUREMENT_INTERVAL_MS =
    MEASUREMENT_INTERVAL_SECONDS * 1000UL;
constexpr uint32_t EDGENT_CONNECT_TIMEOUT_MS = 15000;
constexpr uint32_t BLYNK_SYNC_FALLBACK_MS = 3000;
constexpr uint32_t OTA_LISTEN_WINDOW_MS = 15000;
constexpr uint32_t MINIMUM_SLEEP_MS = 1000;
constexpr uint32_t NTP_SYNC_STATUS_TIMEOUT_MS = 10000;
constexpr uint32_t UPLOAD_ACK_REQUEST_DELAY_MS = 100;
constexpr uint32_t UPLOAD_ACK_TIMEOUT_MS = 1500;
constexpr uint8_t MAX_RECORD_UPLOADS_PER_CYCLE = 2;
constexpr time_t MIN_VALID_UTC_EPOCH = 1704067200;  // 2024-01-01 00:00 UTC
constexpr char NTP_SERVER_PRIMARY[] = "pool.ntp.org";
constexpr char NTP_SERVER_SECONDARY[] = "time.google.com";
constexpr char NTP_SERVER_TERTIARY[] = "time.cloudflare.com";
constexpr char STAY_AWAKE_KEY[] = "stay_awake";

// Jarak vertikal sensor ke titik nol/dasar pengukuran pasang surut.
// Ubah nilai ini sesuai hasil pengukuran di lokasi pemasangan.
constexpr uint16_t DEFAULT_SENSOR_HEIGHT_MM = 3000;
constexpr float MIN_SENSOR_HEIGHT_M = 0.03f;
constexpr float MAX_SENSOR_HEIGHT_M = 20.0f;

// Semua channel aktif, pengukuran shunt dan bus secara continuous.
constexpr uint16_t INA3221_CONFIG_ALL_CHANNELS_CONTINUOUS = 0x7127;

float latestSolarVoltage = NAN;
float latestBatteryVoltage = NAN;
float latestSystem5VVoltage = NAN;
float latestSystemCurrent = NAN;
uint16_t latestDistanceMm = 0;
int32_t latestWaterLevelMm = 0;
uint32_t sensorHeightMm = DEFAULT_SENSOR_HEIGHT_MM;
bool solarVoltageValid = false;
bool batteryValid = false;
bool system5VVoltageValid = false;
bool distanceValid = false;
float latestTemperatureC = NAN;
float latestHumidityPercent = NAN;
bool sht40Valid = false;
bool a02yyuwPowerEnabled = false;
bool a02yyuwWarmupComplete = false;
uint32_t a02yyuwPowerOnAt = 0;
Preferences appPreferences;

enum class MeasurementQuality : uint8_t {
  INVALID = 0,
  POOR = 1,
  GOOD = 2,
};

enum class A02FrameResult : uint8_t {
  NONE,
  VALID,
  CHECKSUM_ERROR,
  RANGE_ERROR,
};

uint8_t a02Frame[4] = {};
uint8_t a02FrameIndex = 0;
uint16_t a02Samples[A02YYUW_TARGET_SAMPLES] = {};
uint8_t a02DiscardedSamples = 0;
uint8_t a02AcquiredSamples = 0;
uint16_t a02ChecksumErrors = 0;
uint16_t a02RangeErrors = 0;
uint8_t a02UsedSamples = 0;
uint8_t a02OutlierSamples = 0;
float latestDistanceMedianMm = NAN;
float latestDistanceMadMm = NAN;
float latestOutlierLimitMm = NAN;
uint32_t latestAcquisitionDurationMs = 0;
MeasurementQuality measurementQuality = MeasurementQuality::INVALID;
uint32_t a02AcquisitionStartedAt = 0;
bool normalCycleStarted = false;
bool automaticMeasurementComplete = false;
bool otherMeasurementsComplete = false;
uint32_t cycleStartedAt = 0;

bool edgentConfiguredAtBoot = false;
bool edgentConnectTimeoutArmed = false;
bool edgentConnectTimedOut = false;
bool provisioningRequested = false;
bool cycleUploaded = false;
bool v6SyncReceived = false;
bool v17SyncReceived = false;
bool deepSleepDisabled = false;
uint32_t edgentConnectStartedAt = 0;
uint32_t blynkConnectedAt = 0;
uint32_t cycleUploadedAt = 0;
bool ntpSyncRequested = false;
bool ntpSyncTimeoutReported = false;
bool utcTimeAvailableReported = false;
uint32_t ntpSyncRequestedAt = 0;
bool cycleAbsoluteSlotValid = false;
uint64_t cycleAbsoluteSlot = 0;
bool cycleStartedWithAbsoluteTime = false;
tide::OfflineQueue offlineQueue;
tide::MeasurementRecord currentCycleRecord = {};
bool currentCycleRecordReady = false;
bool currentCycleRecordQueued = false;
uint32_t currentCycleSequence = 0;
uint8_t recordsUploadedThisCycle = 0;
uint32_t pendingUploadSequence = 0;
uint32_t uploadStateChangedAt = 0;
uint32_t lastCloudSequence = 0;
bool pendingUploadAcknowledged = false;
bool v21SyncReceived = false;

enum class OfflineUploadState : uint8_t {
  IDLE,
  WAITING_TO_REQUEST_ACK,
  WAITING_FOR_ACK,
};

OfflineUploadState offlineUploadState = OfflineUploadState::IDLE;

bool getValidUtcTime(timeval &utcNow) {
  gettimeofday(&utcNow, nullptr);
  return utcNow.tv_sec >= MIN_VALID_UTC_EPOCH;
}

bool formatUtcTimestamp(time_t epoch, char *buffer, size_t bufferLength) {
  tm utcTime = {};
  if (gmtime_r(&epoch, &utcTime) == nullptr) {
    return false;
  }
  return strftime(buffer, bufferLength, "%Y-%m-%d %H:%M:%S UTC", &utcTime) >
         0;
}

void captureCurrentAbsoluteSlot() {
  timeval utcNow = {};
  if (!getValidUtcTime(utcNow)) {
    cycleAbsoluteSlotValid = false;
    return;
  }

  cycleAbsoluteSlot =
      static_cast<uint64_t>(utcNow.tv_sec) / MEASUREMENT_INTERVAL_SECONDS;
  cycleAbsoluteSlotValid = true;

  char timestamp[32] = {};
  if (formatUtcTimestamp(utcNow.tv_sec, timestamp, sizeof(timestamp))) {
    Serial.printf("Slot waktu siklus: %s (slot %llu).\n", timestamp,
                  static_cast<unsigned long long>(cycleAbsoluteSlot));
  }
}

void serviceNetworkTime() {
  if (WiFi.status() == WL_CONNECTED && !ntpSyncRequested) {
    configTime(0, 0, NTP_SERVER_PRIMARY, NTP_SERVER_SECONDARY,
               NTP_SERVER_TERTIARY);
    ntpSyncRequested = true;
    ntpSyncRequestedAt = millis();
    Serial.println("Sinkronisasi waktu UTC melalui NTP dimulai.");
  }

  timeval utcNow = {};
  if (getValidUtcTime(utcNow)) {
    if (!utcTimeAvailableReported) {
      utcTimeAvailableReported = true;
      char timestamp[32] = {};
      if (formatUtcTimestamp(utcNow.tv_sec, timestamp, sizeof(timestamp))) {
        Serial.printf("Waktu UTC tersedia: %s.\n", timestamp);
      }
    }
    if (normalCycleStarted && !cycleAbsoluteSlotValid) {
      captureCurrentAbsoluteSlot();
      Serial.println("Siklus berikutnya akan mengikuti batas absolut 5 menit.");
    }
    return;
  }

  if (ntpSyncRequested && !ntpSyncTimeoutReported &&
      millis() - ntpSyncRequestedAt >= NTP_SYNC_STATUS_TIMEOUT_MS) {
    ntpSyncTimeoutReported = true;
    Serial.println("NTP belum memberikan waktu; jadwal relatif dipakai sementara.");
  }
}

bool stayAwakeCycleIsDue() {
  timeval utcNow = {};
  if (getValidUtcTime(utcNow)) {
    const uint64_t currentSlot =
        static_cast<uint64_t>(utcNow.tv_sec) / MEASUREMENT_INTERVAL_SECONDS;
    if (!cycleAbsoluteSlotValid) {
      cycleAbsoluteSlot = currentSlot;
      cycleAbsoluteSlotValid = true;
      return false;
    }
    return currentSlot > cycleAbsoluteSlot;
  }

  return millis() - cycleStartedAt >= MEASUREMENT_INTERVAL_MS;
}

bool calculateAbsoluteSleep(uint64_t &sleepDurationUs,
                            time_t &nextWakeEpoch) {
  timeval utcNow = {};
  if (!getValidUtcTime(utcNow)) {
    return false;
  }

  constexpr uint64_t MICROSECONDS_PER_SECOND = 1000000ULL;
  const uint64_t intervalUs =
      static_cast<uint64_t>(MEASUREMENT_INTERVAL_SECONDS) *
      MICROSECONDS_PER_SECOND;
  const uint64_t currentEpochUs =
      static_cast<uint64_t>(utcNow.tv_sec) * MICROSECONDS_PER_SECOND +
      static_cast<uint32_t>(utcNow.tv_usec);
  const uint64_t elapsedInSlotUs = currentEpochUs % intervalUs;
  sleepDurationUs = intervalUs - elapsedInSlotUs;

  // Hindari bangun ulang hampir seketika apabila proses selesai tepat sebelum
  // batas slot. Dalam kondisi ini gunakan batas lima menit sesudahnya.
  if (sleepDurationUs < 100000ULL) {
    sleepDurationUs += intervalUs;
  }

  nextWakeEpoch =
      static_cast<time_t>((currentEpochUs + sleepDurationUs) /
                          MICROSECONDS_PER_SECOND);
  return true;
}

void resetA02FrameParser() {
  memset(a02Frame, 0, sizeof(a02Frame));
  a02FrameIndex = 0;
}

void setA02YYUWPower(bool enabled) {
  digitalWrite(A02YYUW_POWER_PIN, enabled ? HIGH : LOW);
  a02yyuwPowerEnabled = enabled;
  resetA02FrameParser();

  while (Serial1.available() > 0) {
    Serial1.read();
  }

  if (enabled) {
    a02yyuwPowerOnAt = millis();
    a02yyuwWarmupComplete = false;
    Serial.printf("A02YYUW ON: D0/GPIO %d HIGH, warm-up %lu ms.\n",
                  A02YYUW_POWER_PIN,
                  static_cast<unsigned long>(A02YYUW_WARMUP_MS));
  } else {
    a02yyuwWarmupComplete = false;
    Serial.printf("A02YYUW OFF: D0/GPIO %d LOW.\n", A02YYUW_POWER_PIN);
  }

  if (Blynk.connected()) {
    Blynk.virtualWrite(V9, enabled ? 1 : 0);
  }
}

void printA02YYUWPowerStatus() {
  Serial.printf("A02YYUW power: %s | D0/GPIO %d: %s\n",
                a02yyuwPowerEnabled ? "ON" : "OFF", A02YYUW_POWER_PIN,
                digitalRead(A02YYUW_POWER_PIN) == HIGH ? "HIGH" : "LOW");
}

void registerA02YYUWConsoleCommand() {
  edgentConsole.addCommand("sensor", [](int argc, const char **argv) {
    if (argc < 1 || strcmp(argv[0], "status") == 0) {
      printA02YYUWPowerStatus();
    } else if (strcmp(argv[0], "on") == 0) {
      if (!automaticMeasurementComplete) {
        Serial.println("Akuisisi otomatis masih berjalan; tunggu hingga selesai.");
      } else {
        setA02YYUWPower(true);
      }
    } else if (strcmp(argv[0], "off") == 0) {
      if (!automaticMeasurementComplete) {
        Serial.println("Akuisisi otomatis masih berjalan; sensor tidak dimatikan.");
      } else {
        setA02YYUWPower(false);
      }
    } else {
      Serial.println("Perintah: sensor on | sensor off | sensor status");
    }
  });
}

uint64_t estimateCurrentCycleTimestampMs() {
  if (cycleStartedWithAbsoluteTime && cycleAbsoluteSlotValid) {
    return cycleAbsoluteSlot * MEASUREMENT_INTERVAL_SECONDS * 1000ULL;
  }

  timeval utcNow = {};
  if (!getValidUtcTime(utcNow)) {
    return 0;
  }

  const uint64_t nowMs = static_cast<uint64_t>(utcNow.tv_sec) * 1000ULL +
                         static_cast<uint32_t>(utcNow.tv_usec) / 1000ULL;
  const uint32_t elapsedMs = millis() - cycleStartedAt;
  return nowMs > elapsedMs ? nowMs - elapsedMs : 0;
}

tide::MeasurementRecord buildCurrentMeasurementRecord() {
  tide::MeasurementRecord record = {};
  if (batteryValid) {
    record.flags |= tide::RECORD_BATTERY_VALID;
    record.batteryVoltage = latestBatteryVoltage;
  }
  if (solarVoltageValid) {
    record.flags |= tide::RECORD_SOLAR_VALID;
    record.solarVoltage = latestSolarVoltage;
  }
  if (system5VVoltageValid) {
    record.flags |= tide::RECORD_SYSTEM_VALID;
    record.systemVoltage = latestSystem5VVoltage;
    record.systemCurrent = latestSystemCurrent;
  }
  if (distanceValid) {
    record.flags |= tide::RECORD_DISTANCE_VALID;
    record.distanceMm = latestDistanceMm;
    record.waterLevelMm = latestWaterLevelMm;
  }
  if (sht40Valid) {
    record.flags |= tide::RECORD_SHT40_VALID;
    record.temperatureC = latestTemperatureC;
    record.humidityPercent = latestHumidityPercent;
  }

  record.timestampMs = estimateCurrentCycleTimestampMs();
  if (record.timestampMs != 0) {
    record.flags |= tide::RECORD_TIME_VALID;
  }
  record.distanceMadMm =
      isnan(latestDistanceMadMm) ? 0.0f : latestDistanceMadMm;
  record.acquisitionDurationMs = latestAcquisitionDurationMs;
  record.acquiredSamples = a02AcquiredSamples;
  record.usedSamples = a02UsedSamples;
  record.outlierSamples = a02OutlierSamples;
  record.quality = static_cast<uint8_t>(measurementQuality);
  return record;
}

void persistCurrentMeasurement() {
  if (currentCycleRecordReady) {
    return;
  }

  currentCycleRecord = buildCurrentMeasurementRecord();
  currentCycleRecordReady = true;
  if (offlineQueue.ready() && offlineQueue.enqueue(currentCycleRecord)) {
    currentCycleRecordQueued = true;
    currentCycleSequence = currentCycleRecord.sequence;
    Serial.printf("Record #%lu disimpan; antrean offline %lu/%lu.\n",
                  static_cast<unsigned long>(currentCycleSequence),
                  static_cast<unsigned long>(offlineQueue.count()),
                  static_cast<unsigned long>(offlineQueue.capacity()));
  } else {
    tide::OfflineQueue::finalizeRecord(currentCycleRecord);
    Serial.println("PERINGATAN: record hanya tersedia di RAM; LittleFS gagal.");
  }
}

void retryPersistCurrentMeasurement() {
  if (!currentCycleRecordReady || currentCycleRecordQueued ||
      !offlineQueue.ready()) {
    return;
  }

  tide::MeasurementRecord retryRecord = currentCycleRecord;
  if (offlineQueue.enqueue(retryRecord)) {
    currentCycleRecord = retryRecord;
    currentCycleRecordQueued = true;
    currentCycleSequence = retryRecord.sequence;
    Serial.printf("Retry penyimpanan record #%lu berhasil; antrean=%lu.\n",
                  static_cast<unsigned long>(currentCycleSequence),
                  static_cast<unsigned long>(offlineQueue.count()));
  }
}

void updateCurrentRecordTimestampIfPossible() {
  if (!currentCycleRecordReady ||
      (currentCycleRecord.flags & tide::RECORD_TIME_VALID) != 0) {
    return;
  }

  const uint64_t timestampMs = estimateCurrentCycleTimestampMs();
  if (timestampMs == 0) {
    return;
  }

  currentCycleRecord.timestampMs = timestampMs;
  currentCycleRecord.flags |= tide::RECORD_TIME_VALID;
  tide::OfflineQueue::finalizeRecord(currentCycleRecord);
  if (currentCycleRecordQueued &&
      !offlineQueue.updateNewestTimestamp(currentCycleSequence, timestampMs)) {
    Serial.println("PERINGATAN: timestamp record terbaru gagal diperbarui.");
  } else {
    Serial.printf("Timestamp record #%lu diperbarui setelah sinkronisasi NTP.\n",
                  static_cast<unsigned long>(currentCycleSequence));
  }
}

void sendRecordToBlynk(const tide::MeasurementRecord &record) {
  if (!Blynk.connected()) {
    return;
  }

  if ((record.flags & tide::RECORD_TIME_VALID) != 0) {
    Blynk.beginGroup(record.timestampMs);
  } else {
    // Blynk menggunakan waktu server. Nilai tetap terselamatkan, tetapi waktu
    // asli tidak dapat direkonstruksi setelah cold boot offline tanpa RTC.
    Blynk.beginGroup();
  }

  if ((record.flags & tide::RECORD_BATTERY_VALID) != 0) {
    Blynk.virtualWrite(V0, record.batteryVoltage);
  }
  if ((record.flags & tide::RECORD_SOLAR_VALID) != 0) {
    Blynk.virtualWrite(V5, record.solarVoltage);
  }
  if ((record.flags & tide::RECORD_SYSTEM_VALID) != 0) {
    Blynk.virtualWrite(V3, record.systemVoltage);
    Blynk.virtualWrite(V4, record.systemCurrent);
  }
  if ((record.flags & tide::RECORD_DISTANCE_VALID) != 0) {
    Blynk.virtualWrite(V1, record.distanceMm);
    Blynk.virtualWrite(V2, record.waterLevelMm / 1000.0f);
  }
  if ((record.flags & tide::RECORD_SHT40_VALID) != 0) {
    Blynk.virtualWrite(V7, record.temperatureC);
    Blynk.virtualWrite(V8, record.humidityPercent);
  }

  Blynk.virtualWrite(V11, record.quality);
  Blynk.virtualWrite(V12, record.acquiredSamples);
  Blynk.virtualWrite(V13, record.usedSamples);
  Blynk.virtualWrite(V14, record.outlierSamples);
  Blynk.virtualWrite(V15, record.distanceMadMm);
  Blynk.virtualWrite(V16, record.acquisitionDurationMs);
  Blynk.virtualWrite(V21, record.sequence);
  Blynk.endGroup();
}

void publishOfflineQueueDiagnostics() {
  if (!Blynk.connected()) {
    return;
  }
  const int32_t pendingRecords =
      offlineQueue.ready() ? static_cast<int32_t>(offlineQueue.count()) : -1;
  const int32_t droppedRecords =
      offlineQueue.ready()
          ? static_cast<int32_t>(min(offlineQueue.droppedRecords(),
                                     static_cast<uint32_t>(INT32_MAX)))
          : -1;
  Blynk.virtualWrite(V19, pendingRecords);
  Blynk.virtualWrite(V20, droppedRecords);
}

uint8_t recordUploadLimitForCurrentCycle() {
  // Dua record hanya pada setiap dua slot UTC. Rata-rata 1,5 record/siklus
  // memberi ruang terhadap batas datapoint harian sambil menguras backlog.
  return cycleAbsoluteSlotValid && (cycleAbsoluteSlot % 2ULL) == 0
             ? MAX_RECORD_UPLOADS_PER_CYCLE
             : 1;
}

void finishUploadWorkForCycle(const char *reason) {
  if (cycleUploaded) {
    return;
  }
  cycleUploaded = true;
  cycleUploadedAt = millis();
  publishOfflineQueueDiagnostics();
  Serial.printf("Upload siklus selesai (%s); %u record dikonfirmasi, "
                "antrean tersisa %lu; jendela OTA %lu ms.\n",
                reason, recordsUploadedThisCycle,
                static_cast<unsigned long>(offlineQueue.ready()
                                               ? offlineQueue.count()
                                               : 0),
                static_cast<unsigned long>(OTA_LISTEN_WINDOW_MS));
}

void beginOfflineRecordUpload(const tide::MeasurementRecord &record) {
  pendingUploadSequence = record.sequence;
  pendingUploadAcknowledged = false;
  sendRecordToBlynk(record);
  offlineUploadState = OfflineUploadState::WAITING_TO_REQUEST_ACK;
  uploadStateChangedAt = millis();

  Serial.printf("Record #%lu dikirim dengan timestamp %s; menunggu ACK V21.\n",
                static_cast<unsigned long>(record.sequence),
                (record.flags & tide::RECORD_TIME_VALID) != 0 ? "asli"
                                                               : "server");
}

void serviceOfflineUpload() {
  if (cycleUploaded || !Blynk.connected() || !currentCycleRecordReady) {
    return;
  }

  retryPersistCurrentMeasurement();
  updateCurrentRecordTimestampIfPossible();

  if (!offlineQueue.ready() || !currentCycleRecordQueued) {
    sendRecordToBlynk(currentCycleRecord);
    finishUploadWorkForCycle("record RAM dikirim best effort");
    return;
  }

  if (offlineUploadState == OfflineUploadState::WAITING_TO_REQUEST_ACK) {
    if (millis() - uploadStateChangedAt >= UPLOAD_ACK_REQUEST_DELAY_MS) {
      v21SyncReceived = false;
      Blynk.syncVirtual(V21);
      offlineUploadState = OfflineUploadState::WAITING_FOR_ACK;
      uploadStateChangedAt = millis();
    }
    return;
  }

  if (offlineUploadState == OfflineUploadState::WAITING_FOR_ACK) {
    if (pendingUploadAcknowledged && v21SyncReceived) {
      if (!offlineQueue.pop(pendingUploadSequence)) {
        Serial.printf("ERROR: ACK record #%lu diterima tetapi antrean gagal "
                      "diperbarui. Record dipertahankan.\n",
                      static_cast<unsigned long>(pendingUploadSequence));
        offlineUploadState = OfflineUploadState::IDLE;
        finishUploadWorkForCycle("gagal memperbarui antrean");
        return;
      }

      Serial.printf("ACK record #%lu diterima; record dihapus dari antrean.\n",
                    static_cast<unsigned long>(pendingUploadSequence));
      ++recordsUploadedThisCycle;
      pendingUploadSequence = 0;
      pendingUploadAcknowledged = false;
      offlineUploadState = OfflineUploadState::IDLE;

      if (offlineQueue.count() == 0) {
        finishUploadWorkForCycle("antrean kosong");
      } else if (recordsUploadedThisCycle >=
                 recordUploadLimitForCurrentCycle()) {
        finishUploadWorkForCycle("batas replay per siklus tercapai");
      }
      return;
    }

    if (millis() - uploadStateChangedAt >= UPLOAD_ACK_TIMEOUT_MS) {
      Serial.printf("ACK V21 untuk record #%lu timeout; record tetap disimpan.\n",
                    static_cast<unsigned long>(pendingUploadSequence));
      offlineUploadState = OfflineUploadState::IDLE;
      pendingUploadSequence = 0;
      pendingUploadAcknowledged = false;
      finishUploadWorkForCycle("ACK timeout");
    }
    return;
  }

  if (recordsUploadedThisCycle >= recordUploadLimitForCurrentCycle()) {
    finishUploadWorkForCycle("batas replay per siklus tercapai");
    return;
  }

  tide::MeasurementRecord record = {};
  if (!offlineQueue.peek(record)) {
    if (offlineQueue.count() == 0) {
      finishUploadWorkForCycle("antrean kosong");
    } else if (offlineQueue.discardCorruptHead()) {
      Serial.println("Record head rusak dibuang; mencoba record berikutnya.");
    } else {
      Serial.println("ERROR: head antrean offline rusak/tidak dapat dibaca.");
      finishUploadWorkForCycle("antrean tidak dapat dibaca");
    }
    return;
  }
  beginOfflineRecordUpload(record);
}

uint8_t sht40Crc(const uint8_t *data, size_t length) {
  uint8_t crc = 0xFF;
  for (size_t i = 0; i < length; ++i) {
    crc ^= data[i];
    for (uint8_t bit = 0; bit < 8; ++bit) {
      crc = (crc & 0x80) ? static_cast<uint8_t>((crc << 1) ^ 0x31)
                         : static_cast<uint8_t>(crc << 1);
    }
  }
  return crc;
}

bool readSht40(float &temperatureC, float &humidityPercent) {
  // 0xFD: pengukuran suhu dan kelembapan dengan presisi tinggi.
  Wire.beginTransmission(SHT40_ADDRESS);
  Wire.write(0xFD);
  if (Wire.endTransmission() != 0) {
    return false;
  }

  delay(10);
  if (Wire.requestFrom(SHT40_ADDRESS, static_cast<uint8_t>(6)) != 6) {
    return false;
  }

  uint8_t data[6];
  for (uint8_t i = 0; i < sizeof(data); ++i) {
    data[i] = Wire.read();
  }
  if (sht40Crc(data, 2) != data[2] || sht40Crc(data + 3, 2) != data[5]) {
    return false;
  }

  const uint16_t rawTemperature =
      (static_cast<uint16_t>(data[0]) << 8) | data[1];
  const uint16_t rawHumidity =
      (static_cast<uint16_t>(data[3]) << 8) | data[4];
  temperatureC = -45.0f + 175.0f * rawTemperature / 65535.0f;
  humidityPercent = -6.0f + 125.0f * rawHumidity / 65535.0f;
  humidityPercent = constrain(humidityPercent, 0.0f, 100.0f);
  return true;
}

bool writeRegister16(uint8_t reg, uint16_t value) {
  Wire.beginTransmission(INA3221_ADDRESS);
  Wire.write(reg);
  Wire.write(static_cast<uint8_t>(value >> 8));
  Wire.write(static_cast<uint8_t>(value & 0xFF));
  return Wire.endTransmission() == 0;
}

bool readRegister16(uint8_t reg, uint16_t &value) {
  Wire.beginTransmission(INA3221_ADDRESS);
  Wire.write(reg);

  if (Wire.endTransmission(false) != 0) {
    return false;
  }

  if (Wire.requestFrom(INA3221_ADDRESS, static_cast<uint8_t>(2)) != 2) {
    return false;
  }

  value = (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
  return true;
}

bool readChannel1Voltage(float &voltage) {
  uint16_t rawRegister = 0;
  if (!readRegister16(REG_CH1_BUS_VOLTAGE, rawRegister)) {
    return false;
  }

  // Bit 15..3 berisi nilai tegangan dengan resolusi 8 mV/bit.
  const uint16_t rawVoltage = rawRegister >> 3;
  voltage = rawVoltage * 0.008f;
  return true;
}

bool readChannel3(float &voltage, float &current) {
  uint16_t rawBusRegister = 0;
  uint16_t rawShuntRegister = 0;
  if (!readRegister16(REG_CH3_BUS_VOLTAGE, rawBusRegister) ||
      !readRegister16(REG_CH3_SHUNT_VOLTAGE, rawShuntRegister)) {
    return false;
  }

  voltage = (rawBusRegister >> 3) * 0.008f;
  const int16_t rawShunt = static_cast<int16_t>(rawShuntRegister) >> 3;
  current = rawShunt * 0.000040f / CH3_SHUNT_RESISTANCE_OHM;
  return true;
}

bool readChannel2Voltage(float &voltage) {
  uint16_t rawBusRegister = 0;

  if (!readRegister16(REG_CH2_BUS_VOLTAGE, rawBusRegister)) {
    return false;
  }

  // Bus voltage: bit 15..3, resolusi 8 mV/bit.
  voltage = (rawBusRegister >> 3) * 0.008f;
  return true;
}

void incrementSaturated(uint16_t &value) {
  if (value < UINT16_MAX) {
    ++value;
  }
}

A02FrameResult readA02YYUWFrame(uint16_t &distanceMm) {
  while (Serial1.available() > 0) {
    const uint8_t data = Serial1.read();

    // Setiap frame valid selalu dimulai dengan 0xFF.
    if (a02FrameIndex == 0 && data != 0xFF) {
      continue;
    }

    a02Frame[a02FrameIndex++] = data;
    if (a02FrameIndex < sizeof(a02Frame)) {
      continue;
    }

    a02FrameIndex = 0;
    const uint8_t checksum =
        static_cast<uint8_t>(a02Frame[0] + a02Frame[1] + a02Frame[2]);
    if (checksum != a02Frame[3]) {
      return A02FrameResult::CHECKSUM_ERROR;
    }

    distanceMm =
        (static_cast<uint16_t>(a02Frame[1]) << 8) | a02Frame[2];
    if (distanceMm < 30 || distanceMm > 4500) {
      return A02FrameResult::RANGE_ERROR;
    }
    return A02FrameResult::VALID;
  }

  return A02FrameResult::NONE;
}

void sortUint16(uint16_t *values, uint8_t count) {
  for (uint8_t i = 1; i < count; ++i) {
    const uint16_t key = values[i];
    int16_t j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

void sortFloat(float *values, uint8_t count) {
  for (uint8_t i = 1; i < count; ++i) {
    const float key = values[i];
    int16_t j = i - 1;
    while (j >= 0 && values[j] > key) {
      values[j + 1] = values[j];
      --j;
    }
    values[j + 1] = key;
  }
}

float medianSorted(const uint16_t *values, uint8_t count) {
  if (count % 2 != 0) {
    return values[count / 2];
  }
  return (values[count / 2 - 1] + values[count / 2]) * 0.5f;
}

float medianSorted(const float *values, uint8_t count) {
  if (count % 2 != 0) {
    return values[count / 2];
  }
  return (values[count / 2 - 1] + values[count / 2]) * 0.5f;
}

void finalizeA02YYUWMeasurement() {
  latestAcquisitionDurationMs = millis() - a02AcquisitionStartedAt;
  measurementQuality = MeasurementQuality::INVALID;
  distanceValid = false;
  a02UsedSamples = 0;
  a02OutlierSamples = 0;
  latestDistanceMedianMm = NAN;
  latestDistanceMadMm = NAN;
  latestOutlierLimitMm = NAN;

  if (a02AcquiredSamples >= A02YYUW_MIN_VALID_SAMPLES) {
    sortUint16(a02Samples, a02AcquiredSamples);
    latestDistanceMedianMm =
        medianSorted(a02Samples, a02AcquiredSamples);

    float deviations[A02YYUW_TARGET_SAMPLES] = {};
    for (uint8_t i = 0; i < a02AcquiredSamples; ++i) {
      deviations[i] = fabsf(a02Samples[i] - latestDistanceMedianMm);
    }
    sortFloat(deviations, a02AcquiredSamples);
    latestDistanceMadMm = medianSorted(deviations, a02AcquiredSamples);
    latestOutlierLimitMm =
        max(A02YYUW_MIN_OUTLIER_LIMIT_MM,
            3.0f * 1.4826f * latestDistanceMadMm);

    uint16_t inliers[A02YYUW_TARGET_SAMPLES] = {};
    uint8_t inlierCount = 0;
    for (uint8_t i = 0; i < a02AcquiredSamples; ++i) {
      if (fabsf(a02Samples[i] - latestDistanceMedianMm) <=
          latestOutlierLimitMm) {
        inliers[inlierCount++] = a02Samples[i];
      }
    }
    a02OutlierSamples = a02AcquiredSamples - inlierCount;

    if (inlierCount >= A02YYUW_MIN_VALID_SAMPLES) {
      const uint8_t trimEachSide = inlierCount / 10;
      a02UsedSamples = inlierCount - (2 * trimEachSide);
      double sum = 0.0;
      for (uint8_t i = trimEachSide;
           i < inlierCount - trimEachSide; ++i) {
        sum += inliers[i];
      }

      latestDistanceMm = static_cast<uint16_t>(
          lround(sum / static_cast<double>(a02UsedSamples)));
      latestWaterLevelMm =
          static_cast<int32_t>(sensorHeightMm) - latestDistanceMm;
      distanceValid = true;

      const uint32_t candidateFrames =
          static_cast<uint32_t>(a02AcquiredSamples) + a02ChecksumErrors +
          a02RangeErrors;
      const bool protocolQualityGood =
          candidateFrames == 0 ||
          static_cast<uint32_t>(a02AcquiredSamples) * 100 >=
              candidateFrames * 90;
      measurementQuality =
          (a02AcquiredSamples == A02YYUW_TARGET_SAMPLES &&
           inlierCount >= 40 &&
           latestDistanceMadMm <= A02YYUW_GOOD_MAX_MAD_MM &&
           protocolQualityGood)
              ? MeasurementQuality::GOOD
              : MeasurementQuality::POOR;
    }
  }

  setA02YYUWPower(false);
  automaticMeasurementComplete = true;

  Serial.println("Hasil filter A02YYUW:");
  Serial.printf("  acquired=%u, used=%u, outlier=%u, checksum_error=%u, "
                "range_error=%u\n",
                a02AcquiredSamples, a02UsedSamples, a02OutlierSamples,
                a02ChecksumErrors, a02RangeErrors);
  if (distanceValid) {
    Serial.printf("  median=%.1f mm, MAD=%.1f mm, limit=%.1f mm\n",
                  latestDistanceMedianMm, latestDistanceMadMm,
                  latestOutlierLimitMm);
    Serial.printf("  filtered=%u mm, quality=%u, duration=%lu ms\n",
                  latestDistanceMm,
                  static_cast<uint8_t>(measurementQuality),
                  static_cast<unsigned long>(latestAcquisitionDurationMs));
  } else {
    Serial.printf("  INVALID: minimal %u sampel/inlier diperlukan.\n",
                  A02YYUW_MIN_VALID_SAMPLES);
  }
}

void startNormalMeasurementCycle() {
  cycleStartedAt = millis();
  normalCycleStarted = true;
  cycleAbsoluteSlotValid = false;
  captureCurrentAbsoluteSlot();
  cycleStartedWithAbsoluteTime = cycleAbsoluteSlotValid;
  automaticMeasurementComplete = false;
  otherMeasurementsComplete = false;
  cycleUploaded = false;
  currentCycleRecord = {};
  currentCycleRecordReady = false;
  currentCycleRecordQueued = false;
  currentCycleSequence = 0;
  recordsUploadedThisCycle = 0;
  pendingUploadSequence = 0;
  pendingUploadAcknowledged = false;
  offlineUploadState = OfflineUploadState::IDLE;
  edgentConnectTimeoutArmed = false;
  edgentConnectTimedOut = false;
  provisioningRequested = false;
  edgentConnectStartedAt = 0;
  blynkConnectedAt = Blynk.connected() ? millis() : 0;

  distanceValid = false;
  measurementQuality = MeasurementQuality::INVALID;
  a02DiscardedSamples = 0;
  a02AcquiredSamples = 0;
  a02ChecksumErrors = 0;
  a02RangeErrors = 0;
  a02UsedSamples = 0;
  a02OutlierSamples = 0;
  latestAcquisitionDurationMs = 0;
  resetA02FrameParser();
  setA02YYUWPower(true);
  Serial.println("Siklus 5 menit dimulai: mengukur sebelum koneksi cloud.");
}

void serviceAutomaticA02YYUWMeasurement() {
  if (automaticMeasurementComplete || !a02yyuwPowerEnabled) {
    return;
  }

  if (!a02yyuwWarmupComplete) {
    if (millis() - a02yyuwPowerOnAt < A02YYUW_WARMUP_MS) {
      return;
    }
    while (Serial1.available() > 0) {
      Serial1.read();
    }
    resetA02FrameParser();
    a02yyuwWarmupComplete = true;
    a02AcquisitionStartedAt = millis();
    Serial.printf("A02YYUW warm-up selesai; buang %u frame lalu ambil %u "
                  "sampel processed-mode.\n",
                  A02YYUW_DISCARD_SAMPLES, A02YYUW_TARGET_SAMPLES);
  }

  for (uint8_t processedFrames = 0; processedFrames < 64; ++processedFrames) {
    uint16_t distanceMm = 0;
    const A02FrameResult result = readA02YYUWFrame(distanceMm);
    if (result == A02FrameResult::NONE) {
      break;
    }

    if (a02DiscardedSamples < A02YYUW_DISCARD_SAMPLES) {
      if (result == A02FrameResult::VALID) {
        ++a02DiscardedSamples;
      }
      continue;
    }

    if (result == A02FrameResult::VALID) {
      if (a02AcquiredSamples < A02YYUW_TARGET_SAMPLES) {
        a02Samples[a02AcquiredSamples++] = distanceMm;
        if (a02AcquiredSamples % 10 == 0) {
          Serial.printf("A02YYUW: %u/%u sampel terkumpul.\n",
                        a02AcquiredSamples, A02YYUW_TARGET_SAMPLES);
        }
      }
    } else if (result == A02FrameResult::CHECKSUM_ERROR) {
      incrementSaturated(a02ChecksumErrors);
    } else if (result == A02FrameResult::RANGE_ERROR) {
      incrementSaturated(a02RangeErrors);
    }
  }

  if (a02AcquiredSamples >= A02YYUW_TARGET_SAMPLES ||
      millis() - a02AcquisitionStartedAt >=
          A02YYUW_ACQUISITION_TIMEOUT_MS) {
    finalizeA02YYUWMeasurement();
  }
}

void serviceManualA02YYUWTest() {
  if (!automaticMeasurementComplete || !a02yyuwPowerEnabled) {
    return;
  }
  if (!a02yyuwWarmupComplete) {
    if (millis() - a02yyuwPowerOnAt < A02YYUW_WARMUP_MS) {
      return;
    }
    while (Serial1.available() > 0) {
      Serial1.read();
    }
    resetA02FrameParser();
    a02yyuwWarmupComplete = true;
    Serial.println("A02YYUW test manual siap membaca UART.");
  }

  uint16_t distanceMm = 0;
  if (readA02YYUWFrame(distanceMm) == A02FrameResult::VALID) {
    Serial.printf("A02YYUW test manual: %u mm\n", distanceMm);
  }
}

void performOtherCycleMeasurements() {
  solarVoltageValid = readChannel1Voltage(latestSolarVoltage);
  batteryValid = readChannel2Voltage(latestBatteryVoltage);
  system5VVoltageValid =
      readChannel3(latestSystem5VVoltage, latestSystemCurrent);
  sht40Valid = readSht40(latestTemperatureC, latestHumidityPercent);
  otherMeasurementsComplete = true;

  Serial.printf("Solar CH1: %s", solarVoltageValid ? "" : "ERROR ");
  if (solarVoltageValid) Serial.printf("%.3f V", latestSolarVoltage);
  Serial.println();
  Serial.printf("Baterai CH2: %s", batteryValid ? "" : "ERROR ");
  if (batteryValid) Serial.printf("%.3f V", latestBatteryVoltage);
  Serial.println();
  if (system5VVoltageValid) {
    Serial.printf("Sistem CH3: %.3f V, %.3f A\n", latestSystem5VVoltage,
                  latestSystemCurrent);
  } else {
    Serial.println("Sistem CH3: ERROR");
  }
  if (sht40Valid) {
    Serial.printf("SHT40: %.2f C, %.2f %%RH\n", latestTemperatureC,
                  latestHumidityPercent);
  } else {
    Serial.println("SHT40: ERROR");
  }
  persistCurrentMeasurement();
}

bool edgentIsConfigured() {
  return configStore.getFlag(CONFIG_FLAG_VALID);
}

bool otaIsPendingOrRunning() {
  return overTheAirURL.length() > 0 ||
         BlynkState::is(MODE_OTA_UPGRADE);
}

bool edgentConnectDeadlineExpired() {
  return !deepSleepDisabled && edgentConnectTimeoutArmed && !cycleUploaded &&
         millis() - edgentConnectStartedAt >= EDGENT_CONNECT_TIMEOUT_MS;
}

void abortEdgentConnectionForThisCycle() {
  if (deepSleepDisabled || cycleUploaded || edgentConnectTimedOut ||
      provisioningRequested || otaIsPendingOrRunning() || g_buttonPressed) {
    return;
  }

  edgentConnectTimeoutArmed = false;
  edgentConnectTimedOut = true;
  Blynk.disconnect();
  WiFi.disconnect();
  if (BlynkState::is(MODE_CONNECTING_NET) ||
      BlynkState::is(MODE_CONNECTING_CLOUD) ||
      BlynkState::is(MODE_WAIT_CONFIG) ||
      BlynkState::is(MODE_CONFIGURING)) {
    // Membuat loop blocking Edgent keluar melalui pemeriksaan state internal.
    BlynkState::set(MODE_RUNNING);
  }
  Serial.printf("Deadline koneksi/upload Edgent %lu ms terlampaui.\n",
                static_cast<unsigned long>(EDGENT_CONNECT_TIMEOUT_MS));
}

void armEdgentConnectTimeout() {
  if (deepSleepDisabled || edgentConnectTimeoutArmed) {
    return;
  }
  edgentConnectTimeoutArmed = true;
  edgentConnectStartedAt = millis();
  edgentTimer.setTimeout(EDGENT_CONNECT_TIMEOUT_MS, []() {
    if (edgentConnectDeadlineExpired()) {
      abortEdgentConnectionForThisCycle();
    }
  });
}

void setDeepSleepDisabled(bool disabled) {
  if (deepSleepDisabled == disabled) {
    return;
  }

  deepSleepDisabled = disabled;
  if (appPreferences.putBool(STAY_AWAKE_KEY, disabled) != sizeof(bool)) {
    Serial.println("PERINGATAN: status V17 gagal disimpan ke NVS.");
  }
  if (disabled) {
    edgentConnectTimeoutArmed = false;
    edgentConnectTimedOut = false;
  }
  Serial.printf("V17 Stay Awake: %s (deep sleep %s).\n",
                disabled ? "ON" : "OFF", disabled ? "nonaktif" : "aktif");
}

void enterTimedDeepSleep() {
  if (deepSleepDisabled || otaIsPendingOrRunning() || g_buttonPressed) {
    return;
  }
  if (provisioningRequested &&
      (BlynkState::is(MODE_RESET_CONFIG) ||
       BlynkState::is(MODE_WAIT_CONFIG) ||
       BlynkState::is(MODE_CONFIGURING) ||
       BlynkState::is(MODE_SWITCH_TO_STA))) {
    return;
  }

  retryPersistCurrentMeasurement();
  setA02YYUWPower(false);
  const uint32_t activeDurationMs = millis() - cycleStartedAt;
  uint64_t sleepDurationUs = 0;
  time_t nextWakeEpoch = 0;
  const bool absoluteScheduleAvailable =
      calculateAbsoluteSleep(sleepDurationUs, nextWakeEpoch);

  if (absoluteScheduleAvailable) {
    char nextWakeTimestamp[32] = {};
    formatUtcTimestamp(nextWakeEpoch, nextWakeTimestamp,
                       sizeof(nextWakeTimestamp));
    Serial.printf("Siklus selesai dalam %lu ms; deep sleep %llu ms; "
                  "bangun pada %s.\n",
                  static_cast<unsigned long>(activeDurationMs),
                  static_cast<unsigned long long>(sleepDurationUs / 1000ULL),
                  nextWakeTimestamp);
  } else {
    const uint32_t fallbackSleepMs =
        activeDurationMs < MEASUREMENT_INTERVAL_MS
            ? MEASUREMENT_INTERVAL_MS - activeDurationMs
            : MINIMUM_SLEEP_MS;
    sleepDurationUs = static_cast<uint64_t>(fallbackSleepMs) * 1000ULL;
    Serial.printf("Siklus selesai dalam %lu ms; NTP belum tersedia; "
                  "deep sleep relatif %lu ms.\n",
                  static_cast<unsigned long>(activeDurationMs),
                  static_cast<unsigned long>(fallbackSleepMs));
  }

  Blynk.disconnect();
  WiFi.disconnect(true, false);
  WiFi.mode(WIFI_OFF);
  appPreferences.end();

  // Pertahankan D0 LOW selama deep sleep; rangkaian tetap memerlukan pulldown
  // eksternal pada base PN2222A sebagai kondisi aman saat reset/power-up.
  digitalWrite(A02YYUW_POWER_PIN, LOW);
  gpio_hold_en(static_cast<gpio_num_t>(A02YYUW_POWER_PIN));
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_timer_wakeup(sleepDurationUs);
  Serial.flush();
  esp_deep_sleep_start();
}
}  // namespace

BLYNK_CONNECTED() {
  blynkConnectedAt = millis();
  serviceNetworkTime();
  v6SyncReceived = false;
  v17SyncReceived = false;
  v21SyncReceived = false;
  // Ambil konfigurasi serta sequence ACK terakhir yang tersimpan di cloud.
  Blynk.syncVirtual(V6);
  Blynk.syncVirtual(V17);
  Blynk.syncVirtual(V21);
  // Kondisi fisik saat ini menjadi nilai awal switch V9.
  Blynk.virtualWrite(V9, a02yyuwPowerEnabled ? 1 : 0);
  // Informasikan nama access point yang sedang dipakai tanpa mengirim password.
  Blynk.virtualWrite(V18, WiFi.SSID());
}

BLYNK_WRITE(V9) {
  if (!automaticMeasurementComplete) {
    Serial.println("V9 diabaikan: akuisisi otomatis A02YYUW sedang berjalan.");
    Blynk.virtualWrite(V9, a02yyuwPowerEnabled ? 1 : 0);
    return;
  }
  setA02YYUWPower(param.asInt() != 0);
}

BLYNK_WRITE(V17) {
  const int requestedState = param.asInt();
  if (requestedState != 0 && requestedState != 1) {
    Serial.printf("Blynk V17 ditolak: %d (gunakan 0 atau 1).\n",
                  requestedState);
    Blynk.virtualWrite(V17, deepSleepDisabled ? 1 : 0);
    return;
  }
  v17SyncReceived = true;
  setDeepSleepDisabled(requestedState == 1);
}

BLYNK_WRITE(V21) {
  const int32_t cloudSequence = param.asLong();
  v21SyncReceived = true;
  lastCloudSequence = cloudSequence > 0
                          ? static_cast<uint32_t>(cloudSequence)
                          : 0;
  if (offlineUploadState == OfflineUploadState::WAITING_FOR_ACK &&
      lastCloudSequence == pendingUploadSequence) {
    pendingUploadAcknowledged = true;
  }
}

BLYNK_WRITE(V6) {
  const float requestedHeightM = param.asFloat();

  if (!isfinite(requestedHeightM) ||
      requestedHeightM < MIN_SENSOR_HEIGHT_M ||
      requestedHeightM > MAX_SENSOR_HEIGHT_M) {
    Serial.printf("Blynk V6 ditolak: %.3f m (rentang %.2f-%.2f m)\n",
                  requestedHeightM, MIN_SENSOR_HEIGHT_M,
                  MAX_SENSOR_HEIGHT_M);
    return;
  }
  v6SyncReceived = true;

  const uint32_t requestedHeightMm =
      static_cast<uint32_t>(requestedHeightM * 1000.0f + 0.5f);
  if (requestedHeightMm != sensorHeightMm) {
    sensorHeightMm = requestedHeightMm;
    appPreferences.putUInt("sensor_h_mm", sensorHeightMm);
  }
  if (distanceValid) {
    latestWaterLevelMm =
        static_cast<int32_t>(sensorHeightMm) - latestDistanceMm;
    if (currentCycleRecordReady &&
        (currentCycleRecord.flags & tide::RECORD_DISTANCE_VALID) != 0) {
      currentCycleRecord.waterLevelMm = latestWaterLevelMm;
      tide::OfflineQueue::finalizeRecord(currentCycleRecord);
      if (currentCycleRecordQueued &&
          !offlineQueue.replaceNewest(currentCycleRecord)) {
        Serial.println("PERINGATAN: koreksi datum record terbaru gagal disimpan.");
      }
    }
    if (cycleUploaded && Blynk.connected()) {
      // Koreksi V2 apabila balasan sync V6 datang setelah fallback upload.
      Blynk.virtualWrite(V2, latestWaterLevelMm / 1000.0f);
    }
  }
  Serial.printf("Tinggi referensi sensor diubah dari Blynk: %.3f m\n",
                sensorHeightMm / 1000.0f);
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Wire.begin(SDA, SCL);
  Wire.setClock(400000);

  // Siapkan latch LOW sebelum melepas hold agar tidak ada celah pin floating.
  pinMode(A02YYUW_POWER_PIN, OUTPUT);
  digitalWrite(A02YYUW_POWER_PIN, LOW);
  gpio_hold_dis(static_cast<gpio_num_t>(A02YYUW_POWER_PIN));
  gpio_deep_sleep_hold_dis();
  digitalWrite(A02YYUW_POWER_PIN, LOW);
  Serial1.begin(9600, SERIAL_8N1, A02YYUW_RX_PIN, A02YYUW_TX_PIN);

  appPreferences.begin("tidal", false);
  deepSleepDisabled = appPreferences.getBool(STAY_AWAKE_KEY, false);
  const uint32_t storedSensorHeightMm =
      appPreferences.getUInt("sensor_h_mm", DEFAULT_SENSOR_HEIGHT_MM);
  const uint32_t minimumSensorHeightMm =
      static_cast<uint32_t>(MIN_SENSOR_HEIGHT_M * 1000.0f);
  const uint32_t maximumSensorHeightMm =
      static_cast<uint32_t>(MAX_SENSOR_HEIGHT_M * 1000.0f);
  sensorHeightMm =
      storedSensorHeightMm >= minimumSensorHeightMm &&
              storedSensorHeightMm <= maximumSensorHeightMm
          ? storedSensorHeightMm
          : DEFAULT_SENSOR_HEIGHT_MM;

  if (offlineQueue.begin()) {
    Serial.printf("LittleFS siap: antrean offline %lu/%lu, dropped=%lu.\n",
                  static_cast<unsigned long>(offlineQueue.count()),
                  static_cast<unsigned long>(offlineQueue.capacity()),
                  static_cast<unsigned long>(offlineQueue.droppedRecords()));
  } else {
    Serial.println("ERROR: LittleFS gagal; backup offline tidak tersedia.");
  }

  BlynkEdgent.begin();
  edgentConfiguredAtBoot = edgentIsConfigured();
  provisioningRequested = !edgentConfiguredAtBoot;
  registerA02YYUWConsoleCommand();

  Serial.println("INA3221 - CH1 solar, CH2 baterai, CH3 sistem 5V");
  Serial.printf("I2C SDA: GPIO %d, SCL: GPIO %d\n", SDA, SCL);
  Serial.println("SHT40 menggunakan alamat I2C 0x44.");
  Serial.printf("A02YYUW TX -> D7/GPIO %d\n", A02YYUW_RX_PIN);
  Serial.printf("Kontrol load switch A02YYUW: D0/GPIO %d (HIGH=ON).\n",
                A02YYUW_POWER_PIN);
  Serial.println("Tes Serial: sensor on | sensor off | sensor status");
  Serial.println("Tes Blynk: switch V9 (0=OFF, 1=ON).");
  Serial.printf("Tinggi referensi sensor awal: %lu mm\n",
                static_cast<unsigned long>(sensorHeightMm));
  Serial.println("Provisioning Wi-Fi dan koneksi cloud dikelola Blynk.Edgent.");
  Serial.println("Jadwal UTC absolut 5 menit aktif setelah waktu NTP tersedia.");
  Serial.printf("V17 Stay Awake tersimpan: %s (deep sleep %s).\n",
                deepSleepDisabled ? "ON" : "OFF",
                deepSleepDisabled ? "nonaktif" : "aktif");

  if (!writeRegister16(REG_CONFIG, INA3221_CONFIG_ALL_CHANNELS_CONTINUOUS)) {
    Serial.println("ERROR: INA3221 tidak ditemukan pada alamat 0x40.");
    Serial.println("Periksa wiring dan pin alamat A0.");
  } else {
    Serial.println("INA3221 berhasil diinisialisasi.");
  }

  Wire.beginTransmission(SHT40_ADDRESS);
  if (Wire.endTransmission() != 0) {
    Serial.println("ERROR: SHT40 tidak ditemukan pada alamat 0x44.");
  } else {
    Serial.println("SHT40 berhasil ditemukan.");
  }

  if (edgentConfiguredAtBoot) {
    startNormalMeasurementCycle();
  } else {
    Serial.println("Edgent belum diprovisioning: sensor tetap OFF dan perangkat "
                   "tetap terjaga sampai provisioning selesai.");
  }
}

void loop() {
  serviceNetworkTime();

  // Provisioning pertama tidak dibatasi timeout dan tidak masuk deep sleep.
  if (!normalCycleStarted) {
    BlynkEdgent.run();
    if (edgentIsConfigured()) {
      Serial.println("Provisioning selesai; memulai siklus pengukuran pertama.");
      startNormalMeasurementCycle();
    }
    return;
  }

  // URL OTA sudah diterima sebelum state berubah ke MODE_OTA_UPGRADE. Keduanya
  // harus menahan perangkat tetap aktif sampai update selesai/restart.
  if (otaIsPendingOrRunning()) {
    if (a02yyuwPowerEnabled) {
      setA02YYUWPower(false);
    }
    BlynkEdgent.run();
    return;
  }

  // V17=1 menonaktifkan seluruh deadline yang normalnya berakhir dengan sleep.
  // Edgent tetap dilayani agar perangkat dapat menerima V17=0 dan perintah OTA.
  if (deepSleepDisabled) {
    edgentConnectTimeoutArmed = false;
    edgentConnectTimedOut = false;
  }

  serviceAutomaticA02YYUWMeasurement();
  serviceManualA02YYUWTest();

  if (automaticMeasurementComplete && !otherMeasurementsComplete) {
    performOtherCycleMeasurements();
  }

  if (!automaticMeasurementComplete) {
    // Hanya terjadi pada siklus pertama tepat setelah provisioning; koneksi
    // yang sudah terbentuk tetap dilayani selama akuisisi berlangsung.
    if (Blynk.connected()) {
      BlynkEdgent.run();
    }
    return;
  }

  if (BlynkState::is(MODE_RESET_CONFIG)) {
    provisioningRequested = true;
    BlynkEdgent.run();
    return;
  }

  if (edgentConnectDeadlineExpired()) {
    abortEdgentConnectionForThisCycle();
  }
  if (edgentConnectTimedOut) {
    enterTimedDeepSleep();
    return;
  }

  if (provisioningRequested &&
      (BlynkState::is(MODE_WAIT_CONFIG) ||
       BlynkState::is(MODE_CONFIGURING) ||
       BlynkState::is(MODE_SWITCH_TO_STA))) {
    BlynkEdgent.run();
    return;
  }

  if (BlynkState::is(MODE_ERROR)) {
    if (deepSleepDisabled) {
      // enterError() bawaan Edgent akan me-restart MCU. Dalam mode Stay Awake,
      // kembalikan state ke percobaan jaringan agar siklus ukur tetap berjalan.
      edgentConnectTimeoutArmed = false;
      edgentConnectTimedOut = false;
      Blynk.disconnect();
      WiFi.disconnect();
      BlynkState::set(MODE_CONNECTING_NET);
      return;
    }
    if (provisioningRequested) {
      // Kredensial baru gagal divalidasi: jangan tidur. Bersihkan konfigurasi
      // yang gagal lalu buka kembali portal Edgent untuk percobaan berikutnya.
      edgentConnectTimeoutArmed = false;
      edgentConnectTimedOut = false;
      BlynkState::set(MODE_RESET_CONFIG);
      return;
    }
    abortEdgentConnectionForThisCycle();
    enterTimedDeepSleep();
    return;
  }

  if (!provisioningRequested &&
      (BlynkState::is(MODE_WAIT_CONFIG) ||
       BlynkState::is(MODE_CONFIGURING) ||
       BlynkState::is(MODE_SWITCH_TO_STA))) {
    armEdgentConnectTimeout();
    BlynkEdgent.run();
    return;
  }

  // Tanpa deep sleep, mulai siklus pengukuran baru setiap lima menit tanpa
  // me-restart ESP32. Jalur provisioning di atas tetap memiliki prioritas.
  if (deepSleepDisabled && otherMeasurementsComplete &&
      stayAwakeCycleIsDue()) {
    Serial.println("V17 Stay Awake aktif: memulai slot 5 menit berikutnya.");
    startNormalMeasurementCycle();
    return;
  }

  if (cycleUploaded) {
    if (deepSleepDisabled) {
      BlynkEdgent.run();
    } else if (!Blynk.connected() ||
        millis() - cycleUploadedAt >= OTA_LISTEN_WINDOW_MS) {
      enterTimedDeepSleep();
    } else {
      BlynkEdgent.run();
    }
    return;
  }

  if (edgentConnectTimedOut) {
    enterTimedDeepSleep();
    return;
  }

  if (!Blynk.connected()) {
    armEdgentConnectTimeout();
  }
  BlynkEdgent.run();

  if (otaIsPendingOrRunning()) {
    return;
  }
  if (edgentConnectDeadlineExpired()) {
    abortEdgentConnectionForThisCycle();
  }
  if (edgentConnectTimedOut) {
    enterTimedDeepSleep();
    return;
  }

  if (Blynk.connected()) {
    if (blynkConnectedAt == 0) {
      blynkConnectedAt = millis();
    }
    // Upload setelah V6 dan V17 tersinkron, atau gunakan nilai NVS setelah
    // fallback apabila salah satu Datastream belum dibuat/tidak merespons.
    if ((v6SyncReceived && v17SyncReceived) ||
        millis() - blynkConnectedAt >= BLYNK_SYNC_FALLBACK_MS) {
      serviceOfflineUpload();
    }
  }
}
