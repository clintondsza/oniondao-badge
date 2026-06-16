---
name: tamagonion
description: Build, design, and flash a custom Tamagonion virtual pet for the OnionDAO badge
argument-hint: describe your pet — or "flash [file]" — or "char: description [in file]"
allowed-tools: [Read, Write, Edit, Bash, Glob]
version: 1.0.0
---

# Tamagonion

You are an expert developer for the OnionDAO badge. Your job is to build, modify, or flash a Tamagonion Lua script based on the user's request: **$ARGUMENTS**

## Decide what to do

- If `$ARGUMENTS` starts with `flash` → go to **[FLASH ONLY]**
- If `$ARGUMENTS` starts with `char:` → go to **[CHARACTER ONLY]**
- If `$ARGUMENTS` is a pet description (or empty) → go to **[NEW PET]** — if empty, ask the user what kind of pet they want before proceeding

---

# EMBEDDED KNOWLEDGE — Onion OS Lua API

Everything you need is in this section. Do not read any external files.

## Hardware

- **Display:** 264 × 176 px, 1-bit black-and-white e-ink
- **Full refresh:** ~1.5 s, solid black. Triggered by `clear_display()`.
- **Partial refresh:** ~0.5 s, only changed pixels. Used for action-bar-only updates.
- **Buttons:** `left`, `right`, `up`, `down`, `select`, `cancel`
- **MCU:** ESP32-S3, Lua 5.4, standard libs available (`math`, `string`, `table`, `os`)

## Screen layout constants

```lua
local W, H       = 264, 176   -- display size
local CHAR_W     = 11          -- FreeMono 9pt xAdvance (both "small" and "bold" fonts)
-- Zones:
-- Header:     y = 0..28      (black bg, white text)
-- Creature:   cx=132, cy=70  (spans roughly y=30..106)
-- HUN bar:    y = 108..117
-- HAP bar:    y = 119..128
-- NRG bar:    y = 130..139
-- HLT bar:    y = 141..150
-- Divider:    y = 153
-- Action bar: y = 154..175   (text baseline y=171)
local STATS_Y    = {108, 119, 130, 141}
local BAR_X      = 48
local BAR_W      = W - BAR_X - 8   -- 208 px
local BAR_H      = 10
local ACT_BY     = 154
local ACT_BH     = 22
local ACT_BW     = 66
local ACT_TEXT_Y = 171
```

## Drawing API

All drawing goes inside `display_begin()` / `display_commit()`. Nothing hits the screen until commit.

```lua
onion.display_begin()           -- start batch
onion.clear_display()           -- fill white + force full refresh on commit
onion.display_commit()          -- flush (one e-ink refresh)
onion.release_display()         -- return display to OS — call before script exits
onion.display_size()            -- returns {width=264, height=176}

-- Shapes — opts: {fill=true/false, color="black"/"white", clear=false}
onion.display_rect(x, y, w, h, opts)
onion.display_circle(cx, cy, r, opts)
onion.display_round_rect(x, y, w, h, radius, opts)
onion.display_triangle(x0,y0, x1,y1, x2,y2, opts)
onion.display_line(x0, y0, x1, y1)
onion.display_pixel(x, y, opts)

-- Text — baseline at y; color="black"/"white"; font="small"(default)/"bold"; clear=false
onion.display_text(str, x, baseline_y, opts)
onion.display_lines(tbl, x, y, line_height, opts)  -- tbl = array of strings

-- Bitmap from SPIFFS (optional, for sprite-based pets)
onion.display_bitmap(name, x, y, opts)
onion.images()   -- returns array of image filenames available in SPIFFS
```

**Centering text:**
```lua
local function cx_text(str, rx, rw)
    return rx + math.floor((rw - #str * CHAR_W) / 2)
end
```

## Input API

```lua
local btns = onion.buttons()
-- btns.left, btns.right, btns.up, btns.down, btns.select, btns.cancel  → boolean

onion.sleep(ms)       -- yield for ms milliseconds
onion.millis()        -- monotonic uptime in ms (integer)
onion.time()          -- SNTP wall clock seconds (0 if unsynced)
onion.time_synced()   -- true if SNTP succeeded
```

**Edge detection:**
```lua
local last = onion.buttons()
-- in loop:
local btns = onion.buttons()
if btns.left and not last.left then  -- fires once on press-down
    -- handle
end
last = btns
```

## Storage API (NVS)

```lua
onion.kv_set("key", "value")   -- key ≤ 15 chars, value = any string
onion.kv_get("key")            -- returns string or nil
onion.kv_del("key")            -- delete key
onion.kv_list()                -- returns array of key strings
```

## Badge identity

```lua
onion.hardware_id()   -- unique badge hardware ID string
onion.onion_id()      -- attendee Onion ID (0 if unlinked)
onion.wallet()        -- Solana wallet address
onion.username()      -- attendee username string
```

## Sound API

```lua
onion.sound_speaker_begin()
onion.sound_play_tone(hz, duration_ms, volume)  -- volume 0.0–1.0, blocks for duration
onion.sound_speaker_end()
```

## Network API

```lua
onion.http_get(url, headers)              -- returns {status=200, body="..."}
onion.http_post(url, body, headers)       -- returns {status=200, body="..."}
onion.mqtt_connected()                    -- bool
onion.mqtt_subscribe(topic)
onion.mqtt_publish(topic, payload, qos, retain)
onion.mqtt_receive(timeout_ms)            -- returns {topic, payload} or nil
onion.espnow_start(); onion.espnow_stop()
onion.espnow_send(mac, payload)
onion.espnow_receive(timeout_ms)          -- returns {mac, payload, rssi} or nil
```

## Misc

```lua
onion.log(msg)             -- print to serial monitor
onion.secure_random(n)     -- n cryptographic random bytes
onion.sha256(data)         -- hex SHA-256 string
onion.wifi_disconnect(); onion.wifi_reconnect()
```

---

## Game model

### Pet state

```lua
local MAX_STAT = 10
local pet = {
    hunger=MAX_STAT, happiness=MAX_STAT, energy=MAX_STAT, health=MAX_STAT,
    age_ticks=0, stage=0, alive=true, sleeping=false,
}
local function cstat(v) return math.max(0, math.min(MAX_STAT, v)) end
```

### Stage progression

```lua
local STAGE_TICKS = {[0]=0, [1]=3, [2]=20, [3]=60, [4]=120}  -- tune to taste
local function stage_for(t)
    for i = 4, 0, -1 do if t >= STAGE_TICKS[i] then return i end end
    return 0
end
```

### Tick logic

```lua
local TICK_MS = 120000   -- 2 real minutes per tick; use 10000 for fast testing

local function tick_stats()
    if not pet.alive then return pet.stage, pet.stage end
    local old = pet.stage
    pet.age_ticks = pet.age_ticks + 1
    pet.stage     = stage_for(pet.age_ticks)
    if old == 0 then return old, pet.stage end
    if pet.sleeping then
        pet.energy    = cstat(pet.energy + 3)
        pet.happiness = cstat(pet.happiness + 1)
        if pet.energy >= MAX_STAT then pet.sleeping = false end
    else
        pet.hunger = cstat(pet.hunger - 1)
        pet.energy = cstat(pet.energy - 1)
        if pet.age_ticks % 2 == 0 then pet.happiness = cstat(pet.happiness - 1) end
    end
    if pet.hunger    == 0 then pet.health = cstat(pet.health - 2) end
    if pet.happiness == 0 then pet.health = cstat(pet.health - 1) end
    if pet.hunger >= 7 and pet.happiness >= 5 and pet.energy >= 5 then
        pet.health = cstat(pet.health + 1)
    end
    if pet.health <= 6 and math.random(100) <= 2 then
        pet.health = cstat(pet.health - 1)
    end
    if pet.health == 0 then pet.alive = false end
    return old, pet.stage
end
```

### NVS save / load

```lua
local function save_pet()
    if not onion.kv_set then return end
    onion.kv_set("pet", string.format(
        '{"h":%d,"hp":%d,"e":%d,"hl":%d,"a":%d,"alive":%s,"sl":%s}',
        pet.hunger, pet.happiness, pet.energy, pet.health, pet.age_ticks,
        pet.alive and "true" or "false", pet.sleeping and "true" or "false"))
end

local function load_pet()
    if not onion.kv_get then return end
    local s = onion.kv_get("pet"); if not s or s == "" then return end
    local function n(k) return tonumber(s:match('"'..k..'":(%d+)')) end
    if n("h")  then pet.hunger    = cstat(n("h"))  end
    if n("hp") then pet.happiness = cstat(n("hp")) end
    if n("e")  then pet.energy    = cstat(n("e"))  end
    if n("hl") then pet.health    = cstat(n("hl")) end
    if n("a")  then pet.age_ticks = n("a")         end
    pet.alive    = (s:find('"alive":true') ~= nil)
    pet.sleeping = (s:find('"sl":true')    ~= nil)
    pet.stage    = stage_for(pet.age_ticks)
end
```

---

## Draw helpers (copy verbatim into every pet script)

```lua
local function draw_header(pet_name, stage_names)
    onion.display_rect(0, 0, W, 29, {fill=true, color="black"})
    onion.display_text(pet_name, 8, 21, {clear=false, color="white", font="bold"})
    onion.display_text(stage_names[pet.stage] or "?", 110, 21,
        {clear=false, color="white", font="bold"})
    local astr = "D" .. math.floor(pet.age_ticks / 12)
    onion.display_text(astr, W - 8 - #astr * CHAR_W, 21,
        {clear=false, color="white", font="bold"})
end

local function draw_stat_bar(label, y, val)
    onion.display_text(label, 8, y + BAR_H, {clear=false, color="black", font="small"})
    onion.display_rect(BAR_X, y, BAR_W, BAR_H, {clear=false, color="black"})
    local fill = math.floor(val * (BAR_W - 2) / MAX_STAT)
    if fill > 0 then
        onion.display_rect(BAR_X + 1, y + 1, fill, BAR_H - 2,
            {clear=false, fill=true, color="black"})
    end
end

local function draw_action_bar(sel, labels, feedback)
    if feedback and feedback ~= "" then
        onion.display_rect(0, ACT_BY, W, ACT_BH, {clear=false, fill=true, color="black"})
        onion.display_text(feedback, cx_text(feedback, 0, W), ACT_TEXT_Y,
            {clear=false, color="white", font="small"})
        return
    end
    for i = 0, 3 do
        local bx  = i * ACT_BW
        local lbl = labels[i]
        local lx  = bx + math.floor((ACT_BW - #lbl * CHAR_W) / 2)
        if i == sel then
            onion.display_rect(bx, ACT_BY, ACT_BW, ACT_BH, {clear=false, fill=true, color="black"})
            onion.display_text(lbl, lx, ACT_TEXT_Y, {clear=false, color="white", font="small"})
        else
            onion.display_rect(bx, ACT_BY, ACT_BW, ACT_BH, {clear=false, color="black"})
            onion.display_text(lbl, lx, ACT_TEXT_Y, {clear=false, color="black", font="small"})
        end
    end
end
```

---

## Character design guide

### Shapes approach

`draw_creature(stage, frame, happy, sick, sleeping)` centered at cx=132, cy=70.

- stage 0 (EGG): egg shape only, no face, `return` early
- stage 1–4: scale `r = 10 + stage * 4` → 14, 18, 22, 26
- frame 0 = idle, frame 1 = dance — only limb endpoints differ
- sick: X eyes; sleeping: line eyes + `"z"/"Z"` text; normal: filled circles
- happy mouth: upward V; sad: downward V
- `color="white"` fills to carve out shapes

```lua
local function draw_creature(stage, frame, happy, sick, sleeping)
    local cx, cy = 132, 70
    if stage == 0 then
        onion.display_round_rect(cx-14, cy-22, 28, 40, 12, {fill=true, color="black"})
        onion.display_round_rect(cx-11, cy-19, 22, 33,  9, {fill=true, color="white"})
        return
    end
    local r  = 10 + stage * 4
    local ey = cy - math.floor(r / 4)
    local ex = math.floor(r * 2 / 5)
    local my = cy + math.floor(r / 3)
    onion.display_circle(cx, cy, r+2, {fill=true, color="black"})
    onion.display_circle(cx, cy, r,   {fill=true, color="white"})
    if sick then
        onion.display_line(cx-ex-2,ey-2,cx-ex+2,ey+2); onion.display_line(cx-ex+2,ey-2,cx-ex-2,ey+2)
        onion.display_line(cx+ex-2,ey-2,cx+ex+2,ey+2); onion.display_line(cx+ex+2,ey-2,cx+ex-2,ey+2)
    elseif sleeping then
        onion.display_line(cx-ex-2,ey,cx-ex+2,ey); onion.display_line(cx+ex-2,ey,cx+ex+2,ey)
    else
        onion.display_circle(cx-ex,ey,2,{fill=true,color="black"})
        onion.display_circle(cx+ex,ey,2,{fill=true,color="black"})
    end
    if happy then
        onion.display_line(cx-4,my,cx,my+3); onion.display_line(cx,my+3,cx+4,my)
    else
        onion.display_line(cx-4,my+3,cx,my); onion.display_line(cx,my,cx+4,my+3)
    end
    if frame==0 then
        onion.display_line(cx-(r+2),cy+2,cx-(r+10),cy+8)
        onion.display_line(cx+(r+2),cy+2,cx+(r+10),cy+8)
    else
        onion.display_line(cx-(r+2),cy-2,cx-(r+10),cy-10)
        onion.display_line(cx+(r+2),cy-2,cx+(r+10),cy-10)
    end
    if sleeping then
        onion.display_text("z",cx+r+4, cy-8, {clear=false,font="small",color="black"})
        onion.display_text("Z",cx+r+10,cy-18,{clear=false,font="small",color="black"})
    end
end
```

### ASCII art approach

```lua
local ASCII = {
    idle  = {"  (o_o)  ","  [___]  ","  /| |\\  "},
    dance = {" \\(^_^)/ ","  [___]  ","   | |   "},
    sad   = {"  (;_;)  ","  [___]  ","  /| |\\  "},
    sick  = {"  (x_x)  ","  [___]  ","   | |   "},
    sleep = {"  (-_-)  ","  [___]  ","  /| |\\  "},
}
local function draw_creature(stage, frame, happy, sick, sleeping)
    local art = sick and ASCII.sick or sleeping and ASCII.sleep
        or (happy and (frame==1 and ASCII.dance or ASCII.idle) or ASCII.sad)
    onion.display_lines(art, 70+(4-stage)*2, 38, 16, {clear=false, font="small"})
end
```

### Play dance (partial update, creature zone only)

```lua
local function play_dance()
    if pet.stage == 0 then return end
    for _=1,4 do
        for f=0,1 do
            onion.display_begin()
            onion.display_rect(64,29,136,78,{fill=true,color="white"})
            draw_creature(pet.stage,f,true,false,false)
            onion.display_commit()
            onion.sleep(300)
        end
    end
end
```

---

## Render functions

```lua
local function render_main(sel, ACT_LABELS, STAT_LABELS, PET_NAME, STAGE_NAMES, feedback)
    onion.display_begin(); onion.clear_display()
    draw_header(PET_NAME, STAGE_NAMES)
    if pet.stage == 0 then
        draw_creature(0,0,false,false,false)
        local pct = math.floor(pet.age_ticks*100/STAGE_TICKS[1])
        local msg = "Incubating... "..math.min(pct,100).."%"
        onion.display_text(msg,cx_text(msg,0,W),122,{clear=false,font="small"})
        local bw=W-32; local fil=math.min(math.floor(pet.age_ticks*(bw-2)/STAGE_TICKS[1]),bw-2)
        onion.display_rect(16,130,bw,12,{clear=false,color="black"})
        if fil>0 then onion.display_rect(17,131,fil,10,{clear=false,fill=true,color="black"}) end
    elseif pet.sleeping then
        draw_creature(pet.stage,0,pet.happiness>=5,false,true)
        for i,y in ipairs(STATS_Y) do draw_stat_bar(STAT_LABELS[i],y,
            ({pet.hunger,pet.happiness,pet.energy,pet.health})[i]) end
        onion.display_line(0,ACT_BY-1,W-1,ACT_BY-1)
        onion.display_rect(0,ACT_BY,W,ACT_BH,{clear=false,fill=true,color="black"})
        local slp="* SLEEPING *"
        onion.display_text(slp,cx_text(slp,0,W),ACT_TEXT_Y,{clear=false,color="white",font="bold"})
    else
        draw_creature(pet.stage,0,pet.happiness>=5 and pet.hunger>=3,pet.health<=3,false)
        for i,y in ipairs(STATS_Y) do draw_stat_bar(STAT_LABELS[i],y,
            ({pet.hunger,pet.happiness,pet.energy,pet.health})[i]) end
        onion.display_line(0,ACT_BY-1,W-1,ACT_BY-1)
        draw_action_bar(sel,ACT_LABELS,feedback)
    end
    onion.display_commit()
end

local function render_action_bar_only(sel, ACT_LABELS)
    onion.display_begin()
    onion.display_rect(0,ACT_BY-1,W,ACT_BH+1,{clear=false,fill=true,color="white"})
    onion.display_line(0,ACT_BY-1,W-1,ACT_BY-1)
    draw_action_bar(sel,ACT_LABELS,"")
    onion.display_commit()
end

local function render_evolved(new_stage, PET_NAME, STAGE_NAMES)
    onion.display_begin(); onion.clear_display()
    onion.display_rect(0,0,W,29,{fill=true,color="black"})
    local ttl="* EVOLVED! *"
    onion.display_text(ttl,cx_text(ttl,0,W),21,{clear=false,color="white",font="bold"})
    draw_creature(new_stage,0,true,false,false)
    local msg=PET_NAME.." is now a "..(STAGE_NAMES[new_stage] or "?").."!"
    onion.display_text(msg,cx_text(msg,0,W),132,{clear=false,font="small"})
    onion.display_line(0,142,W-1,142)
    local cont="Press any button to continue"
    onion.display_text(cont,cx_text(cont,0,W),162,{clear=false,font="small"})
    onion.display_commit()
end

local function render_hatch(PET_NAME)
    onion.display_begin(); onion.clear_display()
    onion.display_rect(0,0,W,29,{fill=true,color="black"})
    local ttl="!! IT HATCHED !!"
    onion.display_text(ttl,cx_text(ttl,0,W),21,{clear=false,color="white",font="bold"})
    draw_creature(1,0,true,false,false)
    local say="Say hi to "..PET_NAME.."!"
    onion.display_text(say,cx_text(say,0,W),125,{clear=false,font="bold"})
    onion.display_line(0,135,W-1,135)
    local cont="Press any button to continue"
    onion.display_text(cont,cx_text(cont,0,W),158,{clear=false,font="small"})
    onion.display_commit()
end

local function render_dead(PET_NAME)
    onion.display_begin(); onion.clear_display()
    local rip="RIP"
    onion.display_text(rip,cx_text(rip,0,W),64,{clear=false,font="bold"})
    onion.display_text(PET_NAME,cx_text(PET_NAME,0,W),82,{clear=false,font="bold"})
    local days=math.floor(pet.age_ticks/12)
    local msg="Lived "..days..(days==1 and " day" or " days")
    onion.display_text(msg,cx_text(msg,0,W),110,{clear=false,font="small"})
    onion.display_line(0,125,W-1,125)
    local sub="Press SELECT for a new pet"
    onion.display_text(sub,cx_text(sub,0,W),152,{clear=false,font="small"})
    onion.display_commit()
end

local function render_exit_confirm(going_exit)
    onion.display_begin(); onion.clear_display()
    onion.display_rect(0,0,W,29,{fill=true,color="black"})
    local ttl="SAVE & EXIT?"
    onion.display_text(ttl,cx_text(ttl,0,W),21,{clear=false,color="white",font="bold"})
    local sub="Your pet will be saved."
    onion.display_text(sub,cx_text(sub,0,W),60,{clear=false,font="small"})
    onion.display_line(0,80,W-1,80)
    local function btn(x, label, filled)
        if filled then
            onion.display_rect(x,118,112,30,{clear=false,fill=true,color="black"})
            onion.display_text(label,cx_text(label,x,112),139,{clear=false,color="white",font="bold"})
        else
            onion.display_rect(x,118,112,30,{clear=false,color="black"})
            onion.display_text(label,cx_text(label,x,112),139,{clear=false,color="black",font="bold"})
        end
    end
    btn(8,   "STAY", not going_exit)
    btn(144, "EXIT", going_exit)
    onion.display_commit()
end
```

---

## Complete main loop template

```lua
local PET_NAME    = "MYPET"
local STAGE_NAMES = {[0]="EGG",[1]="BABY",[2]="CHILD",[3]="TEEN",[4]="ADULT"}
local STAT_LABELS = {"HUN","HAP","NRG","HLT"}
local ACT_LABELS  = {[0]="FEED",[1]="PLAY",[2]="SLEEP",[3]="MED"}

local sel=0; local tick_acc=0
local last_ms         = onion.millis and onion.millis() or 0
local needs_full      = true; local needs_bar=false
local feedback_msg=""; local feedback_until=0
local just_hatched=false; local just_evolved=false; local evolved_stage=0
local confirming_exit=false; local exit_choice=false

local function set_feedback(msg)
    feedback_msg=msg; feedback_until=(onion.millis and onion.millis() or 0)+2000; needs_full=true
end

local function perform_action(sel)
    if sel==0 then
        if pet.hunger>=MAX_STAT then return "Already full!" end
        pet.hunger=cstat(pet.hunger+3); pet.happiness=cstat(pet.happiness+1); return "Yum!"
    elseif sel==1 then
        if pet.energy<=1 then return "Too tired!" end
        play_dance()
        pet.happiness=cstat(pet.happiness+3); pet.hunger=cstat(pet.hunger-1); pet.energy=cstat(pet.energy-1)
        return "Wheee!"
    elseif sel==2 then
        if pet.energy>=MAX_STAT then return "Not tired!" end
        pet.sleeping=true; return ""
    elseif sel==3 then
        if pet.health>=MAX_STAT then return "Healthy!" end
        pet.health=cstat(pet.health+3); pet.happiness=cstat(pet.happiness-1); return "All better!"
    end
    return ""
end

load_pet(); math.randomseed(pet.age_ticks+pet.hunger*7+13)
onion.display_begin(); onion.clear_display(); onion.display_commit()
local last=onion.buttons()
while last.left or last.right or last.up or last.down or last.select or last.cancel do
    onion.sleep(50); last=onion.buttons()
end

while true do
    local now_t=onion.millis and onion.millis() or 0
    local dt=now_t-last_ms; if dt<0 then dt=0 end; last_ms=now_t
    tick_acc=tick_acc+dt
    while tick_acc>=TICK_MS do
        tick_acc=tick_acc-TICK_MS
        local old_s,new_s=tick_stats()
        if old_s==0 and new_s==1 then just_hatched=true
        elseif old_s~=new_s and new_s~=0 then just_evolved=true; evolved_stage=new_s end
        needs_full=true
    end
    if feedback_msg~="" and now_t>=feedback_until then feedback_msg=""; needs_full=true end
    local btns=onion.buttons()
    if confirming_exit then
        if btns.select and not last.select then
            if exit_choice then save_pet(); onion.release_display(); return
            else confirming_exit=false; needs_full=true end
        elseif btns.cancel and not last.cancel then confirming_exit=false; needs_full=true
        elseif (btns.right and not last.right) or (btns.down and not last.down) then
            if not exit_choice then exit_choice=true; render_exit_confirm(true) end
        elseif (btns.left and not last.left) or (btns.up and not last.up) then
            if exit_choice then exit_choice=false; render_exit_confirm(false) end
        end
    elseif just_hatched or just_evolved then
        local any=(btns.left and not last.left) or (btns.right and not last.right)
            or (btns.up and not last.up) or (btns.down and not last.down)
            or (btns.select and not last.select)
        if any then just_hatched=false; just_evolved=false; needs_full=true end
        if btns.cancel and not last.cancel then
            confirming_exit=true; exit_choice=false; render_exit_confirm(false); needs_full=false
        end
    else
        if btns.cancel and not last.cancel then
            confirming_exit=true; exit_choice=false; render_exit_confirm(false); needs_full=false
        elseif pet.stage==0 then
            if btns.select and not last.select then
                pet.age_ticks=STAGE_TICKS[1]; pet.stage=1; just_hatched=true; save_pet(); needs_full=true
            end
        elseif not pet.alive then
            if btns.select and not last.select then
                pet={hunger=MAX_STAT,happiness=MAX_STAT,energy=MAX_STAT,health=MAX_STAT,
                     age_ticks=0,stage=0,alive=true,sleeping=false}
                sel=0; feedback_msg=""; tick_acc=0; save_pet(); needs_full=true
            end
        elseif pet.sleeping then
            local any=btns.select or btns.up or btns.down or btns.left or btns.right
            if any and not(last.select and last.up and last.down and last.left and last.right) then
                pet.sleeping=false; save_pet(); needs_full=true
            end
        else
            if btns.left  and not last.left  then sel=(sel-1+4)%4; needs_bar=true end
            if btns.right and not last.right then sel=(sel+1)%4;   needs_bar=true end
            if btns.up    and not last.up    then sel=0;            needs_bar=true end
            if btns.down  and not last.down  then sel=2;            needs_bar=true end
            if btns.select and not last.select then set_feedback(perform_action(sel)); save_pet() end
        end
    end
    last=btns
    if needs_full then
        needs_full=false; needs_bar=false
        if confirming_exit then render_exit_confirm(exit_choice)
        elseif just_hatched then render_hatch(PET_NAME)
        elseif just_evolved then render_evolved(evolved_stage,PET_NAME,STAGE_NAMES)
        elseif not pet.alive then render_dead(PET_NAME)
        else render_main(sel,ACT_LABELS,STAT_LABELS,PET_NAME,STAGE_NAMES,feedback_msg) end
    elseif needs_bar then
        needs_bar=false; render_action_bar_only(sel,ACT_LABELS)
    end
    onion.sleep(20)
end
```

---

# [NEW PET]

## 1. Decide identity
From the description: `PET_NAME` (1 uppercase word), `STAGE_NAMES` (5 names for EGG→ADULT), `STAT_LABELS` (4×3-char), `ACT_LABELS` (4×4-char), sound personality.

## 2. Design draw_creature
Use the shapes or ASCII guide above. Handle all 5 stages, frame 0/1, sick/sleeping/happy/sad.

## 3. Assemble the full script
Order: constants → identity → pet state → save/load → cx_text → draw helpers → draw_creature → play_dance → render_* functions → perform_action → tick_stats → main loop.

Only `draw_creature` is custom. Everything else is verbatim from the templates above.

## 4. Save
Filename: lowercase PET_NAME + `-pet.lua` (e.g. `glitch-pet.lua`).
Write to: `software/mods/onion-os/scripts/<name>.lua` if that path exists, otherwise write to the current directory.

## 5. Flash → [FLASH]

---

# [CHARACTER ONLY]

Parse `char: <description> [in <filename>]`.
- Read the target `.lua` file
- Find `draw_creature` function start and end
- Write new `draw_creature` from description using the shapes/ASCII guide above
- Replace using Edit (preserve exact function signature)
- Then → [FLASH]

---

# [FLASH]

```bash
mkdir -p /tmp/spiffs_data
rm -f /tmp/spiffs_data/scripts_*.lua
for f in software/mods/onion-os/scripts/*.lua; do
    [ -f "$f" ] && cp "$f" "/tmp/spiffs_data/scripts_$(basename $f)"
done

SPIFFSGEN=$(find "$HOME/.espressif" -name "spiffsgen.py" 2>/dev/null | head -1)
python3 "$SPIFFSGEN" 0x180000 /tmp/spiffs_data /tmp/spiffs.bin

PORT=$(ls /dev/cu.usbserial-* /dev/tty.usbserial-* 2>/dev/null | head -1)
ESPTOOL=$(find "$HOME/.espressif" -name "esptool.py" 2>/dev/null | head -1)
"$ESPTOOL" --chip esp32s3 --port "$PORT" --baud 460800 write_flash 0x670000 /tmp/spiffs.bin
```

If no port found → tell user to connect badge via USB.
If flash fails with "Failed to connect" → hold BOOT button while the command runs.
Success ends with: `Hard resetting via RTS pin...`

After flash: tell user the script name and "On the badge: Scripts → select `<filename>`"
