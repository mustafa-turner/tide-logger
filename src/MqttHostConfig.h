#pragma once

#include <cstddef>
#include <cstdint>

namespace tide {

// A runtime override is deliberately limited to an IPv4 address. This keeps
// commands from the Blynk Terminal unambiguous and rejects malformed targets.
inline bool isValidMqttIpv4(const char *value) {
  if (value == nullptr || *value == '\0') {
    return false;
  }

  uint16_t octets[4] = {};
  const char *cursor = value;
  for (uint8_t index = 0; index < 4; ++index) {
    if (*cursor < '0' || *cursor > '9') {
      return false;
    }

    uint16_t octet = 0;
    uint8_t digits = 0;
    while (*cursor >= '0' && *cursor <= '9') {
      if (++digits > 3) {
        return false;
      }
      octet = static_cast<uint16_t>(octet * 10 + (*cursor - '0'));
      if (octet > 255) {
        return false;
      }
      ++cursor;
    }
    octets[index] = octet;

    if (index < 3) {
      if (*cursor != '.') {
        return false;
      }
      ++cursor;
    } else if (*cursor != '\0') {
      return false;
    }
  }

  // Unspecified, multicast/reserved, and limited-broadcast addresses cannot
  // identify the unicast MQTT broker expected by this firmware.
  const bool all255 = octets[0] == 255 && octets[1] == 255 &&
                      octets[2] == 255 && octets[3] == 255;
  return octets[0] != 0 && octets[0] < 224 && !all255;
}

}  // namespace tide
