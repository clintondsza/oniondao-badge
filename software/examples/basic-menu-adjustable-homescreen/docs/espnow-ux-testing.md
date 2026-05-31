# ESPNow UX Testing Guide — Two Badges

This document walks through every ESPNow feature with two badges and describes
exactly what you should see at each step. Use it to confirm a build is working
correctly before shipping or merging.

---

## Setup

- Two badges flashed with the same firmware
- Both powered on (USB or LiPo)
- Both showing the home screen or menu
- Badges placed within 1–2 metres of each other for initial testing

---

## 1. Entering ESPNow

**On both badges:**
1. Press any button from the home screen to open the menu
2. Navigate to **8. ESPNow Beacon** and press SELECT

**Expected:**
- Screen refreshes (~1.5s) and shows the BEACON tab
- The top line reads `Me: NCB-XXXXXX` where `XXXXXX` is the last 3 bytes of that badge's MAC address in hex
- The two badges show **different** callsigns — this confirms hardware-derived identity is working
- Below the callsign, the full MAC address is displayed (e.g. `34:CD:B0:0E:12:4C`)
- `Sent: 0    Peers: 0` is shown — no activity yet

---

## 2. Tab Navigation

**On either badge:**
1. Press RIGHT — screen switches to PEERS tab
2. Press RIGHT again — screen switches to INBOX tab
3. Press LEFT — returns to PEERS, then LEFT again to BEACON

**Expected:**
- The active tab label at the top is shown with a filled (inverted) background
- Inactive tabs have an outlined box
- PEERS shows "No peers seen yet."
- INBOX shows "No messages yet."
- Navigation is instant on button press (triggers a full e-paper refresh, ~1.5s)

---

## 3. Sending a Beacon

**On Badge A:**
1. Make sure you are on the BEACON tab
2. Press SELECT

**Expected on Badge A:**
- `Sent:` count increments by 1
- Serial monitor (if connected) prints: `[espnow] sent beacon #1 as NCB-XXXXXX`

**Expected on Badge B:**
- Switch to the PEERS tab — Badge A's callsign appears in the list with an RSSI bar and proximity tier
- Switch to the INBOX tab — a new entry appears: `From: NCB-XXXXXX #1` with `[beacon]` as the message
- Back on the BEACON tab, `Peers: 1` is now shown and "Last recv:" shows Badge A's name and MAC

**Repeat from Badge B:**
- Press SELECT on Badge B — Badge B's callsign now appears in Badge A's PEERS tab
- Both badges should show `Peers: 1` and see each other in INBOX

---

## 4. RSSI Proximity

**Prerequisites:** Both badges have exchanged at least one beacon (Peers list is populated)

**Test A — Close range:**
1. Hold both badges 2–3 cm apart
2. Press SELECT on either badge to send a beacon
3. Switch to the PEERS tab on the receiving badge

**Expected:**
- RSSI reading: approximately **−30 to −45 dBm**
- Signal bar: **6–8 blocks** filled
- Tier label: **CLOSE**

**Test B — Mid range:**
1. Place badges ~3 metres apart
2. Send a beacon and check PEERS tab

**Expected:**
- RSSI reading: approximately **−55 to −65 dBm**
- Signal bar: **3–5 blocks** filled
- Tier label: **NEAR**

**Test C — Far range:**
1. Place badges at opposite ends of a room (10+ metres)
2. Send a beacon and check PEERS tab

**Expected:**
- RSSI reading: approximately **−70 to −85 dBm**
- Signal bar: **0–2 blocks** filled
- Tier label: **FAR**

> **Note:** RSSI only refreshes when a new beacon is received. After moving
> the badges, press SELECT to send a fresh beacon before reading the new value.
> The reading will not change on its own between sends.

---

## 5. Callsign Editor

**On Badge A:**
1. From the BEACON tab, press UP
2. The EDIT CALLSIGN screen opens showing the current callsign character by character
3. Press UP/DOWN to scroll through characters (A–Z, 0–9, space, -)
4. Press RIGHT to move to the next character position
5. Press LEFT to move back
6. Press SELECT to save

**Expected during editing:**
- The current cursor position is highlighted
- The character above and below the current selection are shown as context
- Other characters in the callsign remain unchanged as you scroll

**Expected after saving:**
- Returns to BEACON tab
- The `Me:` line shows the new callsign immediately
- The next beacon sent from Badge A will show the new callsign on Badge B's PEERS and INBOX tabs
- Power-cycling Badge A and re-entering ESPNow shows the same callsign (NVS persistence confirmed)

**Cancel test:**
1. Open the editor, make a change
2. Press CANCEL instead of SELECT

**Expected:**
- Returns to BEACON tab with the original callsign unchanged

---

## 6. Ping and Round-Trip Time (RTT)

**Prerequisites:** Both badges are on the ESPNow screen and have exchanged at least one beacon

> **Important:** Both badges must be in the ESPNow screen before pinging.
> If Badge B is on a different screen or powered off, Badge A's ping will
> display "RTT: waiting..." indefinitely until Badge B enters ESPNow and
> its auto-reply fires.

**On Badge A:**
1. Navigate to the PEERS tab
2. Scroll to Badge B using UP/DOWN
3. Press SELECT — a targeting menu appears with two options: PING and MESSAGE
4. Navigate to PING and press SELECT

**Expected on Badge A:**
- Returns to BEACON tab
- Shows `RTT: waiting...`
- Within 1–2 seconds, updates to `RTT: N ms` (typically 10–50 ms)

**Expected on Badge B:**
- A new INBOX entry appears from Badge A (the ping is also logged)
- No action required — the pong reply is automatic

**Verify RTT is live:**
- Send a second ping — RTT should update with a fresh measurement each time

---

## 7. Direct Messaging

**Prerequisites:** Both badges are on the ESPNow screen and have exchanged at least one beacon

**On Badge A:**
1. Navigate to the PEERS tab
2. Select Badge B and press SELECT → choose MESSAGE
3. The message composer opens — use UP/DOWN to select characters, RIGHT to advance, SELECT to send

**Expected on Badge A:**
- Returns to BEACON tab after sending
- Serial monitor prints: `[espnow] sent beacon ... TEXT`

**Expected on Badge B:**
- INBOX tab shows a new entry with Badge A's callsign and the message text
- The message is distinct from `[beacon]` entries — it shows the typed text

---

## 8. Encryption Indicator

**Prerequisites:** A ping or message has been sent between the two badges (unicast link established)

**On the sending badge:**
1. Navigate to the PEERS tab

**Expected:**
- The target badge shows a `[E]` label on the right side of its row
- This confirms the unicast link was established with AES-128 encryption (LMK set)
- Broadcast beacons do **not** show `[E]` — encryption is only for unicast

> **Full encryption verification requires a third badge** flashed with a
> different PMK value. Change `"NullCity-Badge-1"` in the firmware to any
> other 16-character string, flash Badge C, and confirm that Badge C's INBOX
> does not receive unicast messages between Badges A and B. Broadcast beacons
> will still appear on Badge C since broadcasts are unencrypted.

---

## 9. Exiting ESPNow

**On either badge:**
1. Press CANCEL from any ESPNow tab

**Expected:**
- Returns to the main menu
- ESPNow radio shuts down immediately (`esp_now_deinit()` + `WiFi.mode(WIFI_OFF)`)
- Serial monitor prints: `[espnow] shut down`
- Re-entering menu item 8 starts fresh — peer table cleared, counts reset to 0
- No background radio activity while on other screens

---

## Quick Pass/Fail Checklist

| Feature | Pass condition |
|---|---|
| Hardware identity | Both badges show different `NCB-XXXXXX` callsigns |
| Tab navigation | LEFT/RIGHT cycles BEACON → PEERS → INBOX |
| Beacon exchange | Pressing SELECT populates the other badge's PEERS and INBOX |
| RSSI at 1 inch | −30 to −45 dBm, 6–8 bars, CLOSE |
| RSSI at 3 metres | −55 to −65 dBm, 3–5 bars, NEAR |
| Callsign edit | Full callsign saves correctly; change persists after power cycle |
| Callsign cancel | Original callsign unchanged after pressing CANCEL |
| Ping RTT | Numeric ms value appears within 2 seconds |
| RTT waiting | Ping shows "waiting..." if second badge is not on ESPNow screen |
| Messaging | Typed text appears in recipient's INBOX |
| Encryption `[E]` | Appears next to peer after first unicast exchange |
| Clean exit | CANCEL returns to menu; re-entry starts with zeroed state |
