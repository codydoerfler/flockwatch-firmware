#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_wifi.h>
#include <atomic>
#include "flockwatch_protocol.h"

// These builds allow one companion connection. Callback-to-loop state is
// atomic: NimBLE runs on its own task, WiFi's callback never calls this code.
static NimBLECharacteristic* fwDetectionChar = nullptr;
static NimBLECharacteristic* fwStatusChar = nullptr;
static std::atomic<uint16_t> fwPayloadLimit{0};
static std::atomic<bool> fwStatusPending{false};
static std::atomic<uint32_t> fwDetectionCount{0};
static std::atomic<uint32_t> fwPacketsPerSec{0};
enum class FwState { Scanning, CoexScanning, Error };
static std::atomic<FwState> fwState{FwState::Scanning};
static bool fwReady = false;
static uint32_t fwLastHeartbeat = 0;

static std::string flockwatchStatus() {
  const FwState state = fwState.load();
  return fwStatusJson(state == FwState::Error ? "error" :
                      state == FwState::CoexScanning ? "ble_coex_scanning" : "scanning",
                      millis() / 1000, fwPacketsPerSec.load(), fwDetectionCount.load());
}

class FlockWatchServerCallbacks : public NimBLEServerCallbacks {
  void onConnect(NimBLEServer*, ble_gap_conn_desc*) override {
    fwPayloadLimit = 20; // ATT default until the central exchanges MTU.
    fwStatusPending = true;
  }
  void onDisconnect(NimBLEServer*) override {
    fwPayloadLimit = 0;
  }
  void onMTUChange(uint16_t mtu, ble_gap_conn_desc*) override {
    fwPayloadLimit = mtu > 3 ? mtu - 3 : 0;
    fwStatusPending = true;
  }
};

class FlockWatchStatusCallbacks : public NimBLECharacteristicCallbacks {
  void onRead(NimBLECharacteristic* characteristic) override {
    characteristic->setValue(flockwatchStatus());
  }
  void onSubscribe(NimBLECharacteristic*, ble_gap_conn_desc*, uint16_t value) override {
    if (value & 1) fwStatusPending = true;
  }
};

static void flockwatchBegin() {
  // setup() calls this AFTER WiFi start; the central role reuses this host.
  NimBLEDevice::init("");
  NimBLEDevice::setMTU(517);
  NimBLEDevice::setPower(ESP_PWR_LVL_P3);
  std::string mac = NimBLEDevice::getAddress().toString();
  char name[sizeof("FlockWatch-FFFF")];
  snprintf(name, sizeof(name), "FlockWatch-%c%c%c%c",
           toupper(mac[12]), toupper(mac[13]), toupper(mac[15]), toupper(mac[16]));
  NimBLEDevice::setDeviceName(name);

  static FlockWatchServerCallbacks serverCallbacks;
  static FlockWatchStatusCallbacks statusCallbacks;
  NimBLEServer* server = NimBLEDevice::createServer();
  server->setCallbacks(&serverCallbacks, false);
  server->advertiseOnDisconnect(true);
  NimBLEService* service = server->createService(FW_SERVICE_UUID);
  fwDetectionChar = service->createCharacteristic(FW_DETECTION_UUID, NIMBLE_PROPERTY::NOTIFY);
  fwStatusChar = service->createCharacteristic(FW_STATUS_UUID,
                                               NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
  fwStatusChar->setCallbacks(&statusCallbacks);
  fwStatusChar->setValue(flockwatchStatus());
  service->start();

  // Name + flags fit the primary 31-byte advertisement. The 128-bit service
  // goes in scan response so the complete local name is never shortened.
  NimBLEAdvertisementData advertisement;
  advertisement.setFlags(0x06);
  advertisement.setName(name);
  NimBLEAdvertisementData response;
  response.setCompleteServices(NimBLEUUID(FW_SERVICE_UUID));
  NimBLEAdvertising* advertising = NimBLEDevice::getAdvertising();
  advertising->setScanResponse(true);
  advertising->setAdvertisementData(advertisement);
  advertising->setScanResponseData(response);
  fwReady = advertising->start();
  fwLastHeartbeat = millis();
  fwStatusPending = true;
  Serial.printf("[flockwatch] %s peripheral %s\n", name, fwReady ? "ready" : "advertising failed");
}

static void flockwatchDetection(uint32_t ts, const char* method, uint8_t confidence,
                               int8_t rssi, const char* mac, const char* detail) {
  fwDetectionCount.fetch_add(1); // counts hits even while disconnected
  const uint16_t budget = fwPayloadLimit.load();
  if (!fwDetectionChar || !budget || !fwDetectionChar->getSubscribedCount()) return;
  std::string payload = fwDetectionJson(ts, method, confidence, rssi, mac, detail, budget);
  if (!payload.empty()) fwDetectionChar->notify(payload);
}

static void flockwatchTick(bool stopped, bool bleScanning, uint32_t packets) {
  const uint32_t now = millis();
  static uint32_t lastRateAt = now;
  static uint32_t lastPackets = packets;
  const uint32_t elapsed = now - lastRateAt;
  if (elapsed >= 1000) {
    fwPacketsPerSec = (uint64_t)(packets - lastPackets) * 1000 / elapsed;
    lastPackets = packets;
    lastRateAt = now;
  }
  bool wifiActive = false;
  const bool wifiError = esp_wifi_get_promiscuous(&wifiActive) != ESP_OK || !wifiActive;
  const FwState next = stopped || wifiError || !fwReady ? FwState::Error :
                       bleScanning ? FwState::CoexScanning : FwState::Scanning;
  const bool changed = fwState.exchange(next) != next;
  const bool pending = fwStatusPending.exchange(false);
  if (changed || pending || now - fwLastHeartbeat >= 30000) {
    fwLastHeartbeat = now;
    if (!fwStatusChar) return;
    std::string payload = flockwatchStatus();
    fwStatusChar->setValue(payload);
    // No fragments or invalid JSON on legacy/default MTU connections. A fresh
    // status is sent after MTU negotiation/subscription and is always readable.
    if (payload.size() <= fwPayloadLimit.load()) fwStatusChar->notify(payload);
  }
}
