# FlockWatch BLE Interface Contract v1

This is the shared contract between the firmware (flockwatch-firmware, forked from
simeononsecurity/flock-you-esp32) and the iOS app (flockwatch-ios). Both sides must
implement this exactly so they interoperate without further coordination.

## BLE GATT Profile (peripheral = M5Atom Lite, central = iOS app)

- Service UUID: `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` (reuse Nordic UART Service UUID
  base for compatibility with generic BLE tooling during dev/debug)
- Characteristic (Notify): `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` — "Detection Event"
  - Device -> App, notify only
  - Payload: JSON, UTF-8, one detection per notification
- Characteristic (Read/Notify): `6E400004-B5A3-F393-E0A9-E50E24DCCA9E` — "Device Status"
  - Device -> App, read + notify, emitted every 30s (heartbeat) and on state change
  - Payload: JSON, UTF-8
- Device advertises with local name prefix `FlockWatch-` followed by last 4 hex chars of
  its MAC (e.g. `FlockWatch-9BE2`) so the app can filter scan results without relying
  solely on service UUID (some Android/iOS stacks miss 128-bit service UUIDs in adv
  packets intermittently).

## Detection Event JSON (Notify on characteristic 6E400003)

```json
{
  "v": 1,
  "type": "detection",
  "ts_device_ms": 1234567,
  "method": "wifi_oui_high | wifi_oui_mfr | wifi_fwdefault_mac | wifi_probe_wildcard |
             wifi_ssid_pattern | wifi_ie_fingerprint | ble_raven | ble_flock_gatt |
             ble_name_pattern | ble_dfu_target",
  "confidence": 87,
  "rssi": -61,
  "mac": "AA:BB:CC:DD:EE:FF",
  "detail": "free text, e.g. matched OUI or SSID string, optional, may be empty"
}
```

Notes:
- `ts_device_ms` is the ESP32's own millis()-based clock, NOT wall clock (device has no
  RTC/network time). The app must stamp its OWN wall-clock timestamp + GPS fix at the
  moment the BLE notification is received, and treat ts_device_ms only as a local
  ordering/dedup key within a single connection session.
- `confidence` is 0-100, matches the existing scoring in fy_detect.h.
- Dedup key for the app's local store: `mac + method + floor(ts_device_ms / 5000)` to
  collapse repeated notifications of the same ongoing detection into one event, but the
  exact window is up to app-side tuning; the firmware does NOT dedup before sending.

## Device Status JSON (Read + Notify on characteristic 6E400004)

```json
{
  "v": 1,
  "type": "status",
  "state": "scanning | ble_coex_scanning | error",
  "battery_pct": null,
  "uptime_s": 9421,
  "fw_version": "1.0.0-flockwatch",
  "packets_per_sec": 14,
  "detections_since_boot": 3
}
```

Note: M5Atom Lite has no onboard battery/fuel gauge — `battery_pct` is always `null`
unless a future hardware variant adds one. The app should hide/gray the battery UI
element when null rather than showing 0%.

## Background / auto-reconnect behavior (iOS app responsibility)

- App holds `bluetooth-central` background mode capability.
- On first pairing, app stores the peripheral's identifier (CBPeripheral.identifier) in
  UserDefaults/Keychain and uses `retrievePeripherals(withIdentifiers:)` on every launch
  and on `centralManagerDidUpdateState` to reconnect without user interaction.
- Scan filter: service UUID `6E400001-...` OR local name prefix `FlockWatch-` (belt and
  suspenders per note above).
- Auto-reconnect on disconnect: exponential backoff starting at 2s, capping at 60s,
  indefinite retry while app is running (foreground or background).
- "Mission critical" mode (Settings toggle, OFF by default):
  - Also requests "Always" location authorization.
  - Enables `allowsBackgroundLocationUpdates = true` + a low-frequency significant-
    location-change or region-monitoring trick purely as an iOS background wake
    assist (to get more frequent background execution windows for the BLE stack),
    NOT as the primary detection mechanism.
  - Must clearly disclose in-app that this trades battery life for reliability.

## Location + persistence (iOS app responsibility)

On each accepted (post-dedup) Detection Event:
- Capture current CLLocation (best available fix within e.g. 10s timeout; if none
  available within timeout, store event with null lat/lon rather than dropping it).
- Persist to local store (SQLite via GRDB or Core Data — implementer's choice) with
  columns: id, wall_clock_ts, device_ts_ms, lat, lon, horizontal_accuracy_m, method,
  confidence, rssi, mac, detail, raw_json, uploaded_to_osm (bool, default false; used
  by V2).

## V2 hook (do not build yet, just don't block on it)

`uploaded_to_osm` column and a `PendingUpload` concept in the data layer should exist
in the V1 schema so V2 (OSM OAuth + Overpass pull, review queue, push as
`man_made=surveillance`/`surveillance:type=ALPR` nodes) can be added without a schema
migration that touches existing rows beyond adding new tables/columns.
