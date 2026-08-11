#pragma once

#include <stdint.h>

namespace tide {

class TelemetryDeliveryTracker {
 public:
  bool begin(uint32_t sequence, uint16_t packetId) {
    if (sequence == 0 || packetId == 0 || pending_) return false;
    sequence_ = sequence;
    packetId_ = packetId;
    acknowledged_ = false;
    pending_ = true;
    return true;
  }

  bool acknowledge(uint16_t packetId) {
    if (!pending_ || packetId != packetId_) return false;
    acknowledged_ = true;
    return true;
  }

  bool takeAcknowledged(uint32_t &sequence) {
    if (!pending_ || !acknowledged_) return false;
    sequence = sequence_;
    clear();
    return true;
  }

  void clear() {
    sequence_ = 0;
    packetId_ = 0;
    pending_ = false;
    acknowledged_ = false;
  }

  bool pending() const { return pending_; }
  uint32_t sequence() const { return sequence_; }
  uint16_t packetId() const { return packetId_; }

 private:
  uint32_t sequence_ = 0;
  uint16_t packetId_ = 0;
  bool pending_ = false;
  bool acknowledged_ = false;
};

}  // namespace tide
