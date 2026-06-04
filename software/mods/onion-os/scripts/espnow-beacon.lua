-- Broadcast a short ESP-NOW packet and show the first reply.

local ok, err = onion.espnow_start()
if not ok then
    onion.log(err)
    return
end

local info = onion.espnow_info()
onion.display_lines({
    "ESP-NOW",
    "MAC " .. info.mac,
    "CH " .. info.channel,
    "Waiting..."
}, 8, 26, 20, { font = "bold", clear = true })

local payload = "onion:" .. onion.hardware_id():sub(1, 12)
local sent, send_err = onion.espnow_send(payload)
if not sent then
    onion.log(send_err)
    return
end

local msg = onion.espnow_receive(5000)
if msg then
    onion.display_lines({
        "ESP-NOW RX",
        msg.mac,
        msg.message
    }, 8, 28, 20, { font = "bold", clear = true })
else
    onion.display_lines({
        "ESP-NOW",
        "Broadcast sent",
        "No reply"
    }, 8, 32, 22, { font = "bold", clear = true })
end
