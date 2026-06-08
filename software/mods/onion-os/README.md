# Onion OS

Onion OS is the badge firmware target for linking a physical OnionDAO badge to
an attendee profile through the Onion server.

This is an ESP-IDF v5.5.x project with Arduino as a component, matching the
other badge firmware in this repo.

## Implemented

- Persistent per-badge hardware ID stored in NVS.
- Hardcoded WiFi for `CIC Guest` / `1nnovation`.
- Hardcoded Onion API server URL: `https://oniondao.dev`.
- Hardcoded MQTT broker URL: `mqtt://shortline.proxy.rlwy.net:20928`.
- Baked-in MQTT broker credentials for the `oniondao` user.
- HTTP badge handshake:
  - `POST /api/badge/handshake`
- MQTT badge bridge:
  - publishes `oniondao/badge/handshake`
  - subscribes to `oniondao/badge/{onionId}/handshake/accepted`
  - subscribes to `oniondao/badge/{onionId}/link/request`
  - subscribes to `oniondao/badge/{onionId}/transaction/request`
  - subscribes to `oniondao/badge/{onionId}/lua/request`
  - publishes link, transaction, and Lua responses.
- E-paper status and approval UI.
- Three-second boot splash generated from `main/logo.png`.
- Home menu showing linked username, Onion balance, script explorer, script sync,
  and profile refresh actions.
- SELECT/CANCEL approval controls through the TCA9534 button expander.
- HTTP fallback for link and transaction responses.
- Script manifest download and SPIFFS storage.
- Lua script execution through Espressif's Lua component.
- Server-pushed Lua script approval popup, install, and run flow.
- Partial-refresh e-paper rendering: a retained `GFXcanvas1` framebuffer diffs
  against the previous frame and uses `setPartialWindow` for small changes
  (~300–500 ms, no flash) and `setFullWindow` only when >75 % of the panel
  changes or every 30 partial updates (ghost clearing). Lua canvas frames go
  through the same path via bit-invert copy.
- MQTT large-payload reassembly: fragmented payloads (e.g. large Lua scripts)
  are accumulated across `MQTT_EVENT_DATA` events before being parsed, fixing
  the "Bad MQTT JSON" error on server-pushed scripts.
- Badge-owned Solana Ed25519 wallet generation.
- ATECC608B-backed seed wrapping and approval attestation.
- Solana transaction signing for server-built burn/transfer transactions.
- PSRAM-backed Lua script loading (scripts up to 192 KB).
- Lua key/value store (NVS), SHA-256, and username APIs for script state and
  authenticated pairing.
- Per-peer ESP-NOW LMK encryption and a 128-deep RX queue for real-time
  streaming scripts.
- Lua WiFi disconnect/reconnect with the 802.11 LR PHY for fixed-channel
  ESP-NOW range.
- Streaming-capable Sound module APIs: deep mic DMA buffering, capture stats,
  startup-transient discard, and amp unmute/drain handling.

## Solana Key Custody

The badge signs Solana transactions with a software Ed25519 key because the
ATECC608B does not provide Ed25519 signing. The Ed25519 seed is generated on the
ESP32-S3 and is never stored plaintext. It is encrypted in NVS with
XChaCha20-Poly1305 using a wrapping key derived from an ATECC608B HMAC slot.

Production badges must provision `ATECC_HMAC_SLOT` (`10` in the firmware) with a
non-readable HMAC-capable secret. If that slot is empty, readable, locked with
incompatible config, or uses a different provisioning policy, wallet creation and
transaction approval will fail on the badge.

Link and transaction approvals include an `attestation` JSON object containing
the ATECC serial number, HMAC slot, purpose, nonce, subject, and HMAC. The
current `../landing-2026` server routes accept the extra fields but do not yet
verify them. Server-side verification needs the provisioning database or another
trusted record of the per-badge slot secret.

The CryptoAuthLib defaults are pinned for the badge hardware:

- SDA: GPIO 10
- SCL: GPIO 9
- ATECC address: `0xC0` 8-bit (`0x60` 7-bit)
- I2C speed: 100 kHz

## Updating Without Losing the Solana Key

The wrapped Solana wallet seed is stored in the `nvs` flash partition, not in
the Onion OS app partition. A normal firmware update preserves the key, cached
wallet address, hardware ID, link state, API key, MQTT settings, and script
manifest URL. A full flash erase deletes that state.

For normal updates, flash only the app:

```sh
idf.py -p /dev/cu.usbserial-10 flash
```

Or use the helper script, which does not erase flash:

```sh
scripts/build-flash.sh --port /dev/cu.usbserial-10
scripts/build-flash.sh --port /dev/cu.usbserial-10 --monitor
```

Do not run these commands on a badge whose wallet should be kept:

```sh
idf.py erase-flash
esptool.py erase_flash
```

Before a high-risk update, you can back up the NVS partition:

```sh
esptool.py --chip esp32s3 --port /dev/cu.usbserial-10 read_flash 0x9000 0x5000 onion-os-nvs-backup.bin
```

That backup is badge-specific because the wallet seed is wrapped with a key
derived from the badge's ATECC608B HMAC slot. Restoring it to another badge will
not make the wallet usable.

## Lua Scripts

The firmware downloads a JSON script manifest, stores scripts in SPIFFS, and can
execute Lua scripts through Espressif's `espressif/lua` ESP-IDF component.
The server can also push a registry script over MQTT on
`oniondao/badge/{onionId}/lua/request`; the badge shows an install approval
popup, stores approved code in SPIFFS, runs it, and responds on
`oniondao/badge/{onionId}/lua/response` plus the HTTP fallback
`POST /api/badge/lua-response`.

Installed scripts are stored as `/scripts_*.lua` and may be up to `192` KB;
they are loaded through a PSRAM buffer so large scripts do not depend on
contiguous internal heap. Downloaded image assets are
stored as `/images_*.pbm` or `/images_*.bmp`. The badge's home menu opens a
script explorer that lists both manifest-downloaded scripts and server-pushed
scripts from SPIFFS. In the script explorer, `SELECT` runs the highlighted
script, `LEFT` deletes it from the badge, `RIGHT` syncs scripts, and `CANCEL`
returns home.

The handshake routes currently only return `onionId` and `status`. After a badge
is linked, the firmware refreshes its owner profile by Onion ID with
`GET /api/badge/profile/{onionId}` and falls back to the cached `username` with
`GET /api/public/profile/{username}` for `currentOnionPoints` or
`currentOnionTokens`. If the server has `BADGE_API_KEY` configured, provision the
same key on the badge with the serial command `api-key <badge_api_key>`.

Scripts receive a small global `onion` table:

- `onion.log(message)` writes to serial and the e-paper status line.
- `onion.hardware_id()` returns the badge hardware ID.
- `onion.onion_id()` returns the current Onion ID, or `0` before handshake.
- `onion.wallet()` returns the configured Solana public key, if any.
- `onion.username()` returns the linked profile username, or `""` before the
  badge is linked.
- `onion.secure_random(count)` returns `count` random bytes (binary string)
  from the ATECC608A hardware RNG. `count` is optional and defaults to `32`
  (max `256`). Returns `nil` plus an error string if the secure element is
  unavailable.
- `onion.sha256(data)` returns the 32-byte binary SHA-256 digest of `data`
  (binary-safe input).
- `onion.kv_set(key, value)` stores a binary-safe value (1-`240` bytes) in a
  script-shared NVS namespace separate from the protected badge config. Keys
  are 1-`15` chars. Passing `nil` as the value deletes the key. Returns `true`,
  or `nil` plus an error string.
- `onion.kv_get(key)` returns the stored value, or `nil` when missing.
- `onion.kv_del(key)` deletes the key and returns `true`, or `nil` plus an
  error string.
- `onion.display_size()` returns `{ width = 264, height = 176 }` for the badge
  e-paper panel in landscape orientation.
- `onion.clear_display()` clears the e-paper display with a full ghost-clearing
  refresh and leaves Lua in control of the screen until the next button press.
- `onion.release_display()` returns screen ownership to Onion OS after a Lua
  script has drawn to the display.
- `onion.display_text(text, x, y, clear_or_options, font)` draws one text line.
  `x` defaults to `6`, `y` defaults to `22`, and clear defaults to true.
- `onion.display_lines(lines, x, y, line_height, clear_or_options)` draws a
  Lua table or newline-delimited string in one e-paper refresh.
- `onion.display_line(x0, y0, x1, y1, clear_or_options)` draws a black or white
  line. Clear defaults to false.
- `onion.display_rect(x, y, w, h, clear_or_options)` draws a rectangle. Pass
  `{ fill = true }` to fill it. Clear defaults to false.
- `onion.display_bitmap(name, x, y, clear)` draws a downloaded PBM or BMP image
  asset. Pass `-1` for `x` or `y` to center that axis. `clear` defaults to true.
- `onion.display_buffer()` returns the current Lua canvas as a raw binary string
  (5808 bytes, 264×176 1-bpp bitmap, MSB first, bit=1=black). Use this to capture
  and upload the badge display state to a server after drawing with the display API.
- `onion.images()` returns a table of downloaded PBM and BMP image asset names.
- `onion.buttons()` returns a table with the current badge button state:
  `left`, `down`, `up`, `right`, `select`, `cancel`, and integer `mask`.
- `onion.button_mask(name)` returns the integer mask for a badge button name.
- `onion.sleep(ms)` pauses the Lua script for up to 60 seconds.
- `onion.gpio_read(pin, mode)` reads an expansion GPIO and returns `0` or `1`.
- `onion.gpio_poll(pin, target, timeout_ms, interval_ms, mode)` polls an
  expansion GPIO until it equals `target`, returning `matched, value,
  elapsed_ms`.
- `onion.espnow_start(channel)` enables ESP-NOW. `channel` is optional; with
  WiFi connected, ESP-NOW uses the current AP channel.
- `onion.espnow_stop()` deinitializes ESP-NOW and clears queued packets.
- `onion.espnow_mac()` returns the badge WiFi station MAC address.
- `onion.espnow_info()` returns `{ started, mac, channel, sent, received,
  queued }`.
- `onion.espnow_send(payload, mac)` sends a 1-240 byte payload. `mac` is
  optional and defaults to broadcast (`ff:ff:ff:ff:ff:ff`).
- `onion.espnow_receive(timeout_ms)` returns a packet table or `nil` on
  timeout. Packet fields are `mac`, `payload`, `message`, `len`, `rssi`, and
  `received_at`. Up to `128` inbound packets are queued (oldest dropped on
  overflow), enough for a peer to keep streaming while this badge sits in an
  e-paper refresh.
- `onion.espnow_set_peer_key(mac, key)` enables radio-level AES-128 (LMK) for
  one unicast peer; `key` must be exactly `16` bytes, and `nil` reverts the
  peer to plaintext. Both sides must set the same key or unicast frames are
  silently dropped — confirm the encrypted path still answers and fall back if
  it goes quiet. Broadcast always stays plaintext. The firmware sets a fixed,
  public PMK so per-peer LMKs interoperate across badges; confidentiality
  rests entirely on the 16-byte LMK your script exchanges. Returns `true`, or
  `false` plus an error string.
- `onion.wifi_disconnect()` drops the AP association without turning the radio
  off (ESP-NOW keeps working) and switches the PHY to 802.11 LR for roughly
  2-4x badge-to-badge range. Use it before `espnow_start(channel)` to pin
  badges that roamed to different APs onto one agreed channel. Internet, MQTT,
  and script sync are unavailable until reconnect.
- `onion.wifi_reconnect()` restores the normal PHY and reconnects to the
  stored AP. The firmware also restores the PHY automatically on its own next
  connection attempt after the script exits.

### Room Check-ins

Onion OS listens for trusted room beacons while WiFi is connected. Beacons use
ESP-NOW on the room AP's channel and broadcast a compact `ONCHK1` advertisement
containing beacon ID, room, label, RSSI threshold, and nonce. When the received
RSSI is strong enough, the badge shows a native `CHECK IN?` prompt.

If the user presses SELECT, the badge sends an ESP-NOW approval packet back to
the beacon with hardware ID, Onion ID, username, wallet public key, RSSI, badge
MAC, and the advertisement nonce. The beacon relays that payload to
`POST /api/badge/checkin`; the server decides whether a workshop is active in
that room and awards attendance points idempotently. The beacon can send a
result packet back so the badge displays whether points were awarded.

Packet layout is shared with `software/mods/checkin-beacon`:

```c
magic = "ONCHK1", version = 1
type 1 advertise: header, beaconId[32], room[32], label[48], minRssi, nonce[8], sequence
type 2 approve:   header, beaconId[32], nonce[8], hardwareId[65], onionId,
                  username[32], wallet[48], rssi, approvedAt, badgeMac[6]
type 3 result:    header, beaconId[32], nonce[8], awarded, points, message[80]
```
- `onion.http_get(url, options)` performs an HTTPS GET (server certificates are
  verified against the bundled root CAs) and returns `{ status, body }`, or
  `nil` plus an error string. `options` is optional and may contain `headers`
  (a table of header name → value), `content_type`, and `timeout_ms` (default
  `10000`, max `30000`).
- `onion.http_post(url, body, options)` performs an HTTPS POST. `body` is the
  request body (defaults to `application/json`; override with
  `options.content_type`). Same return shape and `options` as `http_get`.
- `onion.mqtt_connected()` returns `true` when the badge's MQTT bridge is
  connected.
- `onion.mqtt_subscribe(topic, qos)` subscribes to a topic (with `+`/`#`
  wildcards) so matching messages are delivered to `mqtt_receive()`. `qos` is
  optional (default `1`). Returns `true`, or `nil` plus an error string.
  Subscriptions are cleared when the script finishes.
- `onion.mqtt_unsubscribe(topic)` removes a subscription.
- `onion.mqtt_publish(topic, payload, qos, retain)` publishes a message. `qos`
  (default `1`) and `retain` (default `false`) are optional. Returns `true`, or
  `nil` plus an error string.
- `onion.mqtt_receive(timeout_ms)` returns the next queued message table
  (`topic`, `payload`, `message`, `len`, `received_at`) or `nil` on timeout
  (max wait `30000` ms). Only messages matching an active `mqtt_subscribe()`
  filter are queued.
- `onion.mqtt_info()` returns `{ connected, uri, prefix, subscriptions,
  queued }`.

### Swappable modules (CC1101 Sub-GHz, Sound)

The badge has two side-port module slots (see
[`docs/MODULES.md`](../../../docs/MODULES.md)). The **CC1101 Sub-GHz radio** and
the **Sound module** (NS4168 amplifier + SPH0641 PDM mic) land on the same five
physical pins, so **only one may be active at a time** — call the matching
`*_end()` before starting another. Both modules are power-gated on `GPIO18`,
which `begin()` drives HIGH and `end()` releases.

The pins differ per board variant (**L1**, **L2**, **R** — they differ only in
which side port the module attaches to). Set the variant once over serial with
`module <L1|L2|R>` (stored in NVS, default `L1`); every `begin()` uses it as the
default. Individual pins can still be overridden in the `begin()` options table.

CC1101 Sub-GHz:

- `onion.subghz_begin(options)` powers and initialises the radio, returning
  `true` or `nil` plus an error string (it also fails detection if no CC1101 is
  present). `options` may set `freq` (MHz, default `433.92`), `modulation`
  (`"gfsk"` default, `"ook"`/`"ask"`, `"2fsk"`, `"msk"`), and pin overrides
  `sck`, `miso`, `mosi`, `cs`, `gdo0`, `power_pin`.
- `onion.subghz_transmit(payload)` sends a 1–61 byte packet. Returns `true` or
  `nil` plus an error string.
- `onion.subghz_receive(timeout_ms)` returns a packet table (`payload`,
  `message`, `len`, `rssi`, `rssi_dbm`) or `nil` on timeout (max `30000` ms).
- `onion.subghz_set_frequency(mhz)` retunes the radio while running.
- `onion.subghz_info()` returns `{ variant, active }`, plus `frequency`,
  `version`, and `partnum` when the radio is running.
- `onion.subghz_end()` powers the radio down.

Sound — speaker (NS4168) and mic (SPH0641) are also mutually exclusive:

- `onion.sound_speaker_begin(options)` starts I2S output. `options` may set
  `sample_rate` (default `44100`) and pin overrides `bclk`, `ws`, `dout`,
  `ctrl`, `power_pin`. The amp gets a ~`100` ms unmute soft-start and ~`31` ms
  of silence priming so the first real samples are not clipped.
- `onion.sound_play_tone(freq_hz, duration_ms, volume)` plays a sine tone.
  `duration_ms` defaults to `200` (max `10000`); `volume` is `0.0`–`1.0`
  (default `0.6`).
- `onion.sound_play(pcm)` plays raw signed 16-bit little-endian mono PCM (capped
  at 64 KB per call), blocking until written, and returns the number of bytes
  written.
- `onion.sound_speaker_end()` drains the pipeline (~`125` ms of silence so the
  tail plays out) and stops the amplifier.
- `onion.sound_mic_begin(options)` starts PDM capture. `options` may set
  `sample_rate` (default `16000`), `dma_desc`/`dma_frame` (max `64`/`2046`) to
  deepen the capture buffer (driver default is ~90 ms; streaming scripts that
  decode or encode between reads want e.g. `dma_desc = 16, dma_frame = 512` ≈
  512 ms at 16 kHz), `discard_ms` (default `0`, max `1000`) to drop the mic's
  startup transient, and pin overrides `clk`, `din`, `ws`, `ctrl`, `power_pin`.
- `onion.sound_mic_read(num_samples, timeout_ms)` returns up to `num_samples`
  (max `4096`) of raw signed 16-bit PCM as a binary string, plus a stats table
  (`samples`, `bytes`, `sample_rate`, `timeout`, `total_samples`,
  `total_bytes`, `timeouts`, `elapsed_ms`). `timeout_ms` is optional (default
  `1000`, max `5000`); a timed-out read still returns whatever arrived, with
  `timeout = true`.
- `onion.sound_mic_level(duration_ms)` samples for `duration_ms` (max `1000`)
  and returns `{ rms, peak, samples }` — handy for sound-activated scripts.
- `onion.sound_mic_end()` stops the microphone and returns `true` plus a
  totals table (`samples`, `bytes`, `duration_ms`, `timeouts`, `sample_rate`);
  the table is omitted if the mic was not running.

Modules left running are powered down automatically when the script finishes.

HTTP and MQTT both require WiFi; `http_get`/`http_post` connect automatically,
and the MQTT functions use the badge's shared MQTT bridge. Topics passed to the
`mqtt_*` functions are absolute — they are not prefixed with the badge topic
prefix. Queued MQTT payloads are capped at 512 bytes and topics at 128 bytes.

Display option tables support `clear`, `font`, `color`, and `background`.
Fonts are `"small"`, `"bold"`, and `"large"`. Colors are `"black"` and
`"white"`.

Lua draws go through the shared partial-refresh framebuffer: small changes (a
menu cursor) use a fast partial refresh of the dirty region, while the first
draw, large changes (>`75`% of the panel), and every 30th partial use a full
~`1.7` s ghost-clearing refresh. The panel is powered off after every partial
so the e-ink booster cannot glitch the Sound module's mic/speaker path.
`clear_display()` always does a full refresh.

GPIO `mode` is optional and may be `"input"`, `"floating"`, `"pullup"`, `"up"`,
`"pulldown"`, or `"down"`. Lua scripts can read the side-port GPIOs only:
`48, 47, 19, 42, 41, 40, 38, 39, 16, 15, 7, 6, 5, 4`.

ESP-NOW and the Onion OS WiFi/MQTT bridge share the ESP32-S3 station radio. If
the badge is already connected to WiFi, pass no channel to `espnow_start()` and
run nearby badges on the same AP channel. If WiFi is not connected, scripts may
pass a channel from `1` to `14`.

Expected manifest shape:

```json
{
  "scripts": [
    {
      "name": "hello.lua",
      "url": "https://example.com/hello.lua",
      "autorun": false
    }
  ],
  "images": [
    {
      "name": "poster.pbm",
      "url": "https://example.com/poster.pbm"
    },
    {
      "name": "sponsor.bmp",
      "url": "https://example.com/sponsor.bmp"
    }
  ]
}
```

Image assets should be sized for the 264x176 black-and-white e-paper panel.
PBM files may be binary `P4` or ascii `P1`; BMP files must be uncompressed
1/4/8/24/32-bit BMPs. Images larger than 192 KB or larger than the panel are
rejected.

Example Lua image script:

```lua
local ok, err = onion.display_bitmap("poster.pbm", -1, -1)
if not ok then onion.log(err) end
```

Example Lua e-paper script:

```lua
onion.display_lines({
  "Onion SDK",
  "E-paper ready",
  onion.espnow_mac()
}, 8, 28, 22, { font = "bold", clear = true })
onion.display_rect(4, 8, 256, 160, { clear = false })
```

Example Lua ESP-NOW script:

```lua
local ok, err = onion.espnow_start()
if not ok then
  onion.log(err)
  return
end

onion.espnow_send("hello from " .. onion.hardware_id())
local msg = onion.espnow_receive(5000)
if msg then
  onion.display_lines({
    "ESP-NOW RX",
    msg.mac,
    msg.message
  }, 8, 28, 20, true)
end
```

Local example scripts live in [`scripts/`](scripts/). `image-browser.lua`
enumerates downloaded PBM/BMP assets and lets the user move through them with
the badge buttons.

The inspected server currently does not expose a script manifest route, so set
the manifest URL explicitly.

## Configure

Optional compile-time defaults:

```sh
cp main/onion_config.h.example main/onion_config.h
```

Runtime serial commands are persisted in NVS:

```text
api-key <badge_api_key>
mqtt-auth [username] [password] [prefix]
scripts-url <manifest_url>
module <L1|L2|R>
wallet
keygen confirm
handshake
scripts
run <script_name.lua>
delete <script_name.lua>
state
help
```

Examples:

```text
api-key badge-api-secret
mqtt-auth badge badge-password oniondao
wallet
handshake
```

`keygen confirm` deletes any unlinked local wallet and creates a new wrapped key.
It refuses to rotate after the badge is linked, because the server may already
have migrated points to the previous Solana address.

## Build

From this directory:

```sh
idf.py set-target esp32s3
idf.py build
idf.py flash monitor
```

Or use the helper script to auto-detect the first attached ESP32-S3 badge:

```sh
scripts/build-flash.sh
scripts/build-flash.sh --monitor
```

Pass `--port /dev/cu.usbserial-10` if you want to flash a specific port.
The helper script refuses full flash erase so NVS-backed badge state is not
deleted accidentally.
Set `IDF_EXPORT=/path/to/esp-idf/export.sh` if your ESP-IDF install is not in a
common location.

The project uses the same 8 MB flash / OPI PSRAM defaults as the existing badge
mods.
