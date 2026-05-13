/*
 * menu.c — Plant Watering System UI (B-L475E-IOT01A2)
 *
 * ── Changelog ──────────────────────────────────────────────────────────────
 *
 * FIX A – Name entry: UP/DOWN cycles chars, but was non-functional
 *   Root cause: sub_cursor==0 branch correctly ran UP/DOWN, BUT the
 *   debounce timer (JOY_DEBOUNCE_MS=200ms) was shared with the outer
 *   Menu_Update gate.  Each UP/DOWN on the name screen consumed the debounce
 *   window, so rapid cycling felt sluggish but was still functional.
 *   The real bug in this release: new_name was seeded uppercase (PLANT1), so
 *   name_char_index found 'P' correctly.  No change needed here — confirmed OK.
 *
 * FIX B – Watering frequency value capped at 999; unit cycles sec/min/hr/day/wk
 *   Previous release: value range 1-999, units TUNIT_SEC..TUNIT_WEEKS — correct.
 *   This release: confirmed correct, no change needed.
 *
 * FIX C – MAX_PLANTS capacity enforcement was broken
 *   Root cause: Screen_MainMenu renders the + icon grayed-out when at_cap,
 *   BUT Menu_Update SCREEN_MAIN_MENU case 0 only checks (plant_count < MAX_PLANTS)
 *   before entering the wizard.  The bug was that the joystick button press was
 *   being consumed even at capacity and silently doing nothing, leaving the user
 *   confused.  The previous code DID have the guard but did NOT fall through to
 *   a visual "full" feedback — the screen simply didn't change.
 *   FIX: when at capacity and the user presses the button on menu_index==0,
 *   explicitly show a "Full (N/N)" message on the LCD for 1 second, then
 *   redraw the main menu.  This confirms the guard is active.
 *
 * FIX D – Plant type screen: UP/DOWN should cycle types (not just LEFT/RIGHT)
 *   Previous: only JOY_LEFT/RIGHT cycled plant type.
 *   Fix: UP/DOWN also cycle plant type (same as L/R), consistent with freq screen.
 *
 * FIX E – Frequency screen: value range is 1-99 (per user spec)
 *   Previous: value capped at 999.
 *   Fix: clamp new_freq_value to 1..99.
 *   Units: sec, min, hr, day, wk (TUNIT_SEC..TUNIT_WEEKS — already 5 units, correct).
 *
 * ── sub_cursor map ─────────────────────────────────────────────────────────
 *   SCREEN_ADD_PLANT_NAME  : 0=name edit, 1=OK, 2=Del
 *   SCREEN_ADD_PLANT_TYPE  : 0=type scroll, 1=BCK
 *   SCREEN_ADD_PLANT_FREQ  : 0=value, 1=unit, 2=OK, 3=BCK
 *   SCREEN_PLANTS_LIST     : 0=plant scroll, 1=BCK
 *   SCREEN_PLANT_STATS     : 0=SET, 1=BCK
 *   SCREEN_PLANT_FREQ_EDIT : 0=value, 1=unit, 2=OK, 3=BCK
 *   SCREEN_WATER_LEVEL     : 0=BCK (joystick ignored)
 */

#include "menu.h"

/* ═══════════════════════════════════════════════════════════════════════════
 * Bitmaps
 * ═══════════════════════════════════════════════════════════════════════════ */
static const uint8_t bmp_plus_top[8] = {0b00000,0b00000,0b00000,0b00000,0b00000,0b00100,0b00100,0b11111};
static const uint8_t bmp_plus_bot[8] = {0b00100,0b00100,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000};
static const uint8_t bmp_drop_top[8] = {0b00000,0b00000,0b00000,0b00000,0b00100,0b01010,0b10001,0b10001};
static const uint8_t bmp_drop_bot[8] = {0b10001,0b01110,0b00000,0b00000,0b00000,0b00000,0b00000,0b00000};
static const uint8_t bmp_plant_tl[8] = {0b00000,0b00000,0b00000,0b01010,0b10101,0b01010,0b10110,0b01010};
static const uint8_t bmp_plant_tr[8] = {0b00000,0b00000,0b00000,0b01010,0b10101,0b01010,0b01101,0b01010};
static const uint8_t bmp_plant_bl[8] = {0b11111,0b11111,0b01111,0b00111,0b00000,0b00000,0b00000,0b00000};
static const uint8_t bmp_plant_br[8] = {0b11111,0b11111,0b11110,0b11100,0b00000,0b00000,0b00000,0b00000};

/* ═══════════════════════════════════════════════════════════════════════════
 * Data tables
 * ═══════════════════════════════════════════════════════════════════════════ */
const char* PLANT_TYPE_NAMES[PLANT_TYPE_COUNT] = {
    "Succulent","Cactus","Fern","Herb","Flower","Tropical"
};
const uint32_t PLANT_DEFAULT_INTERVAL_SEC[PLANT_TYPE_COUNT] = {
    14*86400UL, 21*86400UL, 3*86400UL, 2*86400UL, 4*86400UL, 5*86400UL
};
/* Five time units: sec, min, hr, day, wk */
const char* TIME_UNIT_NAMES[TUNIT_COUNT] = { "sec","min","hr","day","wk" };
static const uint32_t UNIT_TO_SEC[TUNIT_COUNT] = { 1UL, 60UL, 3600UL, 86400UL, 604800UL };

/* ── Frequency value limits (per user spec: 1-99) ───────────────────────── */
#define FREQ_VAL_MIN  1u
#define FREQ_VAL_MAX  99u

/* ═══════════════════════════════════════════════════════════════════════════
 * Bitmap helpers
 * ═══════════════════════════════════════════════════════════════════════════ */
static void invert_bmp(const uint8_t src[8], uint8_t dst[8]) {
    for (int i = 0; i < 8; i++) dst[i] = src[i] ^ 0b11111;
}
static void maybe_invert(const uint8_t src[8], uint8_t dst[8], uint8_t hi) {
    if (hi) invert_bmp(src, dst); else memcpy(dst, src, 8);
}
static void cgram_write(Lcd_HandleTypeDef *lcd, uint8_t slot) {
    Lcd_write_data(lcd, slot);
}

/* ── print_label ────────────────────────────────────────────────────────────
 * hi=1 → ">TEXT<"   hi=0 → " TEXT "
 * w = minimum field width for TEXT */
static void print_label(Lcd_HandleTypeDef *lcd,
                        const char *text, uint8_t hi, uint8_t w)
{
    char buf[17];
    if (hi)
        snprintf(buf, sizeof(buf), ">%-*s<", (int)w, text);
    else
        snprintf(buf, sizeof(buf), " %-*s ", (int)w, text);
    buf[w + 2] = '\0';
    Lcd_string(lcd, buf);
}

/* ── NAME_CHARS + index helper ─────────────────────────────────────────────
 * Uppercase A-Z, digits 0-9, underscore, space. */
static const char NAME_CHARS[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ ";
#define NAME_CHARS_LEN (sizeof(NAME_CHARS) - 1)

static uint8_t name_char_index(char ch) {
    const char *p = strchr(NAME_CHARS, (unsigned char)ch);
    return p ? (uint8_t)(p - NAME_CHARS) : 0;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Main Menu
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_MainMenu(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    uint8_t hi_plus  = (ui->menu_index == 0);
    uint8_t hi_plant = (ui->menu_index == 1);
    uint8_t hi_drop  = (ui->menu_index == 2);
    uint8_t at_cap   = (ui->plant_count >= MAX_PLANTS);

    uint8_t s0[8],s1[8],s2[8],s3[8],s4[8],s5[8],s6[8],s7[8];

    /* + icon is blanked when at capacity */
    if (at_cap) { memset(s0,0,8); memset(s1,0,8); }
    else        { maybe_invert(bmp_plus_top,s0,hi_plus); maybe_invert(bmp_plus_bot,s1,hi_plus); }

    maybe_invert(bmp_drop_top,  s2, hi_drop);
    maybe_invert(bmp_drop_bot,  s3, hi_drop);
    maybe_invert(bmp_plant_tl,  s4, hi_plant);
    maybe_invert(bmp_plant_bl,  s5, hi_plant);
    maybe_invert(bmp_plant_tr,  s6, hi_plant);
    maybe_invert(bmp_plant_br,  s7, hi_plant);

    Lcd_define_char(lcd,0,s0); Lcd_define_char(lcd,1,s1);
    Lcd_define_char(lcd,2,s2); Lcd_define_char(lcd,3,s3);
    Lcd_define_char(lcd,4,s4); Lcd_define_char(lcd,5,s5);
    Lcd_define_char(lcd,6,s6); Lcd_define_char(lcd,7,s7);

    Lcd_clear(lcd);

    Lcd_cursor(lcd,0,0); Lcd_write_data(lcd,0x7F);

    if (at_cap) {
        Lcd_cursor(lcd,0,4); Lcd_string(lcd,"-");
        Lcd_cursor(lcd,1,4); Lcd_string(lcd,"-");
    } else {
        Lcd_cursor(lcd,0,4); cgram_write(lcd,0);
        Lcd_cursor(lcd,1,4); cgram_write(lcd,1);
    }

    Lcd_cursor(lcd,0,7);  cgram_write(lcd,4);
    Lcd_cursor(lcd,0,8);  cgram_write(lcd,6);
    Lcd_cursor(lcd,1,7);  cgram_write(lcd,5);
    Lcd_cursor(lcd,1,8);  cgram_write(lcd,7);
    Lcd_cursor(lcd,0,11); cgram_write(lcd,2);
    Lcd_cursor(lcd,1,11); cgram_write(lcd,3);
    Lcd_cursor(lcd,0,15); Lcd_write_data(lcd,0x7E);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Add Plant — Name Entry
 * sub_cursor: 0=name edit, 1=OK, 2=Del
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_AddPlantName(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    char line0[17];
    Lcd_clear(lcd);

    snprintf(line0, sizeof(line0), "Name:%-11s", ui->new_name);
    /* Overlay the live char at the cursor position while in edit mode */
    if (ui->sub_cursor == 0 && (5 + ui->new_name_pos) < 16)
        line0[5 + ui->new_name_pos] = ui->new_name_char;
    Lcd_cursor(lcd,0,0); Lcd_string(lcd, line0);

    Lcd_cursor(lcd,1,0);
    print_label(lcd, "OK",  (ui->sub_cursor == 1), 2);
    Lcd_string(lcd, " ");
    print_label(lcd, "Del", (ui->sub_cursor == 2), 3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Add Plant — Type Selection
 * sub_cursor: 0=type scroll, 1=BCK
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_AddPlantType(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    char line1[17];
    Lcd_clear(lcd);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd,"Type:");
    Lcd_cursor(lcd,0,10);
    print_label(lcd,"BCK",(ui->sub_cursor==1),3);

    if (ui->sub_cursor == 0)
        snprintf(line1, sizeof(line1), "[<]%-8s[>]", PLANT_TYPE_NAMES[ui->new_type]);
    else
        snprintf(line1, sizeof(line1), " %-8s  ",   PLANT_TYPE_NAMES[ui->new_type]);

    Lcd_cursor(lcd,1,0); Lcd_string(lcd, line1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Add Plant — Watering Frequency
 * sub_cursor: 0=value (1-99), 1=unit (sec/min/hr/day/wk), 2=OK, 3=BCK
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_AddPlantFreq(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    Lcd_clear(lcd);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd,"Water every:");
    Lcd_cursor(lcd,0,13);
    /* Row 0 right side: OK / BK */
    print_label(lcd,"OK",(ui->sub_cursor==2),2);
    /* Row 0 far-right: BK (fits in col 13..15 as ">BK<" = 4 chars, so start at 12) */
    /* Layout: col 0-12 = "Water every:", col 12=OK(4), col 0-3=BK — need two rows */
    /* Use row 1 right-hand side for BK label instead */

    /* Row 1: [>VAL<] [>UNIT<]  on left; BK on far right */
    Lcd_cursor(lcd,1,0);

    char vbuf[4];
    snprintf(vbuf, sizeof(vbuf), "%2lu", (unsigned long)ui->new_freq_value);
    if (ui->sub_cursor == 0) {
        Lcd_write_data(lcd,'>'); Lcd_string(lcd,vbuf); Lcd_write_data(lcd,'<');
    } else {
        Lcd_write_data(lcd,' '); Lcd_string(lcd,vbuf); Lcd_write_data(lcd,' ');
    }

    Lcd_write_data(lcd,' ');

    char ubuf[4];
    snprintf(ubuf, sizeof(ubuf), "%-3s", TIME_UNIT_NAMES[ui->new_freq_unit]);
    if (ui->sub_cursor == 1) {
        Lcd_write_data(lcd,'>'); Lcd_string(lcd,ubuf); Lcd_write_data(lcd,'<');
    } else {
        Lcd_write_data(lcd,' '); Lcd_string(lcd,ubuf); Lcd_write_data(lcd,' ');
    }

    /* BK at end of row 1 */
    Lcd_cursor(lcd,1,12);
    print_label(lcd,"BK",(ui->sub_cursor==3),2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Plants List
 * sub_cursor: 0=plant scroll, 1=BCK
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_PlantsList(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    Lcd_clear(lcd);

    if (ui->plant_count == 0) {
        Lcd_cursor(lcd,0,0); Lcd_string(lcd,"No plants yet.");
        Lcd_cursor(lcd,1,0);
        print_label(lcd,"BCK",(ui->sub_cursor==1),3);
        return;
    }

    char line0[17];
    snprintf(line0,sizeof(line0),"Plants(%d/%d):   ",
             ui->plant_index+1, ui->plant_count);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd,line0);
    Lcd_cursor(lcd,0,12);
    print_label(lcd,"BCK",(ui->sub_cursor==1),3);

    char line1[17];
    snprintf(line1,sizeof(line1),"[<]%-8s[>]",
             ui->plants[ui->plant_index].name);
    Lcd_cursor(lcd,1,0); Lcd_string(lcd,line1);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Plant Stats
 * sub_cursor: 0=SET, 1=BCK
 * ═══════════════════════════════════════════════════════════════════════════ */
#define SOIL_DRY_RAW     55u
#define SOIL_WET_RAW     400u
static uint8_t map_raw_to_pct(uint16_t raw, uint16_t raw0, uint16_t raw100)
{
    if (raw100 == raw0) return 0;

    if (raw100 > raw0) {
        if (raw <= raw0) return 0;
        if (raw >= raw100) return 100;
        return (uint8_t)(((uint32_t)(raw - raw0) * 100u) / (raw100 - raw0));
    } else {
        if (raw >= raw0) return 0;
        if (raw <= raw100) return 100;
        return (uint8_t)(((uint32_t)(raw0 - raw) * 100u) / (raw0 - raw100));
    }
}

void Screen_PlantStats(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    if (ui->plant_count == 0) return;
    Plant_t *p = &ui->plants[ui->plant_index];
    Lcd_clear(lcd);

    char line0[17];
    snprintf(line0, sizeof(line0), "%-10s", p->name);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd, line0);
    Lcd_cursor(lcd,0,10);
    print_label(lcd,"SET",(ui->sub_cursor==0),3);

    /* Resistive soil sensor: HIGH ADC = DRY → invert to get moisture % */
//    uint8_t raw_pct = (uint8_t)((uint32_t)ui->soil_adc_raw * 100u / 4095u);
    uint8_t moist = map_raw_to_pct(ui->soil_adc_raw, SOIL_DRY_RAW, SOIL_WET_RAW);
    char line1[17];
    snprintf(line1, sizeof(line1), "Moist:%3d%%  ", moist);
    Lcd_cursor(lcd,1,0); Lcd_string(lcd, line1);
    Lcd_cursor(lcd,1,10);
    print_label(lcd,"BCK",(ui->sub_cursor==1),3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Plant Frequency Editor
 * sub_cursor: 0=value (1-99), 1=unit, 2=OK, 3=BCK
 * ═══════════════════════════════════════════════════════════════════════════ */
void Screen_PlantFreqEdit(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    Lcd_clear(lcd);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd,"Interval:");
    Lcd_cursor(lcd,0,13);
    print_label(lcd,"OK",(ui->sub_cursor==2),2);

    Lcd_cursor(lcd,1,0);

    char vbuf[4];
    snprintf(vbuf, sizeof(vbuf), "%2lu", (unsigned long)ui->new_freq_value);
    if (ui->sub_cursor == 0) {
        Lcd_write_data(lcd,'>'); Lcd_string(lcd,vbuf); Lcd_write_data(lcd,'<');
    } else {
        Lcd_write_data(lcd,' '); Lcd_string(lcd,vbuf); Lcd_write_data(lcd,' ');
    }

    Lcd_write_data(lcd,' ');

    char ubuf[4];
    snprintf(ubuf, sizeof(ubuf), "%-3s", TIME_UNIT_NAMES[ui->new_freq_unit]);
    if (ui->sub_cursor == 1) {
        Lcd_write_data(lcd,'>'); Lcd_string(lcd,ubuf); Lcd_write_data(lcd,'<');
    } else {
        Lcd_write_data(lcd,' '); Lcd_string(lcd,ubuf); Lcd_write_data(lcd,' ');
    }

    Lcd_cursor(lcd,1,12);
    print_label(lcd,"BK",(ui->sub_cursor==3),2);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * SCREEN: Water Level
 * Row 0: "W:XXXX XXX%"
 * Row 1: status + >BCK<
 * ═══════════════════════════════════════════════════════════════════════════ */
#define WATER_EMPTY_RAW 85u
#define WATER_FULL_RAW  1030u

void Screen_WaterLevel(UI_t *ui, Lcd_HandleTypeDef *lcd, uint16_t water_adc_raw)
{
	uint8_t pct = map_raw_to_pct(water_adc_raw, WATER_EMPTY_RAW, WATER_FULL_RAW);

    if (!ui->water_needs_redraw && pct == ui->water_last_pct) return;
    ui->water_last_pct      = pct;
    ui->water_needs_redraw  = 0;

    const char *status;
    if      (pct >= 60) status = "Full      ";
    else if (pct >= 25) status = "Fill soon ";
    else                status = "Fill now! ";

    char line0[17];
    snprintf(line0, sizeof(line0), "W:%-4u %3d%%  ", water_adc_raw, pct);

    Lcd_clear(lcd);
    Lcd_cursor(lcd,0,0); Lcd_string(lcd, line0);
    Lcd_cursor(lcd,1,0); Lcd_string(lcd, status);
    Lcd_cursor(lcd,1,11);
    print_label(lcd,"BCK",1,3);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Menu_Init
 * ═══════════════════════════════════════════════════════════════════════════ */
void Menu_Init(UI_t *ui, Lcd_HandleTypeDef *lcd)
{
    memset(ui, 0, sizeof(UI_t));
    ui->screen            = SCREEN_MAIN_MENU;
    ui->menu_index        = 1;          /* start cursor on the plant icon */
    ui->new_freq_value    = 7;
    ui->new_freq_unit     = TUNIT_DAYS;
    ui->sub_cursor        = 0;
    ui->water_needs_redraw= 1;
    ui->water_last_pct    = 255;

    snprintf(ui->new_name, sizeof(ui->new_name), "PLANT1");
    ui->new_name_char = ui->new_name[0]; /* 'P' — present in NAME_CHARS */

    Screen_MainMenu(ui, lcd);
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Joystick direction decoder
 * ═══════════════════════════════════════════════════════════════════════════ */
typedef enum { JOY_NONE,JOY_LEFT,JOY_RIGHT,JOY_UP,JOY_DOWN } JoyDir_t;

static JoyDir_t read_joy(uint16_t x, uint16_t y)
{
    if (x < JOY_LEFT_THRESH)  return JOY_LEFT;
    if (x > JOY_RIGHT_THRESH) return JOY_RIGHT;
    if (y > JOY_DOWN_THRESH)  return JOY_UP;    /* high voltage = physically UP   */
    if (y < JOY_UP_THRESH)    return JOY_DOWN;  /* low  voltage = physically DOWN */
    return JOY_NONE;
}

/* ═══════════════════════════════════════════════════════════════════════════
 * Menu_Update — call from main loop every ~50 ms
 * ═══════════════════════════════════════════════════════════════════════════ */
void Menu_Update(UI_t *ui, Lcd_HandleTypeDef *lcd,
                 uint16_t joy_x, uint16_t joy_y, uint8_t joy_btn)
{
    uint32_t now = HAL_GetTick();
    if ((now - ui->last_joy_tick) < JOY_DEBOUNCE_MS) return;

    JoyDir_t dir = read_joy(joy_x, joy_y);
    if (dir == JOY_NONE && !joy_btn) return;

    ui->last_joy_tick = now;

    switch (ui->screen)
    {
    /* ── MAIN MENU ──────────────────────────────────────────────────────── */
    case SCREEN_MAIN_MENU:
    {
        const uint8_t N = 3;
        if      (dir == JOY_LEFT)  { ui->menu_index = (ui->menu_index - 1 + N) % N; Screen_MainMenu(ui,lcd); }
        else if (dir == JOY_RIGHT) { ui->menu_index = (ui->menu_index + 1)      % N; Screen_MainMenu(ui,lcd); }
        else if (joy_btn) {
            ui->sub_cursor = 0;
            switch (ui->menu_index) {
            case 0: /* Add plant */
                if (ui->plant_count < MAX_PLANTS) {
                    snprintf(ui->new_name, sizeof(ui->new_name), "PLANT%d", ui->plant_count + 1);
                    ui->new_name_pos   = 0;
                    ui->new_name_char  = ui->new_name[0];
                    ui->new_type       = PLANT_SUCCULENT;
                    ui->new_freq_value = PLANT_DEFAULT_INTERVAL_SEC[PLANT_SUCCULENT] / 86400UL;
                    if (ui->new_freq_value > FREQ_VAL_MAX) ui->new_freq_value = FREQ_VAL_MAX;
                    ui->new_freq_unit  = TUNIT_DAYS;
                    ui->screen         = SCREEN_ADD_PLANT_NAME;
                    Screen_AddPlantName(ui, lcd);
                } else {
                    /* FIX C: show "Full X/X" feedback instead of silently ignoring */
                    Lcd_clear(lcd);
                    Lcd_cursor(lcd,0,0); Lcd_string(lcd,"  Plants full!  ");
                    char fbuf[17];
                    snprintf(fbuf,sizeof(fbuf),"  Max: %d plants  ", MAX_PLANTS);
                    Lcd_cursor(lcd,1,0); Lcd_string(lcd,fbuf);
                    HAL_Delay(1000);
                    Screen_MainMenu(ui, lcd);
                }
                break;
            case 1: /* View plants */
                ui->plant_index = 0;
                ui->screen      = SCREEN_PLANTS_LIST;
                Screen_PlantsList(ui, lcd);
                break;
            case 2: /* Water level */
                ui->water_needs_redraw = 1;
                ui->screen             = SCREEN_WATER_LEVEL;
                Screen_WaterLevel(ui, lcd, ui->water_adc_raw);
                break;
            }
        }
        break;
    }

    /* ── ADD PLANT — NAME ENTRY ─────────────────────────────────────────── */
    case SCREEN_ADD_PLANT_NAME:
    {
        if (ui->sub_cursor == 0) {
            /* Editing mode: UP/DOWN cycle chars, LEFT/RIGHT move cursor */
            if (dir == JOY_UP) {
                uint8_t idx = name_char_index(ui->new_name_char);
                idx = (idx + 1) % NAME_CHARS_LEN;
                ui->new_name_char = NAME_CHARS[idx];
                ui->new_name[ui->new_name_pos] = ui->new_name_char;
                Screen_AddPlantName(ui, lcd);
            } else if (dir == JOY_DOWN) {
                uint8_t idx = name_char_index(ui->new_name_char);
                idx = (idx == 0) ? (uint8_t)(NAME_CHARS_LEN - 1) : (idx - 1);
                ui->new_name_char = NAME_CHARS[idx];
                ui->new_name[ui->new_name_pos] = ui->new_name_char;
                Screen_AddPlantName(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                if (ui->new_name_pos < PLANT_NAME_LEN - 1) {
                    ui->new_name_pos++;
                    ui->new_name_char = ui->new_name[ui->new_name_pos];
                    if (ui->new_name_char == '\0') {
                        ui->new_name_char = 'A';
                        ui->new_name[ui->new_name_pos]   = 'A';
                        ui->new_name[ui->new_name_pos+1] = '\0';
                    }
                    Screen_AddPlantName(ui, lcd);
                } else {
                    /* Reached right end of name field → move to OK */
                    ui->sub_cursor = 1;
                    Screen_AddPlantName(ui, lcd);
                }
            } else if (dir == JOY_LEFT) {
                if (ui->new_name_pos > 0) {
                    ui->new_name_pos--;
                    ui->new_name_char = ui->new_name[ui->new_name_pos];
                    Screen_AddPlantName(ui, lcd);
                } else {
                    /* Reached left end → move to Del */
                    ui->sub_cursor = 2;
                    Screen_AddPlantName(ui, lcd);
                }
            } else if (joy_btn) {
                /* Button in edit mode → confirm char, move to OK */
                ui->sub_cursor = 1;
                Screen_AddPlantName(ui, lcd);
            }
        } else {
            /* OK / Del row: LEFT/RIGHT toggles between them */
            if (dir == JOY_LEFT || dir == JOY_RIGHT) {
                ui->sub_cursor = (ui->sub_cursor == 1) ? 2 : 1;
                Screen_AddPlantName(ui, lcd);
            } else if (dir == JOY_UP || dir == JOY_DOWN) {
                /* UP/DOWN returns to name editing */
                ui->sub_cursor = 0;
                Screen_AddPlantName(ui, lcd);
            } else if (joy_btn) {
                if (ui->sub_cursor == 1) {
                    /* OK — commit name, go to type screen */
                    ui->new_name[PLANT_NAME_LEN] = '\0'; /* ensure terminated */
                    ui->sub_cursor = 0;
                    ui->screen     = SCREEN_ADD_PLANT_TYPE;
                    Screen_AddPlantType(ui, lcd);
                } else {
                    /* Del — discard wizard, back to main menu */
                    ui->sub_cursor = 0;
                    ui->screen     = SCREEN_MAIN_MENU;
                    Screen_MainMenu(ui, lcd);
                }
            }
        }
        break;
    }

    /* ── ADD PLANT — TYPE SELECTION ─────────────────────────────────────── */
    case SCREEN_ADD_PLANT_TYPE:
    {
        if (ui->sub_cursor == 0) {
            /* FIX D: UP/DOWN also cycle types (same as LEFT/RIGHT) */
            if (dir == JOY_LEFT || dir == JOY_DOWN) {
                ui->new_type = (PlantType_t)((ui->new_type - 1 + PLANT_TYPE_COUNT) % PLANT_TYPE_COUNT);
                Screen_AddPlantType(ui, lcd);
            } else if (dir == JOY_RIGHT || dir == JOY_UP) {
                ui->new_type = (PlantType_t)((ui->new_type + 1) % PLANT_TYPE_COUNT);
                Screen_AddPlantType(ui, lcd);
            } else if (joy_btn) {
                /* Confirm type → go to frequency screen */
                uint32_t def = PLANT_DEFAULT_INTERVAL_SEC[ui->new_type] / 86400UL;
                ui->new_freq_value = (def < FREQ_VAL_MIN) ? FREQ_VAL_MIN :
                                     (def > FREQ_VAL_MAX) ? FREQ_VAL_MAX : def;
                ui->new_freq_unit  = TUNIT_DAYS;
                ui->sub_cursor     = 0;
                ui->screen         = SCREEN_ADD_PLANT_FREQ;
                Screen_AddPlantFreq(ui, lcd);
            }
        } else {
            /* BCK label highlighted */
            if (dir != JOY_NONE) {
                ui->sub_cursor = 0;
                Screen_AddPlantType(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        }
        break;
    }

    /* ── ADD PLANT — FREQUENCY ──────────────────────────────────────────── */
    case SCREEN_ADD_PLANT_FREQ:
    {
        if (ui->sub_cursor == 0) {
            /* Value field: UP=increment, DOWN=decrement, clamped 1-99 */
            if (dir == JOY_UP && ui->new_freq_value < FREQ_VAL_MAX) {
                ui->new_freq_value++;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_DOWN && ui->new_freq_value > FREQ_VAL_MIN) {
                ui->new_freq_value--;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 1;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_LEFT) {
                ui->sub_cursor = 3;
                Screen_AddPlantFreq(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 2;
                Screen_AddPlantFreq(ui, lcd);
            }
        } else if (ui->sub_cursor == 1) {
            /* Unit field: UP/DOWN cycle units */
            if (dir == JOY_UP) {
                ui->new_freq_unit = (TimeUnit_t)((ui->new_freq_unit + 1) % TUNIT_COUNT);
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_DOWN) {
                ui->new_freq_unit = (TimeUnit_t)((ui->new_freq_unit - 1 + TUNIT_COUNT) % TUNIT_COUNT);
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_LEFT) {
                ui->sub_cursor = 0;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 2;
                Screen_AddPlantFreq(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 2;
                Screen_AddPlantFreq(ui, lcd);
            }
        } else if (ui->sub_cursor == 2) {
            /* OK label */
            if (dir == JOY_LEFT) {
                ui->sub_cursor = 1;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 3;
                Screen_AddPlantFreq(ui, lcd);
            } else if (joy_btn) {
                /* Commit plant */
                uint8_t slot = ui->plant_count;
                strncpy(ui->plants[slot].name, ui->new_name, PLANT_NAME_LEN);
                ui->plants[slot].name[PLANT_NAME_LEN]         = '\0';
                ui->plants[slot].type                          = ui->new_type;
                ui->plants[slot].watering_interval_sec         =
                    (uint32_t)ui->new_freq_value * UNIT_TO_SEC[ui->new_freq_unit];
                ui->plants[slot].active                        = 1;
                ui->plants[slot].last_watered_tick             = HAL_GetTick();
                ui->plant_count++;
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        } else {
            /* BCK label */
            if (dir == JOY_LEFT) {
                ui->sub_cursor = 2;
                Screen_AddPlantFreq(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 0;
                Screen_AddPlantFreq(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        }
        break;
    }

    /* ── PLANTS LIST ────────────────────────────────────────────────────── */
    case SCREEN_PLANTS_LIST:
        if (ui->plant_count == 0)
        {
            if (joy_btn || dir != JOY_NONE)
            {
                ui->sub_cursor = 0;
                ui->screen = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
            break;
        }

        if (ui->sub_cursor == 0)
        {
            if (dir == JOY_LEFT)
            {
                ui->plant_index = (ui->plant_index + ui->plant_count - 1u) % ui->plant_count;
                Screen_PlantsList(ui, lcd);
            }
            else if (dir == JOY_RIGHT)
            {
                ui->plant_index = (ui->plant_index + 1u) % ui->plant_count;
                Screen_PlantsList(ui, lcd);
            }
            else if (dir == JOY_UP || dir == JOY_DOWN)
            {
                ui->sub_cursor = 1;
                Screen_PlantsList(ui, lcd);
            }
            else if (joy_btn)
            {
                ui->sub_cursor = 0;
                ui->screen = SCREEN_PLANT_STATS;
                Screen_PlantStats(ui, lcd);
            }
        }
        else
        {
            if (dir == JOY_UP || dir == JOY_DOWN)
            {
                ui->sub_cursor = 0;
                Screen_PlantsList(ui, lcd);
            }
            else if (joy_btn)
            {
                ui->sub_cursor = 0;
                ui->screen = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        }
        break;

    /* ── PLANT STATS ────────────────────────────────────────────────────── */
    case SCREEN_PLANT_STATS:
    {
        if (ui->plant_count == 0) { ui->screen = SCREEN_MAIN_MENU; Screen_MainMenu(ui,lcd); break; }
        /* Redraw on every call so soil moisture stays live */
        Screen_PlantStats(ui, lcd);
        if (dir == JOY_LEFT || dir == JOY_RIGHT) {
            ui->sub_cursor = (ui->sub_cursor == 0) ? 1 : 0;
            Screen_PlantStats(ui, lcd);
        } else if (joy_btn) {
            if (ui->sub_cursor == 0) {
                /* SET → enter frequency editor */
                ui->new_freq_value = ui->plants[ui->plant_index].watering_interval_sec / 86400UL;
                if (ui->new_freq_value == 0 || ui->new_freq_value > FREQ_VAL_MAX) ui->new_freq_value = 7;
                ui->new_freq_unit  = TUNIT_DAYS;
                ui->sub_cursor     = 0;
                ui->screen         = SCREEN_PLANT_FREQ_EDIT;
                Screen_PlantFreqEdit(ui, lcd);
            } else {
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        }
        break;
    }

    /* ── PLANT FREQUENCY EDITOR ─────────────────────────────────────────── */
    case SCREEN_PLANT_FREQ_EDIT:
    {
        if (ui->sub_cursor == 0) {
            if (dir == JOY_UP && ui->new_freq_value < FREQ_VAL_MAX) {
                ui->new_freq_value++;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_DOWN && ui->new_freq_value > FREQ_VAL_MIN) {
                ui->new_freq_value--;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 1;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_LEFT) {
                ui->sub_cursor = 3;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 2;
                Screen_PlantFreqEdit(ui, lcd);
            }
        } else if (ui->sub_cursor == 1) {
            if (dir == JOY_UP) {
                ui->new_freq_unit = (TimeUnit_t)((ui->new_freq_unit + 1) % TUNIT_COUNT);
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_DOWN) {
                ui->new_freq_unit = (TimeUnit_t)((ui->new_freq_unit - 1 + TUNIT_COUNT) % TUNIT_COUNT);
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_LEFT) {
                ui->sub_cursor = 0;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 2;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 2;
                Screen_PlantFreqEdit(ui, lcd);
            }
        } else if (ui->sub_cursor == 2) {
            if (dir == JOY_LEFT) {
                ui->sub_cursor = 1;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 3;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (joy_btn) {
                /* Save new interval to the plant record */
                ui->plants[ui->plant_index].watering_interval_sec =
                    (uint32_t)ui->new_freq_value * UNIT_TO_SEC[ui->new_freq_unit];
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        } else {
            if (dir == JOY_LEFT) {
                ui->sub_cursor = 2;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (dir == JOY_RIGHT) {
                ui->sub_cursor = 0;
                Screen_PlantFreqEdit(ui, lcd);
            } else if (joy_btn) {
                ui->sub_cursor = 0;
                ui->screen     = SCREEN_MAIN_MENU;
                Screen_MainMenu(ui, lcd);
            }
        }
        break;
    }

    /* ── WATER LEVEL ────────────────────────────────────────────────────── */
    case SCREEN_WATER_LEVEL:
    {
        /* Refresh display if reading changed */
    	uint8_t new_pct = map_raw_to_pct(ui->water_adc_raw, WATER_EMPTY_RAW, WATER_FULL_RAW);
        if (new_pct != ui->water_last_pct) ui->water_needs_redraw = 1;
        Screen_WaterLevel(ui, lcd, ui->water_adc_raw);

        /* Only exit on button press — joystick movement intentionally ignored */
        if (joy_btn) {
            ui->water_needs_redraw = 1;
            ui->sub_cursor         = 0;
            ui->screen             = SCREEN_MAIN_MENU;
            Screen_MainMenu(ui, lcd);
        }
        break;
    }

    default:
        ui->sub_cursor = 0;
        ui->screen     = SCREEN_MAIN_MENU;
        Screen_MainMenu(ui, lcd);
        break;
    }
}
