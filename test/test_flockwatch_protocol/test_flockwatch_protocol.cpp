#include <unity.h>
#include "../../flockwatch_protocol.h"

void setUp() {}
void tearDown() {}

void test_detection_contract_and_escape() {
  const auto json = fwDetectionJson(1234567, "wifi_ssid_pattern", 87, -61,
                                   "aa:bb:cc:dd:ee:ff", "name\"\\\n");
  TEST_ASSERT_EQUAL_STRING(
    "{\"v\":1,\"type\":\"detection\",\"ts_device_ms\":1234567,"
    "\"method\":\"wifi_ssid_pattern\",\"confidence\":87,\"rssi\":-61,"
    "\"mac\":\"AA:BB:CC:DD:EE:FF\",\"detail\":\"name\\\"\\\\\\u000a\"}", json.c_str());
}

void test_mtu_keeps_whole_event() {
  auto small = fwDetectionJson(UINT32_MAX, "wifi_probe_wildcard", 100, -127,
                              "AA:BB:CC:DD:EE:FF", std::string(80, '\n').c_str(), 182);
  TEST_ASSERT_FALSE(small.empty());
  TEST_ASSERT_TRUE(small.size() <= 182);
  TEST_ASSERT_NOT_NULL(strstr(small.c_str(), "\"detail\":\"\"}"));
  TEST_ASSERT_TRUE(fwDetectionJson(1, "ble_raven", 45, -90,
                                 "AA:BB:CC:DD:EE:FF", "", 20).empty());
}

void test_utf8_and_truncated_radio_names() {
  TEST_ASSERT_EQUAL_STRING("caf\xc3\xa9", fwJsonEscape("caf\xc3\xa9").c_str());
  TEST_ASSERT_EQUAL_STRING("\\ufffd", fwJsonEscape("\xe2").c_str());
  TEST_ASSERT_EQUAL_STRING("\\ufffd\\ufffd", fwJsonEscape("\xe2\x82").c_str());
  TEST_ASSERT_EQUAL_STRING("\\ufffd\\ufffd\\ufffd", fwJsonEscape("\xed\xa0\x80").c_str());
  TEST_ASSERT_EQUAL_STRING("\\ufffd\\ufffd", fwJsonEscape("\xc0\xaf").c_str());
}

void test_status_contract() {
  TEST_ASSERT_EQUAL_STRING(
    "{\"v\":1,\"type\":\"status\",\"state\":\"ble_coex_scanning\","
    "\"battery_pct\":null,\"uptime_s\":9421,\"fw_version\":\"1.0.0-flockwatch\","
    "\"packets_per_sec\":14,\"detections_since_boot\":3}",
    fwStatusJson("ble_coex_scanning", 9421, 14, 3).c_str());
  auto max = fwStatusJson("error", UINT32_MAX, UINT32_MAX, UINT32_MAX);
  TEST_ASSERT_TRUE(max.size() < 256);
  TEST_ASSERT_EQUAL_CHAR('}', max.back());
}

void test_companion_names_do_not_mask_real_signatures() {
  TEST_ASSERT_TRUE(fwIsCompanionName("FlockWatch-9BE2"));
  TEST_ASSERT_FALSE(fwIsCompanionName("Flock Camera net."));
  TEST_ASSERT_FALSE(fwIsCompanionName("FlockWatch-9BE2 camera"));
  TEST_ASSERT_FALSE(fwIsCompanionName("FlockWatch-ZZZZ"));
  TEST_ASSERT_FALSE(fwIsCompanionName("FlockWatch-"));
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_detection_contract_and_escape);
  RUN_TEST(test_mtu_keeps_whole_event);
  RUN_TEST(test_utf8_and_truncated_radio_names);
  RUN_TEST(test_status_contract);
  RUN_TEST(test_companion_names_do_not_mask_real_signatures);
  return UNITY_END();
}
