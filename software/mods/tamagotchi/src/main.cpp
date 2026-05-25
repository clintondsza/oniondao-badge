#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <Preferences.h>
#include <esp_sleep.h>
#include <GxEPD2_BW.h>
#include <gdey/GxEPD2_270_GDEY027T91.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold24pt7b.h>
#include <Fonts/FreeMono9pt7b.h>

#include "badge_pins.h"

// ── I²C / buttons ─────────────────────────────────────────────────────────────
#define TCA9534_ADDR   0x20
#define TCA9534_INPUT  0x00
#define TCA9534_CONFIG 0x03

#define BTN_LEFT   (1 << 0)
#define BTN_DOWN   (1 << 1)
#define BTN_UP     (1 << 2)
#define BTN_RIGHT  (1 << 3)
#define BTN_SELECT (1 << 4)
#define BTN_CANCEL (1 << 5)

// ── Display ───────────────────────────────────────────────────────────────────
GxEPD2_BW<GxEPD2_270_GDEY027T91, GxEPD2_270_GDEY027T91::HEIGHT> display(
    GxEPD2_270_GDEY027T91(PIN_EPD_CS, PIN_EPD_DC, PIN_EPD_RST, PIN_EPD_BUSY));

// ── Pet constants ─────────────────────────────────────────────────────────────
#define MAX_STAT        10
#define TICK_US         (30ULL * 60 * 1000000)  // 30-min timer wakeup
#define SLEEP_AFTER_MS  45000                    // idle → deep sleep
#define SICK_CHANCE_PCT 5                        // % chance of random sickness per tick

// Stage transitions (age in 30-min ticks): EGG<3, BABY<48, CHILD<144, TEEN<336, ADULT
enum Stage : uint8_t { STAGE_EGG=0, STAGE_BABY, STAGE_CHILD, STAGE_TEEN, STAGE_ADULT };
static const uint32_t STAGE_MIN_TICKS[] = { 0, 3, 48, 144, 336 };
static const char*    STAGE_NAMES[]     = { "EGG", "BABY", "CHILD", "TEEN", "ADULT" };

static const char* PET_NAMES[] = { "KERNEL", "HEXBIT", "DAEMON", "PACKET" };
#define PET_NAME_COUNT 4

enum Action : uint8_t { ACTION_FEED=0, ACTION_PLAY, ACTION_SLEEP, ACTION_MED };
static const char* ACTION_LABELS[] = { "FEED", "PLAY", "SLEEP", "MED " };
#define ACTION_COUNT 4

// ── Pet state ─────────────────────────────────────────────────────────────────
struct PetState {
    uint8_t  hunger;
    uint8_t  happiness;
    uint8_t  energy;
    uint8_t  health;
    uint32_t age_ticks;
    uint8_t  stage;
    uint8_t  name_idx;
    bool     alive;
    bool     sleeping;
};
static PetState g_pet;

// ── App state ─────────────────────────────────────────────────────────────────
static bool    g_needs_redraw    = true;
static uint8_t g_last_btns       = 0;
static int     g_action          = ACTION_FEED;
static uint32_t s_last_activity;
static bool    g_just_hatched    = false;  // show hatch screen on next render
static bool    g_just_slept      = false;  // true only when SLEEP action was pressed this session

// Survive deep sleep
RTC_DATA_ATTR bool rtc_valid        = false;
RTC_DATA_ATTR bool rtc_just_hatched = false;  // set by timer tick, shown on button wake

// ── Helpers ───────────────────────────────────────────────────────────────────
static inline uint8_t cstat(int v) {
    return v < 0 ? 0 : v > MAX_STAT ? MAX_STAT : (uint8_t)v;
}

static uint8_t stage_for(uint32_t ticks) {
    for (int i = 4; i >= 0; i--)
        if (ticks >= STAGE_MIN_TICKS[i]) return (uint8_t)i;
    return STAGE_EGG;
}

// ── NVS ───────────────────────────────────────────────────────────────────────
static void load_pet() {
    Preferences p;
    p.begin("tama", true);
    g_pet.hunger    = p.getUChar("hunger",  MAX_STAT);
    g_pet.happiness = p.getUChar("happy",   MAX_STAT);
    g_pet.energy    = p.getUChar("energy",  MAX_STAT);
    g_pet.health    = p.getUChar("health",  MAX_STAT);
    g_pet.age_ticks = p.getUInt ("age",     0);
    g_pet.name_idx  = p.getUChar("name",    (uint8_t)(esp_random() % PET_NAME_COUNT));
    g_pet.alive     = p.getBool ("alive",   true);
    g_pet.sleeping  = p.getBool ("sleep",   false);
    p.end();
    g_pet.stage = stage_for(g_pet.age_ticks);
}

static void save_pet() {
    Preferences p;
    p.begin("tama", false);
    p.putUChar("hunger", g_pet.hunger);
    p.putUChar("happy",  g_pet.happiness);
    p.putUChar("energy", g_pet.energy);
    p.putUChar("health", g_pet.health);
    p.putUInt ("age",    g_pet.age_ticks);
    p.putUChar("name",   g_pet.name_idx);
    p.putBool ("alive",  g_pet.alive);
    p.putBool ("sleep",  g_pet.sleeping);
    p.end();
}

static void reset_pet() {
    Preferences p;
    p.begin("tama", false);
    p.clear();
    p.end();
    load_pet();  // reloads with fresh defaults
}

// ── Stat tick (runs on every 30-min timer wakeup) ─────────────────────────────
static void tick_stats() {
    if (!g_pet.alive) return;

    uint8_t old_stage = g_pet.stage;
    g_pet.age_ticks++;
    g_pet.stage = stage_for(g_pet.age_ticks);
    if (old_stage == STAGE_EGG && g_pet.stage == STAGE_BABY) rtc_just_hatched = true;

    if (g_pet.sleeping) {
        g_pet.energy    = cstat(g_pet.energy    + 3);
        g_pet.happiness = cstat(g_pet.happiness + 1);
        if (g_pet.energy >= MAX_STAT) g_pet.sleeping = false;
    } else {
        g_pet.hunger = cstat(g_pet.hunger - 1);
        g_pet.energy = cstat(g_pet.energy - 1);
    }

    g_pet.happiness = cstat(g_pet.happiness - 1);

    if (g_pet.hunger    == 0) g_pet.health = cstat(g_pet.health - 2);
    if (g_pet.happiness == 0) g_pet.health = cstat(g_pet.health - 1);

    if ((esp_random() % 100) < SICK_CHANCE_PCT)
        g_pet.health = cstat(g_pet.health - 2);

    if (g_pet.health == 0) g_pet.alive = false;
}

// ── Button read ───────────────────────────────────────────────────────────────
static uint8_t read_buttons() {
    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_INPUT);
    Wire.endTransmission(false);
    Wire.requestFrom(TCA9534_ADDR, 1);
    return (~Wire.read()) & 0x3F;
}

// ── Draw: onion head ──────────────────────────────────────────────────────────
// cx, cy = centre of the bulb circle; r = bulb radius.
// Draws the bulb outline, pointed tip + sprout, internal layer lines, and face.
static void draw_onion_head(int cx, int cy, int r, bool happy, bool sick, bool sleeping) {
    // Bulb
    display.fillCircle(cx, cy, r,     GxEPD_BLACK);
    display.fillCircle(cx, cy, r - 2, GxEPD_WHITE);

    // Pointed tip (small filled triangle just above circle top)
    int tp = cy - r;
    display.fillTriangle(cx - 2, tp + 2, cx + 2, tp + 2, cx, tp - 3, GxEPD_BLACK);

    // Two sprout lines from the tip
    display.drawLine(cx - 1, tp - 3, cx - 3, tp - 7, GxEPD_BLACK);
    display.drawLine(cx + 1, tp - 3, cx + 3, tp - 7, GxEPD_BLACK);

    // Internal lines: centre seam + two bracketed layer curves
    display.drawLine(cx,          cy - r + 3,     cx,          cy + r - 3,     GxEPD_BLACK);
    display.drawLine(cx - 1,      cy - r + 4,     cx - r*5/8,  cy + r/8,       GxEPD_BLACK);
    display.drawLine(cx - r*5/8,  cy + r/8,       cx - 2,      cy + r - 4,     GxEPD_BLACK);
    display.drawLine(cx + 1,      cy - r + 4,     cx + r*5/8,  cy + r/8,       GxEPD_BLACK);
    display.drawLine(cx + r*5/8,  cy + r/8,       cx + 2,      cy + r - 4,     GxEPD_BLACK);

    // Eyes
    int ey = cy - r / 4;
    int ex = r * 2 / 5;
    if (sick) {
        display.drawLine(cx-ex-2, ey-2, cx-ex+2, ey+2, GxEPD_BLACK);
        display.drawLine(cx-ex+2, ey-2, cx-ex-2, ey+2, GxEPD_BLACK);
        display.drawLine(cx+ex-2, ey-2, cx+ex+2, ey+2, GxEPD_BLACK);
        display.drawLine(cx+ex+2, ey-2, cx+ex-2, ey+2, GxEPD_BLACK);
    } else if (sleeping) {
        display.drawLine(cx-ex-2, ey, cx-ex+2, ey, GxEPD_BLACK);
        display.drawLine(cx+ex-2, ey, cx+ex+2, ey, GxEPD_BLACK);
    } else {
        display.fillCircle(cx - ex, ey, 2, GxEPD_BLACK);
        display.fillCircle(cx + ex, ey, 2, GxEPD_BLACK);
        display.fillCircle(cx - ex + 1, ey - 1, 1, GxEPD_WHITE);
        display.fillCircle(cx + ex + 1, ey - 1, 1, GxEPD_WHITE);
    }

    // Mouth
    int my = cy + r / 3;
    if (happy) {
        display.drawLine(cx - 3, my,     cx - 1, my + 2, GxEPD_BLACK);
        display.drawLine(cx - 1, my + 2, cx + 1, my + 2, GxEPD_BLACK);
        display.drawLine(cx + 1, my + 2, cx + 3, my,     GxEPD_BLACK);
    } else {
        display.drawLine(cx - 3, my + 2, cx - 1, my,     GxEPD_BLACK);
        display.drawLine(cx - 1, my,     cx + 1, my,     GxEPD_BLACK);
        display.drawLine(cx + 1, my,     cx + 3, my + 2, GxEPD_BLACK);
    }
}

// ── Draw: creature ────────────────────────────────────────────────────────────
// frame 0 = resting pose, frame 1 = arms-up dance pose
static void draw_creature(int cx, int cy, uint8_t frame = 0) {
    bool happy  = (g_pet.happiness >= 5 && g_pet.hunger >= 3);
    bool sick   = (g_pet.health <= 2 && g_pet.alive && g_pet.stage != STAGE_EGG);
    bool asleep = g_pet.sleeping;

    switch (g_pet.stage) {

    case STAGE_EGG:
        display.fillRoundRect(cx-14, cy-22, 28, 40, 12, GxEPD_BLACK);
        display.fillRoundRect(cx-11, cy-19, 22, 33,  9, GxEPD_WHITE);
        display.drawLine(cx-1, cy-18, cx+2, cy-12, GxEPD_BLACK);
        display.drawLine(cx+2, cy-12, cx-1, cy- 8, GxEPD_BLACK);
        break;

    case STAGE_BABY:
        draw_onion_head(cx, cy, 16, happy, sick, asleep);
        if (frame == 0) {
            display.drawLine(cx-16, cy+ 5, cx-22, cy+ 2, GxEPD_BLACK);
            display.drawLine(cx+16, cy+ 5, cx+22, cy+ 2, GxEPD_BLACK);
        } else {
            display.drawLine(cx-16, cy- 2, cx-20, cy-10, GxEPD_BLACK);
            display.drawLine(cx+16, cy- 2, cx+20, cy-10, GxEPD_BLACK);
        }
        if (asleep) {
            display.setFont(&FreeMono9pt7b); display.setTextColor(GxEPD_BLACK);
            display.setCursor(cx+20, cy-6);  display.print("z");
            display.setCursor(cx+26, cy-16); display.print("Z");
        }
        break;

    case STAGE_CHILD:
        display.fillCircle(cx, cy+10, 14, GxEPD_BLACK);
        display.fillCircle(cx, cy+10, 11, GxEPD_WHITE);
        if (frame == 0) {
            display.drawLine(cx-14, cy+10, cx-20, cy+ 6, GxEPD_BLACK);
            display.drawLine(cx+14, cy+10, cx+20, cy+ 6, GxEPD_BLACK);
        } else {
            display.drawLine(cx-14, cy+ 4, cx-20, cy- 6, GxEPD_BLACK);
            display.drawLine(cx+14, cy+ 4, cx+20, cy- 6, GxEPD_BLACK);
        }
        draw_onion_head(cx, cy-8, 11, happy, sick, asleep);
        if (asleep) {
            display.setFont(&FreeMono9pt7b); display.setTextColor(GxEPD_BLACK);
            display.setCursor(cx+16, cy-14); display.print("z");
            display.setCursor(cx+22, cy-24); display.print("Z");
        }
        break;

    case STAGE_TEEN:
        display.fillRoundRect(cx-12, cy+2,  24, 22, 5, GxEPD_BLACK);
        display.fillRoundRect(cx-10, cy+4,  20, 18, 4, GxEPD_WHITE);
        display.fillRect(cx-4,  cy-5, 8, 9, GxEPD_WHITE);
        if (frame == 0) {
            display.drawLine(cx-12, cy+ 8, cx-20, cy+14, GxEPD_BLACK);
            display.drawLine(cx+12, cy+ 8, cx+20, cy+14, GxEPD_BLACK);
        } else {
            display.drawLine(cx-12, cy+ 2, cx-20, cy- 8, GxEPD_BLACK);
            display.drawLine(cx+12, cy+ 2, cx+20, cy- 8, GxEPD_BLACK);
        }
        display.drawLine(cx- 5, cy+24, cx- 8, cy+34, GxEPD_BLACK);
        display.drawLine(cx+ 5, cy+24, cx+ 8, cy+34, GxEPD_BLACK);
        draw_onion_head(cx, cy-14, 12, happy, sick, asleep);
        if (asleep) {
            display.setFont(&FreeMono9pt7b); display.setTextColor(GxEPD_BLACK);
            display.setCursor(cx+18, cy-20); display.print("z");
            display.setCursor(cx+24, cy-30); display.print("Z");
        }
        break;

    case STAGE_ADULT:
        display.fillRoundRect(cx-14, cy,   28, 26, 6, GxEPD_BLACK);
        display.fillRoundRect(cx-12, cy+2, 24, 22, 5, GxEPD_WHITE);
        display.fillRect(cx-4,  cy-8, 8, 10, GxEPD_WHITE);
        if (frame == 0) {
            display.drawLine(cx-14, cy+ 6, cx-22, cy,    GxEPD_BLACK);
            display.drawLine(cx-22, cy,    cx-20, cy- 4, GxEPD_BLACK);
            display.drawLine(cx+14, cy+ 6, cx+22, cy,    GxEPD_BLACK);
            display.drawLine(cx+22, cy,    cx+20, cy- 4, GxEPD_BLACK);
        } else {
            display.drawLine(cx-14, cy+ 2, cx-22, cy- 8, GxEPD_BLACK);
            display.drawLine(cx-22, cy- 8, cx-20, cy-12, GxEPD_BLACK);
            display.drawLine(cx+14, cy+ 2, cx+22, cy- 8, GxEPD_BLACK);
            display.drawLine(cx+22, cy- 8, cx+20, cy-12, GxEPD_BLACK);
        }
        display.drawLine(cx- 5, cy+26, cx- 8, cy+36,  GxEPD_BLACK);
        display.drawLine(cx- 8, cy+36, cx-12, cy+36,  GxEPD_BLACK);
        display.drawLine(cx+ 5, cy+26, cx+ 8, cy+36,  GxEPD_BLACK);
        display.drawLine(cx+ 8, cy+36, cx+12, cy+36,  GxEPD_BLACK);
        draw_onion_head(cx, cy-20, 13, happy, sick, asleep);
        if (asleep) {
            display.setFont(&FreeMono9pt7b); display.setTextColor(GxEPD_BLACK);
            display.setCursor(cx+18, cy-26); display.print("z");
            display.setCursor(cx+25, cy-36); display.print("Z");
        }
        break;
    }
}

// ── Dance animation (partial-window, ~500 ms per frame) ───────────────────────
// Only the creature area (x=72..191, y=31..108) is refreshed each frame.
// The rest of the screen — header, stat bars, action bar — stays untouched.
static void animate_dance(int cycles) {
    if (g_pet.stage == STAGE_EGG) return;

    const int16_t wx = 72, wy = 31, ww = 120, wh = 78;
    const int cx = 132, cy = 70;

    for (int i = 0; i < cycles * 2; i++) {
        display.setPartialWindow(wx, wy, ww, wh);
        display.firstPage();
        do {
            display.fillRect(wx, wy, ww, wh, GxEPD_WHITE);
            draw_creature(cx, cy, i % 2);
        } while (display.nextPage());
    }
    // Leave display awake — full redraw follows immediately after
}

// ── Draw: stat bar ────────────────────────────────────────────────────────────
// y = top of the bar row (bar rect at y+1..y+8, label baseline at y+10)
static void draw_stat_bar(int x, int y, const char* label, uint8_t val) {
    display.setFont(&FreeMono9pt7b);
    display.setTextColor(GxEPD_BLACK);
    display.setCursor(x, y + 10);
    display.print(label);

    const int bar_x = x + 36;
    const int bar_w = 160;
    const int bar_h = 8;
    const int filled = (val * bar_w) / MAX_STAT;

    display.drawRect(bar_x, y + 1, bar_w, bar_h, GxEPD_BLACK);
    if (filled > 2)
        display.fillRect(bar_x + 1, y + 2, filled - 2, bar_h - 2, GxEPD_BLACK);
}

// ── Draw: action bar ──────────────────────────────────────────────────────────
static void draw_action_bar() {
    const int btn_w = 66;
    const int btn_y = 156;
    const int btn_h = 20;

    for (int i = 0; i < ACTION_COUNT; i++) {
        int bx = i * btn_w;
        if (i == g_action) {
            display.fillRect(bx, btn_y, btn_w, btn_h, GxEPD_BLACK);
            display.setTextColor(GxEPD_WHITE);
        } else {
            display.drawRect(bx, btn_y, btn_w, btn_h, GxEPD_BLACK);
            display.setTextColor(GxEPD_BLACK);
        }
        display.setFont(&FreeMono9pt7b);
        display.setCursor(bx + 6, btn_y + 14);
        display.print(ACTION_LABELS[i]);
    }
}

// ── Draw: header ──────────────────────────────────────────────────────────────
static void draw_header() {
    display.fillRect(0, 0, 264, 29, GxEPD_BLACK);
    display.setFont(&FreeMonoBold9pt7b);
    display.setTextColor(GxEPD_WHITE);

    display.setCursor(8, 21);
    display.print(PET_NAMES[g_pet.name_idx]);

    display.setCursor(110, 21);
    display.print(STAGE_NAMES[g_pet.stage]);

    char age_str[8];
    uint32_t days = g_pet.age_ticks / 48;  // 2 ticks/hr × 24 hr
    snprintf(age_str, sizeof(age_str), "D%lu", (unsigned long)days);
    int16_t x1, y1; uint16_t tw, th;
    display.getTextBounds(age_str, 0, 0, &x1, &y1, &tw, &th);
    display.setCursor(256 - (int16_t)tw, 21);
    display.print(age_str);
}

// ── Draw: main game screen ────────────────────────────────────────────────────
static void draw_main_screen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);
        draw_header();

        // Creature area: centre x=132, centre y=70
        draw_creature(132, 70);

        if (g_pet.stage == STAGE_EGG) {
            display.setFont(&FreeMono9pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(8, 125);
            display.print("Incubating...");

            uint32_t needed = STAGE_MIN_TICKS[STAGE_BABY];
            int filled = (int)((g_pet.age_ticks * 200) / needed);
            if (filled > 200) filled = 200;
            display.drawRect(32, 132, 200, 10, GxEPD_BLACK);
            if (filled > 2) display.fillRect(33, 133, filled - 2, 8, GxEPD_BLACK);

            display.setCursor(8, 162);
            display.print("...check back later");

        } else if (g_pet.sleeping) {
            display.setFont(&FreeMonoBold9pt7b);
            display.setTextColor(GxEPD_BLACK);
            display.setCursor(60, 130);
            display.print("* SLEEPING *");
            draw_stat_bar(8, 138, "NRG", g_pet.energy);
            display.setFont(&FreeMono9pt7b);
            display.setCursor(40, 165);
            display.print("SEL: wake up early");

        } else {
            draw_stat_bar(8, 110, "HUN", g_pet.hunger);
            draw_stat_bar(8, 121, "HAP", g_pet.happiness);
            draw_stat_bar(8, 132, "NRG", g_pet.energy);
            draw_stat_bar(8, 143, "HLT", g_pet.health);
            display.drawFastHLine(0, 154, 264, GxEPD_BLACK);
            draw_action_bar();
        }

    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: death screen ────────────────────────────────────────────────────────
static void draw_dead_screen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        // Tombstone
        display.fillRoundRect(104, 30, 56, 70, 24, GxEPD_BLACK);
        display.fillRoundRect(108, 34, 48, 62, 21, GxEPD_WHITE);
        display.fillRect(92, 98, 80, 8, GxEPD_BLACK);   // base slab

        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(117, 64);
        display.print("RIP");

        display.setFont(&FreeMono9pt7b);
        display.setCursor(117, 80);
        display.print(PET_NAMES[g_pet.name_idx]);

        // Age
        char msg[28];
        uint32_t days = g_pet.age_ticks / 48;
        snprintf(msg, sizeof(msg), "Lived %lu day%s", (unsigned long)days, days==1?"":"s");
        int16_t x1, y1; uint16_t tw, th;
        display.getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
        display.setCursor((264 - (int16_t)tw) / 2, 120);
        display.print(msg);

        display.drawFastHLine(0, 130, 264, GxEPD_BLACK);
        display.setCursor(8, 148);
        display.print("Your pet has passed on.");
        display.setCursor(8, 165);
        display.print("SEL: hatch a new egg");

    } while (display.nextPage());
    display.hibernate();
}

// ── Draw: hatch screen ────────────────────────────────────────────────────────
static void draw_hatch_screen() {
    display.setFullWindow();
    display.firstPage();
    do {
        display.fillScreen(GxEPD_WHITE);

        display.fillRect(0, 0, 264, 29, GxEPD_BLACK);
        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_WHITE);
        display.setCursor(42, 21);
        display.print("!! IT HATCHED !!");

        // Baby onion creature
        int cx = 132, cy = 78;
        draw_onion_head(cx, cy, 16, true, false, false);
        display.drawLine(cx-16, cy+5, cx-22, cy+2, GxEPD_BLACK);
        display.drawLine(cx+16, cy+5, cx+22, cy+2, GxEPD_BLACK);

        display.setFont(&FreeMonoBold9pt7b);
        display.setTextColor(GxEPD_BLACK);
        char msg[24];
        snprintf(msg, sizeof(msg), "Say hi to %s!", PET_NAMES[g_pet.name_idx]);
        int16_t x1, y1; uint16_t tw, th;
        display.getTextBounds(msg, 0, 0, &x1, &y1, &tw, &th);
        display.setCursor((264 - (int16_t)tw) / 2, 125);
        display.print(msg);

        display.drawFastHLine(0, 135, 264, GxEPD_BLACK);
        display.setFont(&FreeMono9pt7b);
        display.setTextColor(GxEPD_BLACK);
        display.setCursor(55, 158);
        display.print("Press any button");

    } while (display.nextPage());
    display.hibernate();
}

// ── Dev: advance one stage (UP+DOWN combo) ────────────────────────────────────
static void advance_stage_for_testing() {
    if (g_pet.stage >= STAGE_ADULT) {
        // Wrap back to a fresh egg
        Preferences p;
        p.begin("tama", false);
        p.clear();
        p.end();
        load_pet();
        return;
    }
    uint8_t next = g_pet.stage + 1;
    g_pet.age_ticks = STAGE_MIN_TICKS[next];
    g_pet.stage     = next;
    g_pet.hunger    = MAX_STAT;
    g_pet.happiness = MAX_STAT;
    g_pet.energy    = MAX_STAT;
    g_pet.health    = MAX_STAT;
    g_pet.alive     = true;
    g_pet.sleeping  = false;
    if (next == STAGE_BABY) g_just_hatched = true;
    save_pet();
}

// ── Actions ───────────────────────────────────────────────────────────────────
static void perform_action(int action) {
    switch (action) {
    case ACTION_FEED:
        g_pet.hunger    = cstat(g_pet.hunger    + 4);
        g_pet.happiness = cstat(g_pet.happiness + 1);
        break;
    case ACTION_PLAY:
        animate_dance(3);  // 6 partial-window frames (~3 s) before applying stats
        g_pet.happiness = cstat(g_pet.happiness + 4);
        g_pet.hunger    = cstat(g_pet.hunger    - 1);
        g_pet.energy    = cstat(g_pet.energy    - 2);
        break;
    case ACTION_SLEEP:
        g_pet.sleeping = true;
        g_just_slept   = true;
        break;
    case ACTION_MED:
        g_pet.health    = cstat(g_pet.health    + 4);
        g_pet.happiness = cstat(g_pet.happiness - 1);
        break;
    }
}

// ── Button handling ───────────────────────────────────────────────────────────
static void handle_buttons(uint8_t pressed) {
    if (!pressed) return;

    // CANCEL: advance one stage for testing (works from any screen)
    if (pressed & BTN_CANCEL) {
        g_just_hatched = false;
        g_just_slept   = false;
        advance_stage_for_testing();
        g_needs_redraw = true;
        return;
    }

    // Any other button dismisses the hatch screen
    if (g_just_hatched) {
        g_just_hatched = false;
        g_needs_redraw = true;
        return;
    }

    if (!g_pet.alive) {
        if (pressed & BTN_SELECT) {
            reset_pet();
            g_action       = ACTION_FEED;
            g_needs_redraw = true;
        }
        return;
    }

    // Wake from sleep on any button
    if (g_pet.sleeping) {
        g_pet.sleeping = false;
        save_pet();
        g_needs_redraw = true;
        return;
    }

    if (g_pet.stage == STAGE_EGG) return;

    if (pressed & BTN_LEFT)  { g_action = (g_action - 1 + ACTION_COUNT) % ACTION_COUNT; g_needs_redraw = true; }
    if (pressed & BTN_RIGHT) { g_action = (g_action + 1) % ACTION_COUNT;                g_needs_redraw = true; }
    if (pressed & BTN_DOWN)  { g_action = (g_action + 1) % ACTION_COUNT;                g_needs_redraw = true; }
    if (pressed & BTN_UP)    { g_action = (g_action - 1 + ACTION_COUNT) % ACTION_COUNT; g_needs_redraw = true; }

    if (pressed & BTN_SELECT) {
        perform_action(g_action);
        save_pet();
        g_needs_redraw = true;
    }
}

// ── Sleep ─────────────────────────────────────────────────────────────────────
static void go_to_sleep() {
    read_buttons();  // clear pending TCA9534 interrupt
    display.hibernate();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_IRQ, 0);
    esp_sleep_enable_timer_wakeup(TICK_US);
    esp_deep_sleep_start();
}

// ── Setup ─────────────────────────────────────────────────────────────────────
void setup() {
    auto cause = esp_sleep_get_wakeup_cause();

    // Timer wakeup: tick stats silently and go right back to sleep.
    // No display, no I²C needed — just NVS and the RNG for sickness roll.
    if (cause == ESP_SLEEP_WAKEUP_TIMER) {
        load_pet();
        tick_stats();
        save_pet();
        esp_sleep_enable_ext0_wakeup((gpio_num_t)PIN_BTN_IRQ, 0);
        esp_sleep_enable_timer_wakeup(TICK_US);
        esp_deep_sleep_start();
    }

    // Interactive session — full peripheral init
    Serial.begin(115200);

    pinMode(PIN_PWR, OUTPUT);
    digitalWrite(PIN_PWR, HIGH);
    pinMode(PIN_SE_EN, OUTPUT);
    digitalWrite(PIN_SE_EN, LOW);  // SE not used

    delay(50);

    Wire.begin(PIN_SDA, PIN_SCL);
    Wire.setClock(400000);

    Wire.beginTransmission(TCA9534_ADDR);
    Wire.write(TCA9534_CONFIG);
    Wire.write(0xFF);
    Wire.endTransmission();

    SPI.begin(PIN_EPD_SCK, /*MISO*/-1, PIN_EPD_MOSI, PIN_EPD_CS);
    display.init(115200, true, 10, false);
    display.setRotation(1);

    load_pet();
    g_just_hatched   = rtc_just_hatched;
    rtc_just_hatched = false;
    rtc_valid        = true;
    g_needs_redraw   = true;
    s_last_activity  = millis();

    Serial.printf("[tama] %s age=%lu ticks alive=%d sleeping=%d\n",
                  PET_NAMES[g_pet.name_idx], (unsigned long)g_pet.age_ticks,
                  (int)g_pet.alive, (int)g_pet.sleeping);
}

// ── Loop ──────────────────────────────────────────────────────────────────────
void loop() {
    if (g_needs_redraw) {
        g_needs_redraw = false;
        if (g_just_hatched)    draw_hatch_screen();
        else if (!g_pet.alive) draw_dead_screen();
        else                   draw_main_screen();
    }

    // Only deep-sleep immediately when the user just pressed SLEEP this session.
    // On wakeup from button press, stay awake so the user can interact.
    if (g_pet.alive && g_pet.sleeping && g_just_slept) {
        g_just_slept = false;
        go_to_sleep();
    }

    uint8_t btns    = read_buttons();
    uint8_t pressed = btns & ~g_last_btns;
    g_last_btns     = btns;

    if (pressed) {
        s_last_activity = millis();
        handle_buttons(pressed);
    }

    if (millis() - s_last_activity >= SLEEP_AFTER_MS) {
        go_to_sleep();
    }

    delay(10);
}
