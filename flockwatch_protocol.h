#pragma once

#include <stdint.h>
#include <stdio.h>
#include <string>

// BLE_CONTRACT.md v1. Keep this wire format independent of serial/dashboard JSON.
static constexpr const char* FW_SERVICE_UUID = "6E400001-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* FW_DETECTION_UUID = "6E400003-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* FW_STATUS_UUID = "6E400004-B5A3-F393-E0A9-E50E24DCCA9E";
static constexpr const char* FW_VERSION = "1.0.0-flockwatch";

// Passive scanners only receive the primary advertisement (the service UUID
// is in scan response). Match the reserved full name, not a broad substring.
static bool fwIsCompanionName(const std::string& name) {
  constexpr size_t prefixLength = sizeof("FlockWatch-") - 1;
  if (name.size() != prefixLength + 4 ||
      name.compare(0, prefixLength, "FlockWatch-") != 0) return false;
  for (size_t i = prefixLength; i < name.size(); ++i)
    if (!((name[i] >= '0' && name[i] <= '9') ||
          (name[i] >= 'A' && name[i] <= 'F'))) return false;
  return true;
}

// SSIDs/advertised names are untrusted bytes, sometimes cut at the queue's
// 32-byte boundary. Preserve valid UTF-8 and replace malformed sequences.
static std::string fwJsonEscape(const char* text) {
  std::string out;
  const auto* p = reinterpret_cast<const unsigned char*>(text);
  while (*p) {
    unsigned char c = *p;
    if (c == '"' || c == '\\') {
      out += '\\'; out += static_cast<char>(c); ++p;
    } else if (c < 0x20) {
      char escaped[7];
      snprintf(escaped, sizeof(escaped), "\\u%04x", c);
      out += escaped; ++p;
    } else if (c < 0x80) {
      out += static_cast<char>(c); ++p;
    } else {
      unsigned n = (c >= 0xc2 && c <= 0xdf) ? 2 :
                   (c >= 0xe0 && c <= 0xef) ? 3 :
                   (c >= 0xf0 && c <= 0xf4) ? 4 : 0;
      bool valid = n != 0;
      for (unsigned i = 1; valid && i < n; ++i)
        valid = p[i] >= 0x80 && p[i] <= 0xbf;
      if (valid && ((c == 0xe0 && p[1] < 0xa0) ||
                    (c == 0xed && p[1] >= 0xa0) ||
                    (c == 0xf0 && p[1] < 0x90) ||
                    (c == 0xf4 && p[1] >= 0x90))) valid = false;
      if (valid) { out.append(reinterpret_cast<const char*>(p), n); p += n; }
      else { out += "\\ufffd"; ++p; }
    }
  }
  return out;
}

static std::string fwDetectionJson(uint32_t ts, const char* method,
                                  uint8_t confidence, int8_t rssi,
                                  const char* mac, const char* detail,
                                  size_t budget = 512) {
  std::string upperMac(mac);
  for (char& c : upperMac) if (c >= 'a' && c <= 'f') c -= 'a' - 'A';
  char base[256];
  snprintf(base, sizeof(base),
           "{\"v\":1,\"type\":\"detection\",\"ts_device_ms\":%lu,"
           "\"method\":\"%s\",\"confidence\":%u,\"rssi\":%d,"
           "\"mac\":\"%s\",\"detail\":\"",
           (unsigned long)ts, method, (unsigned)confidence, (int)rssi, upperMac.c_str());
  std::string result = std::string(base) + fwJsonEscape(detail) + "\"}";
  // Detail is optional; omit its contents on small MTUs, never split JSON.
  if (result.size() > budget) result = std::string(base) + "\"}";
  if (result.size() > budget) return {};
  return result;
}

static std::string fwStatusJson(const char* state, uint32_t uptime,
                               uint32_t pps, uint32_t detections) {
  char json[256];
  snprintf(json, sizeof(json),
           "{\"v\":1,\"type\":\"status\",\"state\":\"%s\","
           "\"battery_pct\":null,\"uptime_s\":%lu,\"fw_version\":\"%s\","
           "\"packets_per_sec\":%lu,\"detections_since_boot\":%lu}",
           state, (unsigned long)uptime, FW_VERSION,
           (unsigned long)pps, (unsigned long)detections);
  return json;
}
