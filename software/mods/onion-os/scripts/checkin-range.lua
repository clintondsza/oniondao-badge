-- Show whether this badge is close enough to an Onion check-in beacon.
-- CANCEL exits back to Onion OS.

local MAGIC = "ONCHK1"
local VERSION = 1
local TYPE_ADVERTISE = 1
local PACKET_LEN = 133
local STALE_MS = 4000
local DRAW_INTERVAL_MS = 1500

local function clip(text, max_len)
    text = tostring(text or "")
    if #text <= max_len then
        return text
    end
    if max_len <= 3 then
        return text:sub(1, max_len)
    end
    return text:sub(1, max_len - 3) .. "..."
end

local function cstring(payload, start_index, length)
    local out = {}
    for i = start_index, start_index + length - 1 do
        local b = payload:byte(i)
        if not b or b == 0 then
            break
        end
        out[#out + 1] = string.char(b)
    end
    return table.concat(out)
end

local function int8(value)
    if not value then
        return 0
    end
    if value >= 128 then
        return value - 256
    end
    return value
end

local function parse_advertise(payload)
    if type(payload) ~= "string" or #payload < PACKET_LEN then
        return nil
    end
    if payload:sub(1, 6) ~= MAGIC then
        return nil
    end
    if payload:byte(7) ~= VERSION or payload:byte(8) ~= TYPE_ADVERTISE then
        return nil
    end

    return {
        beacon_id = cstring(payload, 9, 32),
        room = cstring(payload, 41, 32),
        label = cstring(payload, 73, 48),
        min_rssi = int8(payload:byte(121))
    }
end

local function draw(status, beacon, rssi, seen_ago_ms, info)
    local title = status and "IN RANGE" or "OUT OF RANGE"
    local label = "No beacon seen"
    local room = ""
    local signal = "Signal: -- dBm"
    local threshold = "Need: -- dBm"

    if beacon then
        label = clip(beacon.label ~= "" and beacon.label or beacon.beacon_id, 28)
        room = beacon.room ~= "" and ("Room: " .. clip(beacon.room, 22)) or ""
        signal = "Signal: " .. tostring(rssi) .. " dBm"
        threshold = "Need: " .. tostring(beacon.min_rssi) .. " dBm"
    end

    local age = seen_ago_ms and ("Seen: " .. tostring(math.floor(seen_ago_ms / 1000)) .. "s ago") or "Seen: never"
    local channel = info and ("CH " .. tostring(info.channel) .. "  " .. info.mac) or ""

    onion.display_lines({
        "BEACON RANGE",
        title,
        label,
        room,
        signal,
        threshold,
        age,
        "CANCEL exits",
        channel
    }, 8, 20, 17, { clear = true, font = "bold" })
end

local ok, err = onion.espnow_start()
if not ok then
    onion.display_lines({
        "BEACON RANGE",
        "Radio failed",
        err or "ESP-NOW unavailable"
    }, 8, 32, 20, { clear = true, font = "bold" })
    return
end

local info = onion.espnow_info()
local last_beacon = nil
local last_rssi = nil
local last_seen = nil
local last_draw = 0

draw(false, nil, nil, nil, info)

while true do
    local buttons = onion.buttons()
    if buttons.cancel then
        onion.release_display()
        return
    end

    local msg = onion.espnow_receive(250)
    if msg and msg.payload then
        local beacon = parse_advertise(msg.payload)
        if beacon then
            last_beacon = beacon
            last_rssi = msg.rssi or 0
            last_seen = onion.millis()
        end
    end

    local now = onion.millis()
    if now - last_draw >= DRAW_INTERVAL_MS then
        local seen_ago = last_seen and (now - last_seen) or nil
        local fresh = seen_ago and seen_ago <= STALE_MS
        local in_range = fresh and last_beacon and last_rssi >= last_beacon.min_rssi
        draw(in_range, fresh and last_beacon or nil, fresh and last_rssi or nil, seen_ago, onion.espnow_info())
        last_draw = now
    end
end
