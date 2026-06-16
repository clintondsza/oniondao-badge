-- tamagonion.lua — Tamagotchi-style virtual pet for Onion OS
--
-- Controls:
--   LEFT / RIGHT : cycle FEED / PLAY / SLEEP / MED action
--   UP           : shortcut → jump to FEED
--   DOWN         : shortcut → jump to SLEEP
--   SELECT       : perform action  /  hatch egg  /  new pet after death
--   CANCEL       : save and exit

local HAS_PRIMITIVES = (onion.display_circle ~= nil)
local HAS_KV         = (onion.kv_get ~= nil)
local HAS_MILLIS     = (onion.millis ~= nil)

-- ── Constants ─────────────────────────────────────────────────────────────────
local W, H     = 264, 176
local MAX_STAT = 10
local TICK_MS  = 120000   -- 2 minutes per stat tick

-- Layout: xAdvance = 11px for both FreeMono9pt7b and FreeMonoBold9pt7b
local CHAR_W = 11

-- Display zones (heights verified against font metrics):
--   Header:     y=0..28      (28px)
--   Creature:   cx=132, cy=70; adult spans y≈30..106
--   HUN bar:    y=108..117   (10px, text baseline y=118)
--   HAP bar:    y=119..128
--   NRG bar:    y=130..139
--   HLT bar:    y=141..150   (2px gap after = y=152)
--   Divider:    y=153
--   Action bar: y=154..175   (22px, text baseline y=171)
local STATS_Y    = {108, 119, 130, 141}
local BAR_X      = 48     -- label is 3 chars (33px) at x=8, bar starts after gap
local BAR_W      = W - BAR_X - 8   -- 208px wide, 8px right margin
local BAR_H      = 10
local ACT_BY     = 154    -- action bar top y
local ACT_BH     = 22     -- action bar height (154+22=176 = screen bottom)
local ACT_BW     = 66     -- W/4 = 66px per button
local ACT_TEXT_Y = 171    -- text baseline: ACT_BY+17

local STAGE_EGG   = 0
local STAGE_BABY  = 1
local STAGE_CHILD = 2
local STAGE_TEEN  = 3
local STAGE_ADULT = 4

-- Ticks to reach each stage (at 2 min/tick):
--   EGG→BABY: 6 min  BABY→CHILD: 40 min  CHILD→TEEN: 2 hr  TEEN→ADULT: 4 hr
local STAGE_MIN_TICKS = {[0]=0, [1]=3, [2]=20, [3]=60, [4]=120}
local STAGE_NAMES     = {[0]="EGG",[1]="BABY",[2]="CHILD",[3]="TEEN",[4]="ADULT"}
local PET_NAMES       = {"KERNEL","HEXBIT","DAEMON","PACKET"}

local ACTION_FEED   = 0
local ACTION_PLAY   = 1
local ACTION_SLEEP  = 2
local ACTION_MED    = 3
local ACTION_COUNT  = 4
local ACTION_LABELS = {[0]="FEED",[1]="PLAY",[2]="SLEEP",[3]="MED"}

-- ── Helpers ───────────────────────────────────────────────────────────────────
local function cstat(v)
    if v < 0 then return 0 end
    if v > MAX_STAT then return MAX_STAT end
    return v
end

local function stage_for(ticks)
    for i = 4, 0, -1 do
        if ticks >= STAGE_MIN_TICKS[i] then return i end
    end
    return STAGE_EGG
end

local function now_ms()
    if HAS_MILLIS then return onion.millis() end
    return 0
end

-- Center text horizontally within a region
local function text_cx(str, region_x, region_w)
    return region_x + math.floor((region_w - #str * CHAR_W) / 2)
end

-- ── Pet state ─────────────────────────────────────────────────────────────────
local pet = {
    hunger    = MAX_STAT,
    happiness = MAX_STAT,
    energy    = MAX_STAT,
    health    = MAX_STAT,
    age_ticks = 0,          -- start as EGG; egg hatches at tick 3
    stage     = STAGE_EGG,
    name_idx  = 1,
    alive     = true,
    sleeping  = false,
}

-- ── NVS persistence ───────────────────────────────────────────────────────────
local function save_pet()
    if not HAS_KV then return end
    local s = string.format(
        '{"h":%d,"hp":%d,"e":%d,"hl":%d,"a":%d,"n":%d,"alive":%s,"sl":%s}',
        pet.hunger, pet.happiness, pet.energy, pet.health,
        pet.age_ticks, pet.name_idx,
        pet.alive    and "true" or "false",
        pet.sleeping and "true" or "false"
    )
    onion.kv_set("pet", s)
end

local function load_pet()
    if not HAS_KV then return end
    local s = onion.kv_get("pet")
    if not s or s == "" then return end
    local h  = tonumber(s:match('"h":(%d+)'))
    local hp = tonumber(s:match('"hp":(%d+)'))
    local e  = tonumber(s:match('"e":(%d+)'))
    local hl = tonumber(s:match('"hl":(%d+)'))
    local a  = tonumber(s:match('"a":(%d+)'))
    local n  = tonumber(s:match('"n":(%d+)'))
    if h  then pet.hunger    = cstat(h)  end
    if hp then pet.happiness = cstat(hp) end
    if e  then pet.energy    = cstat(e)  end
    if hl then pet.health    = cstat(hl) end
    if a  then pet.age_ticks = a         end
    if n and n >= 1 and n <= #PET_NAMES then pet.name_idx = n end
    pet.alive    = (s:find('"alive":true') ~= nil)
    pet.sleeping = (s:find('"sl":true')    ~= nil)
    pet.stage    = stage_for(pet.age_ticks)
end

-- ── Drawing helpers ───────────────────────────────────────────────────────────

local function draw_onion_head(cx, cy, r, happy, sick, sleeping_eyes)
    onion.display_circle(cx, cy, r,   {fill=true, color="black"})
    onion.display_circle(cx, cy, r-2, {fill=true, color="white"})

    -- Onion bulb segmentation lines
    local tp = cy - r
    onion.display_triangle(cx-2, tp+2, cx+2, tp+2, cx, tp-3, {fill=true, color="black"})
    onion.display_line(cx-1, tp-3, cx-3, tp-7)
    onion.display_line(cx+1, tp-3, cx+3, tp-7)
    onion.display_line(cx,   cy-r+3, cx, cy+r-3)
    local sw = math.floor(r*5/8)
    local sh = math.floor(r/8)
    onion.display_line(cx-1, cy-r+4, cx-sw, cy+sh)
    onion.display_line(cx-sw, cy+sh, cx-2, cy+r-4)
    onion.display_line(cx+1, cy-r+4, cx+sw, cy+sh)
    onion.display_line(cx+sw, cy+sh, cx+2, cy+r-4)

    -- Eyes
    local ey = cy - math.floor(r/4)
    local ex = math.floor(r * 2 / 5)
    if sick then
        onion.display_line(cx-ex-2, ey-2, cx-ex+2, ey+2)
        onion.display_line(cx-ex+2, ey-2, cx-ex-2, ey+2)
        onion.display_line(cx+ex-2, ey-2, cx+ex+2, ey+2)
        onion.display_line(cx+ex+2, ey-2, cx+ex-2, ey+2)
    elseif sleeping_eyes then
        onion.display_line(cx-ex-2, ey, cx-ex+2, ey)
        onion.display_line(cx+ex-2, ey, cx+ex+2, ey)
    else
        onion.display_circle(cx-ex, ey, 2, {fill=true, color="black"})
        onion.display_circle(cx+ex, ey, 2, {fill=true, color="black"})
        onion.display_circle(cx-ex+1, ey-1, 1, {fill=true, color="white"})
        onion.display_circle(cx+ex+1, ey-1, 1, {fill=true, color="white"})
    end

    -- Mouth
    local my = cy + math.floor(r/3)
    if happy then
        onion.display_line(cx-3, my,   cx-1, my+2)
        onion.display_line(cx-1, my+2, cx+1, my+2)
        onion.display_line(cx+1, my+2, cx+3, my)
    else
        onion.display_line(cx-3, my+2, cx-1, my)
        onion.display_line(cx-1, my,   cx+1, my)
        onion.display_line(cx+1, my,   cx+3, my+2)
    end
end

local function draw_creature(cx, cy, frame)
    frame = frame or 0
    local happy  = (pet.happiness >= 5 and pet.hunger >= 3)
    local sick   = (pet.health <= 3 and pet.alive and pet.stage ~= STAGE_EGG)
    local asleep = pet.sleeping

    if pet.stage == STAGE_EGG then
        onion.display_round_rect(cx-14, cy-22, 28, 40, 12, {fill=true, color="black"})
        onion.display_round_rect(cx-11, cy-19, 22, 33,  9, {fill=true, color="white"})
        onion.display_line(cx-1, cy-18, cx+2, cy-12)
        onion.display_line(cx+2, cy-12, cx-1, cy-8)

    elseif pet.stage == STAGE_BABY then
        draw_onion_head(cx, cy, 16, happy, sick, asleep)
        if frame == 0 then
            onion.display_line(cx-16, cy+5,  cx-22, cy+2)
            onion.display_line(cx+16, cy+5,  cx+22, cy+2)
        else
            onion.display_line(cx-16, cy-2,  cx-20, cy-10)
            onion.display_line(cx+16, cy-2,  cx+20, cy-10)
        end
        if asleep then
            onion.display_text("z", cx+20, cy-6,  {clear=false, font="small", color="black"})
            onion.display_text("Z", cx+26, cy-16, {clear=false, font="small", color="black"})
        end

    elseif pet.stage == STAGE_CHILD then
        onion.display_circle(cx, cy+10, 14, {fill=true, color="black"})
        onion.display_circle(cx, cy+10, 11, {fill=true, color="white"})
        if frame == 0 then
            onion.display_line(cx-14, cy+10, cx-20, cy+6)
            onion.display_line(cx+14, cy+10, cx+20, cy+6)
        else
            onion.display_line(cx-14, cy+4,  cx-20, cy-6)
            onion.display_line(cx+14, cy+4,  cx+20, cy-6)
        end
        draw_onion_head(cx, cy-8, 11, happy, sick, asleep)
        if asleep then
            onion.display_text("z", cx+16, cy-14, {clear=false, font="small", color="black"})
            onion.display_text("Z", cx+22, cy-24, {clear=false, font="small", color="black"})
        end

    elseif pet.stage == STAGE_TEEN then
        onion.display_round_rect(cx-12, cy+2,  24, 22, 5, {fill=true, color="black"})
        onion.display_round_rect(cx-10, cy+4,  20, 18, 4, {fill=true, color="white"})
        onion.display_rect(cx-4, cy-5, 8, 9, {fill=true, color="white"})
        if frame == 0 then
            onion.display_line(cx-12, cy+8,  cx-20, cy+14)
            onion.display_line(cx+12, cy+8,  cx+20, cy+14)
        else
            onion.display_line(cx-12, cy+2,  cx-20, cy-8)
            onion.display_line(cx+12, cy+2,  cx+20, cy-8)
        end
        onion.display_line(cx-5, cy+24, cx-8, cy+34)
        onion.display_line(cx+5, cy+24, cx+8, cy+34)
        draw_onion_head(cx, cy-14, 12, happy, sick, asleep)
        if asleep then
            onion.display_text("z", cx+18, cy-20, {clear=false, font="small", color="black"})
            onion.display_text("Z", cx+24, cy-30, {clear=false, font="small", color="black"})
        end

    else  -- ADULT
        onion.display_round_rect(cx-14, cy,   28, 26, 6, {fill=true, color="black"})
        onion.display_round_rect(cx-12, cy+2, 24, 22, 5, {fill=true, color="white"})
        onion.display_rect(cx-4, cy-8, 8, 10, {fill=true, color="white"})
        if frame == 0 then
            onion.display_line(cx-14, cy+6,  cx-22, cy)
            onion.display_line(cx-22, cy,    cx-20, cy-4)
            onion.display_line(cx+14, cy+6,  cx+22, cy)
            onion.display_line(cx+22, cy,    cx+20, cy-4)
        else
            onion.display_line(cx-14, cy+2,  cx-22, cy-8)
            onion.display_line(cx-22, cy-8,  cx-20, cy-12)
            onion.display_line(cx+14, cy+2,  cx+22, cy-8)
            onion.display_line(cx+22, cy-8,  cx+20, cy-12)
        end
        onion.display_line(cx-5, cy+26, cx-8, cy+34)
        onion.display_line(cx-8, cy+34, cx-12, cy+34)
        onion.display_line(cx+5, cy+26, cx+8, cy+34)
        onion.display_line(cx+8, cy+34, cx+12, cy+34)
        draw_onion_head(cx, cy-20, 13, happy, sick, asleep)
        if asleep then
            onion.display_text("z", cx+18, cy-26, {clear=false, font="small", color="black"})
            onion.display_text("Z", cx+25, cy-36, {clear=false, font="small", color="black"})
        end
    end
end

local function draw_header()
    onion.display_rect(0, 0, W, 29, {fill=true, color="black"})
    onion.display_text(PET_NAMES[pet.name_idx], 8,   21, {clear=false, color="white", font="bold"})
    onion.display_text(STAGE_NAMES[pet.stage],  110, 21, {clear=false, color="white", font="bold"})
    -- Right-align day counter; 1 game-day = 12 ticks = 24 real minutes
    local days = math.floor(pet.age_ticks / 12)
    local astr = "D" .. days
    local ax   = W - 8 - #astr * CHAR_W
    onion.display_text(astr, ax, 21, {clear=false, color="white", font="bold"})
end

-- label  : 3-char string (e.g. "HUN")
-- y      : top of bar
-- val    : current value (0..MAX_STAT)
local function draw_stat_bar(label, y, val)
    onion.display_text(label, 8, y + BAR_H, {clear=false, color="black", font="small"})
    onion.display_rect(BAR_X, y, BAR_W, BAR_H, {clear=false, color="black"})
    local fill = math.floor(val * (BAR_W - 2) / MAX_STAT)
    if fill > 0 then
        onion.display_rect(BAR_X + 1, y + 1, fill, BAR_H - 2, {clear=false, fill=true, color="black"})
    end
end

local function draw_stat_panel()
    draw_stat_bar("HUN", STATS_Y[1], pet.hunger)
    draw_stat_bar("HAP", STATS_Y[2], pet.happiness)
    draw_stat_bar("NRG", STATS_Y[3], pet.energy)
    draw_stat_bar("HLT", STATS_Y[4], pet.health)
end

local function draw_action_bar(sel)
    for i = 0, 3 do
        local bx  = i * ACT_BW
        local lbl = ACTION_LABELS[i]
        -- center text in button using exact 11px advance
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

-- ── Screen renderers ──────────────────────────────────────────────────────────

local function render_action_bar_only(sel)
    onion.display_begin()
    -- Erase action bar zone (including divider at ACT_BY-1) to white in canvas,
    -- then redraw cleanly so partial refresh gets unambiguous black transitions.
    onion.display_rect(0, ACT_BY - 1, W, ACT_BH + 1, {clear=false, fill=true, color="white"})
    onion.display_line(0, ACT_BY - 1, W - 1, ACT_BY - 1)
    draw_action_bar(sel)
    onion.display_commit()
end

local function render_main(sel, feedback, frame)
    frame = frame or 0
    onion.display_begin()
    onion.clear_display()
    draw_header()
    draw_creature(132, 70, frame)

    if pet.stage == STAGE_EGG then
        local needed = STAGE_MIN_TICKS[STAGE_BABY]
        local pct    = math.floor(pet.age_ticks * 100 / needed)
        if pct > 100 then pct = 100 end
        local msg = "Incubating... " .. pct .. "%"
        onion.display_text(msg, text_cx(msg, 0, W), 122, {clear=false, font="small"})
        local bw  = W - 32
        local fil = math.floor(pet.age_ticks * (bw - 2) / needed)
        if fil > bw - 2 then fil = bw - 2 end
        onion.display_rect(16, 130, bw, 12, {clear=false, color="black"})
        if fil > 0 then
            onion.display_rect(17, 131, fil, 10, {clear=false, fill=true, color="black"})
        end

    elseif pet.sleeping then
        draw_stat_panel()
        onion.display_line(0, ACT_BY - 1, W - 1, ACT_BY - 1)
        onion.display_rect(0, ACT_BY, W, ACT_BH, {clear=false, fill=true, color="black"})
        local slp = "* SLEEPING *"
        onion.display_text(slp, text_cx(slp, 0, W), ACT_TEXT_Y,
            {clear=false, color="white", font="bold"})

    else
        draw_stat_panel()
        onion.display_line(0, ACT_BY - 1, W - 1, ACT_BY - 1)

        if feedback and feedback ~= "" then
            onion.display_rect(0, ACT_BY, W, ACT_BH, {clear=false, fill=true, color="black"})
            onion.display_text(feedback, text_cx(feedback, 0, W), ACT_TEXT_Y,
                {clear=false, color="white", font="small"})
        else
            draw_action_bar(sel)
        end
    end

    onion.display_commit()
end

local function render_exit_confirm(going_exit)
    onion.display_begin()
    onion.clear_display()
    onion.display_rect(0, 0, W, 28, {fill=true, color="black"})
    local ttl = "SAVE & EXIT?"
    onion.display_text(ttl, text_cx(ttl, 0, W), 20, {clear=false, color="white", font="bold"})
    local sub = "Your pet will be saved."
    onion.display_text(sub, text_cx(sub, 0, W), 60, {clear=false, font="small"})
    onion.display_line(0, 80, W - 1, 80)
    -- STAY (left)
    if going_exit then
        onion.display_rect(8, 118, 112, 30, {clear=false, color="black"})
        onion.display_text("STAY", text_cx("STAY", 8, 112), 139, {clear=false, color="black", font="bold"})
    else
        onion.display_rect(8, 118, 112, 30, {clear=false, fill=true, color="black"})
        onion.display_text("STAY", text_cx("STAY", 8, 112), 139, {clear=false, color="white", font="bold"})
    end
    -- EXIT (right)
    if going_exit then
        onion.display_rect(144, 118, 112, 30, {clear=false, fill=true, color="black"})
        onion.display_text("EXIT", text_cx("EXIT", 144, 112), 139, {clear=false, color="white", font="bold"})
    else
        onion.display_rect(144, 118, 112, 30, {clear=false, color="black"})
        onion.display_text("EXIT", text_cx("EXIT", 144, 112), 139, {clear=false, color="black", font="bold"})
    end
    onion.display_commit()
end

local function render_dead()
    onion.display_begin()
    onion.clear_display()
    onion.display_round_rect(104, 30, 56, 70, 24, {fill=true, color="black"})
    onion.display_round_rect(108, 34, 48, 62, 21, {fill=true, color="white"})
    onion.display_rect(92, 98, 80, 8, {fill=true, color="black"})
    local rip = "RIP"
    onion.display_text(rip, text_cx(rip, 0, W), 64, {clear=false, font="bold"})
    onion.display_text(PET_NAMES[pet.name_idx], text_cx(PET_NAMES[pet.name_idx], 0, W), 80, {clear=false})
    local days = math.floor(pet.age_ticks / 12)
    local msg  = "Lived " .. days .. (days == 1 and " day" or " days")
    onion.display_text(msg, text_cx(msg, 0, W), 120, {clear=false})
    onion.display_line(0, 132, W - 1, 132)
    onion.display_text("Your pet has passed on.", 8, 152, {clear=false})
    onion.display_text("Press SELECT to hatch a new egg.", 8, 167, {clear=false, font="small"})
    onion.display_commit()
end

local function render_hatch()
    onion.display_begin()
    onion.clear_display()
    onion.display_rect(0, 0, W, 29, {fill=true, color="black"})
    local ttl = "!! IT HATCHED !!"
    onion.display_text(ttl, text_cx(ttl, 0, W), 21, {clear=false, color="white", font="bold"})
    draw_onion_head(132, 78, 16, true, false, false)
    onion.display_line(132-16, 78+5, 132-22, 78+2)
    onion.display_line(132+16, 78+5, 132+22, 78+2)
    local say = "Say hi to " .. PET_NAMES[pet.name_idx] .. "!"
    onion.display_text(say, text_cx(say, 0, W), 125, {clear=false, font="bold"})
    onion.display_line(0, 135, W - 1, 135)
    local cont = "Press any button to continue"
    onion.display_text(cont, text_cx(cont, 0, W), 158, {clear=false, font="small"})
    onion.display_commit()
end

local function render_evolved(new_stage)
    onion.display_begin()
    onion.clear_display()
    onion.display_rect(0, 0, W, 29, {fill=true, color="black"})
    local ttl = "* EVOLVED! *"
    onion.display_text(ttl, text_cx(ttl, 0, W), 21, {clear=false, color="white", font="bold"})
    draw_creature(132, 75, 0)
    local msg = PET_NAMES[pet.name_idx] .. " is now a " .. STAGE_NAMES[new_stage] .. "!"
    onion.display_text(msg, text_cx(msg, 0, W), 132, {clear=false, font="small"})
    onion.display_line(0, 142, W - 1, 142)
    local cont = "Press any button to continue"
    onion.display_text(cont, text_cx(cont, 0, W), 162, {clear=false, font="small"})
    onion.display_commit()
end

-- ── Text-only fallback ────────────────────────────────────────────────────────

local function render_text(sel)
    local function bar(v)
        local n = math.floor(v * 8 / MAX_STAT)
        return "[" .. string.rep("#", n) .. string.rep(" ", 8-n) .. "]"
    end
    local lines
    if not pet.alive then
        lines = { PET_NAMES[pet.name_idx], "", "* PASSED ON *",
                  "D" .. math.floor(pet.age_ticks / 12), "", "SEL: new pet" }
    elseif pet.sleeping then
        lines = { PET_NAMES[pet.name_idx] .. " | " .. STAGE_NAMES[pet.stage],
                  "", "* SLEEPING *", "NRG " .. bar(pet.energy), "HUN " .. bar(pet.hunger) }
    elseif pet.stage == STAGE_EGG then
        local pct = math.floor(pet.age_ticks * 100 / STAGE_MIN_TICKS[STAGE_BABY])
        lines = { "EGG", "", "Incubating " .. pct .. "%", "", "SEL: hatch now" }
    else
        lines = { PET_NAMES[pet.name_idx] .. " | " .. STAGE_NAMES[pet.stage],
                  "", "HUN " .. bar(pet.hunger), "HAP " .. bar(pet.happiness),
                  "NRG " .. bar(pet.energy),     "HLT " .. bar(pet.health),
                  "", "> " .. (ACTION_LABELS[sel] or "?") }
    end
    onion.display_lines(lines, 4, 22, 18, {clear=true})
end

-- ── Stat tick ─────────────────────────────────────────────────────────────────

-- Returns (old_stage, new_stage) so caller can detect hatch and evolution.
local function tick_stats()
    if not pet.alive then return pet.stage, pet.stage end
    local old_stage   = pet.stage
    pet.age_ticks     = pet.age_ticks + 1
    pet.stage         = stage_for(pet.age_ticks)

    -- No stat decay during egg incubation
    if old_stage == STAGE_EGG then return old_stage, pet.stage end

    if pet.sleeping then
        pet.energy    = cstat(pet.energy    + 3)
        pet.happiness = cstat(pet.happiness + 1)
        if pet.energy >= MAX_STAT then pet.sleeping = false end
    else
        pet.hunger = cstat(pet.hunger - 1)
        pet.energy = cstat(pet.energy - 1)
        -- Happiness decays half as fast as hunger/energy
        if pet.age_ticks % 2 == 0 then
            pet.happiness = cstat(pet.happiness - 1)
        end
    end

    -- Health damage from neglect
    if pet.hunger    == 0 then pet.health = cstat(pet.health - 2) end
    if pet.happiness == 0 then pet.health = cstat(pet.health - 1) end

    -- Health recovery when well cared for
    if pet.hunger >= 7 and pet.happiness >= 5 and pet.energy >= 5 then
        pet.health = cstat(pet.health + 1)
    end

    -- Random sickness: only when health is weakened
    if pet.health <= 6 and math.random(100) <= 2 then
        pet.health = cstat(pet.health - 1)
    end

    if pet.health == 0 then pet.alive = false end
    return old_stage, pet.stage
end

-- ── Actions ───────────────────────────────────────────────────────────────────
local feedback_msg   = ""
local feedback_until = 0

local function set_feedback(msg)
    feedback_msg   = msg
    feedback_until = now_ms() + 2000
end

local function play_dance()
    if not HAS_PRIMITIVES then return end
    if pet.stage == STAGE_EGG then return end
    for i = 0, 3 do
        onion.display_begin()
        onion.display_rect(104, 29, 57, 79, {clear=false, fill=true, color="white"})
        draw_creature(132, 70, i % 2)
        onion.display_commit()
        onion.sleep(300)
    end
end

local function perform_action(sel)
    if sel == ACTION_FEED then
        if pet.hunger >= MAX_STAT then
            set_feedback("Already full!")
        else
            pet.hunger    = cstat(pet.hunger    + 3)
            pet.happiness = cstat(pet.happiness + 1)
            set_feedback("Yum!  +Hunger")
        end
    elseif sel == ACTION_PLAY then
        if pet.energy <= 1 then
            set_feedback("Too tired to play!")
        elseif pet.happiness >= MAX_STAT then
            set_feedback("Already happy!")
        else
            play_dance()
            pet.happiness = cstat(pet.happiness + 3)
            pet.hunger    = cstat(pet.hunger    - 1)
            pet.energy    = cstat(pet.energy    - 1)
            set_feedback("Wheee!  +Happy")
        end
    elseif sel == ACTION_SLEEP then
        if pet.energy >= MAX_STAT then
            set_feedback("Not tired!")
        else
            pet.sleeping   = true
            feedback_msg   = ""
            feedback_until = 0
        end
    elseif sel == ACTION_MED then
        if pet.health >= MAX_STAT then
            set_feedback("Already healthy!")
        else
            pet.health    = cstat(pet.health    + 3)
            pet.happiness = cstat(pet.happiness - 1)
            set_feedback("All better!  +Health")
        end
    end
end

-- ── Startup ───────────────────────────────────────────────────────────────────

load_pet()
math.randomseed(pet.age_ticks + pet.hunger * 7 + pet.name_idx * 13)

local sel             = ACTION_FEED
local tick_acc        = 0
local last_ms         = now_ms()
local needs_full      = true
local needs_bar       = false
local just_hatched    = false
local just_evolved    = false
local evolved_stage   = 0
local confirming_exit = false
local exit_choice     = false  -- false=STAY, true=EXIT

-- Ghost-clear: full white refresh to wipe any OS-screen ghosting before first render.
-- clear_display() sets g_forceFullRefresh=true so this is a true full-waveform refresh.
onion.display_begin()
onion.clear_display()
onion.display_commit()

-- Drain held buttons before entering the loop
local last = onion.buttons()
while last.left or last.right or last.up or last.down or last.select or last.cancel do
    onion.sleep(50)
    last = onion.buttons()
end

-- ── Main loop ─────────────────────────────────────────────────────────────────

while true do
    local now_t = now_ms()
    local dt    = now_t - last_ms
    if dt < 0 then dt = 0 end
    last_ms = now_t

    -- Accumulate time and fire stat ticks
    tick_acc = tick_acc + dt
    while tick_acc >= TICK_MS do
        tick_acc = tick_acc - TICK_MS
        local old_s, new_s = tick_stats()
        if old_s == STAGE_EGG and new_s == STAGE_BABY then
            just_hatched = true
        elseif old_s ~= new_s and new_s ~= STAGE_EGG then
            just_evolved  = true
            evolved_stage = new_s
        end
        needs_full = true
    end

    -- Expire feedback
    if feedback_msg ~= "" and now_t >= feedback_until then
        feedback_msg = ""
        needs_full   = true
    end

    -- ── Button handling ───────────────────────────────────────────────────────
    local btns = onion.buttons()

    if confirming_exit then
        if btns.select and not last.select then
            if exit_choice then
                save_pet()
                onion.release_display()
                return
            else
                confirming_exit = false
                exit_choice     = false
                needs_full      = true
            end
        elseif btns.cancel and not last.cancel then
            confirming_exit = false
            exit_choice     = false
            needs_full      = true
        elseif (btns.right and not last.right) or (btns.down and not last.down) then
            if not exit_choice then
                exit_choice = true
                render_exit_confirm(true)
            end
        elseif (btns.left and not last.left) or (btns.up and not last.up) then
            if exit_choice then
                exit_choice = false
                render_exit_confirm(false)
            end
        end

    elseif just_hatched then
        local any = (btns.left   and not last.left)   or (btns.right  and not last.right) or
                    (btns.up     and not last.up)      or (btns.down   and not last.down)  or
                    (btns.select and not last.select)
        if any then
            just_hatched = false
            needs_full   = true
        end
        if btns.cancel and not last.cancel then
            confirming_exit = true
            exit_choice     = false
            needs_full      = false
        end

    elseif just_evolved then
        local any = (btns.left   and not last.left)   or (btns.right  and not last.right) or
                    (btns.up     and not last.up)      or (btns.down   and not last.down)  or
                    (btns.select and not last.select)
        if any then
            just_evolved = false
            needs_full   = true
        end
        if btns.cancel and not last.cancel then
            confirming_exit = true
            exit_choice     = false
            needs_full      = false
        end

    else
        if btns.cancel and not last.cancel then
            confirming_exit = true
            exit_choice     = false
            render_exit_confirm(false)
            needs_full = false

        elseif pet.stage == STAGE_EGG then
            if btns.select and not last.select then
                pet.age_ticks = STAGE_MIN_TICKS[STAGE_BABY]
                pet.stage     = STAGE_BABY
                just_hatched  = true
                save_pet()
                needs_full    = true
            end

        elseif not pet.alive then
            if btns.select and not last.select then
                pet = {
                    hunger    = MAX_STAT, happiness = MAX_STAT,
                    energy    = MAX_STAT, health    = MAX_STAT,
                    age_ticks = 0,
                    stage     = STAGE_EGG,
                    name_idx  = math.random(#PET_NAMES),
                    alive     = true,     sleeping  = false,
                }
                sel          = ACTION_FEED
                feedback_msg = ""
                tick_acc     = 0
                save_pet()
                needs_full   = true
            end

        elseif pet.sleeping then
            local any = (btns.select and not last.select) or
                        (btns.up     and not last.up)     or
                        (btns.down   and not last.down)   or
                        (btns.left   and not last.left)   or
                        (btns.right  and not last.right)
            if any then
                pet.sleeping = false
                save_pet()
                needs_full   = true
            end

        else
            if btns.left and not last.left then
                sel      = (sel - 1 + ACTION_COUNT) % ACTION_COUNT
                needs_bar = true
            end
            if btns.right and not last.right then
                sel      = (sel + 1) % ACTION_COUNT
                needs_bar = true
            end
            if btns.up and not last.up then
                sel      = ACTION_FEED
                needs_bar = true
            end
            if btns.down and not last.down then
                sel      = ACTION_SLEEP
                needs_bar = true
            end
            if btns.select and not last.select then
                perform_action(sel)
                save_pet()
                needs_full = true
            end
        end
    end

    last = btns

    -- ── Render ────────────────────────────────────────────────────────────────
    if needs_full then
        needs_full = false
        needs_bar  = false
        if confirming_exit then
            render_exit_confirm(exit_choice)
        elseif just_hatched then
            if HAS_PRIMITIVES then render_hatch()                else render_text(sel) end
        elseif just_evolved then
            if HAS_PRIMITIVES then render_evolved(evolved_stage) else render_text(sel) end
        elseif not pet.alive then
            if HAS_PRIMITIVES then render_dead()                 else render_text(sel) end
        else
            if HAS_PRIMITIVES then render_main(sel, feedback_msg) else render_text(sel) end
        end
    elseif needs_bar then
        needs_bar = false
        if HAS_PRIMITIVES then render_action_bar_only(sel) else render_text(sel) end
    end

    onion.sleep(20)
end
