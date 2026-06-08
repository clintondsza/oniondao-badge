# ESP32-C3 Check-In Beacon

Minimal ESP32-C3 mini firmware for Onion OS check-ins.

The beacon repeatedly broadcasts the Onion OS packed ESP-NOW advertise packet.
Badges decide whether to prompt by comparing the received advertise RSSI with
`minRssi`. When a user approves on the badge, the badge sends a packed approve
packet to this beacon. The beacon POSTs it to the configured server and sends a
packed result packet back to the badge.

## Configure

Copy the local config template and fill in real values:

```sh
cp main/beacon_config.h.example main/beacon_config.h
```

Important placeholders:

- `BEACON_WIFI_SSID` / `BEACON_WIFI_PASSWORD`
- `BEACON_SERVER_URL`
- `BEACON_SERVER_CA_PEM` for verified HTTPS in production
- `BEACON_SHARED_SECRET`
- `BEACON_ID` / `BEACON_ROOM` / `BEACON_LABEL`
- `BEACON_ESPNOW_CHANNEL`
- `BEACON_RSSI_THRESHOLD_DBM`
- `BEACON_ADVERTISE_INTERVAL_MS`
- `BEACON_DEFAULT_POINTS`
- `BEACON_ESPNOW_PMK`

`BEACON_SERVER_URL` defaults to
`https://oniondao.dev/api/badge/checkin`. `BEACON_SHARED_SECRET` must match the
server's `BADGE_API_KEY` when that key is configured.

ESP-NOW shares the station radio. If Wi-Fi is connected, the beacon uses the
AP's channel; configure badges for that same channel or set the AP to
`BEACON_ESPNOW_CHANNEL`.

## Binary ESP-NOW Protocol

All structs are packed. The common header is:

```cpp
struct Header {
  char magic[6];     // {'O','N','C','H','K','1'}
  uint8_t version;   // 1
  uint8_t type;      // 1 advertise, 2 approve, 3 result
};
```

Advertise packet, broadcast repeatedly to `ff:ff:ff:ff:ff:ff`:

```cpp
struct AdvertisePacket {
  Header header;
  char beaconId[32];
  char room[32];
  char label[48];
  int8_t minRssi;
  uint8_t nonce[8];
  uint32_t sequence;
};
```

Approve packet from badge to beacon:

```cpp
struct ApprovePacket {
  Header header;
  char beaconId[32];
  uint8_t nonce[8];
  char hardwareId[65];
  uint64_t onionId;
  char username[32];
  char wallet[48];
  int8_t rssi;
  uint32_t approvedAt;
  uint8_t badgeMac[6];
};
```

Result packet from beacon to badge:

```cpp
struct ResultPacket {
  Header header;
  char beaconId[32];
  uint8_t nonce[8];
  uint8_t awarded;
  uint16_t points;
  char message[80];
};
```

The beacon accepts approve packets only when magic/version/type match, the
`beaconId` equals `BEACON_ID`, and the nonce matches the current advertise
nonce.

## Server POST

The beacon sends JSON to `BEACON_SERVER_URL` after receiving an approve packet:

```json
{
  "event": "badge_check_in",
  "beaconId": "beacon-c3-001",
  "configuredBeaconId": "beacon-c3-001",
  "room": "front-desk",
  "nonce": "0011223344556677",
  "hardwareId": "badge-hardware-id",
  "onionId": 100001,
  "onionIdString": "100001",
  "username": "alice",
  "wallet": "solana-wallet",
  "badgeReportedRssi": -52,
  "beaconReceivedRssi": -48,
  "approvedAt": 123456,
  "badgeMac": "AA:BB:CC:DD:EE:FF",
  "sourceMac": "AA:BB:CC:DD:EE:FF"
}
```

If `BEACON_SHARED_SECRET` is set, it is sent as:

```http
Authorization: Bearer <secret>
```

For result packets, the beacon reads server fields `status`, `ok`,
`onionsAwarded`, `awarded`, `points`, and `message`. `status: "checked_in"`
with positive `onionsAwarded` sends `awarded = 1`; no-op statuses such as
`already_checked_in`, `no_current_workshop`, `badge_not_linked`, and
`unknown_beacon` are sent back as `awarded = 0` with a short message.

## Build

Use ESP-IDF v5.5.x, matching the other firmware projects in this repo:

```sh
idf.py set-target esp32c3
idf.py build
idf.py flash monitor
```
