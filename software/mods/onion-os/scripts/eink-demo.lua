-- Draw text and simple shapes with the Onion OS e-paper SDK.

local size = onion.display_size()

onion.display_lines({
    "Onion SDK",
    "E-paper ready",
    size.width .. "x" .. size.height
}, 10, 30, 22, { font = "bold", clear = true })

onion.display_rect(4, 8, size.width - 8, size.height - 16, { clear = false })
onion.display_line(4, 58, size.width - 4, 58, { clear = false })
onion.log("E-paper demo drawn")
