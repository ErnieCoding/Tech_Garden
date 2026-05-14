#ifndef MENU_H
#define MENU_H

#include "stm32l4xx_hal.h"
#include "lcd.h"
#include <stdint.h>
#include <string.h>
#include <stdio.h>

/* ── Configuration ──────────────────────────────────────────────────────── */
#define MAX_PLANTS        4
#define PLANT_NAME_LEN   10

#define JOY_LEFT_THRESH   1500u
#define JOY_RIGHT_THRESH  2500u
#define JOY_UP_THRESH     1500u
#define JOY_DOWN_THRESH   2500u
#define JOY_DEBOUNCE_MS   120u

/* ── Plant types ────────────────────────────────────────────────────────── */
typedef enum {
    PLANT_SUCCULENT = 0,
    PLANT_CACTUS,
    PLANT_FERN,
    PLANT_HERB,
    PLANT_FLOWER,
    PLANT_TROPICAL,
    PLANT_TYPE_COUNT
} PlantType_t;

/* ── Time units ─────────────────────────────────────────────────────────── */
typedef enum {
    TUNIT_SEC = 0,
    TUNIT_MIN,
    TUNIT_HR,
    TUNIT_DAYS,
    TUNIT_WEEKS,
    TUNIT_COUNT
} TimeUnit_t;

/* ── Plant record ───────────────────────────────────────────────────────── */
typedef struct {
    char        name[PLANT_NAME_LEN + 1];
    PlantType_t type;
    uint32_t    watering_interval_sec;
    uint8_t     active;
    uint32_t    last_watered_tick;
} Plant_t;

/* ── Screen IDs ─────────────────────────────────────────────────────────── */
typedef enum {
    SCREEN_MAIN_MENU = 0,
    SCREEN_ADD_PLANT_NAME,
    SCREEN_ADD_PLANT_TYPE,
    SCREEN_ADD_PLANT_FREQ,
    SCREEN_PLANTS_LIST,
    SCREEN_PLANT_STATS,
    SCREEN_PLANT_SETTINGS,
    SCREEN_PLANT_FREQ_EDIT,
    SCREEN_WATER_LEVEL
} Screen_t;

/* ── UI state ───────────────────────────────────────────────────────────── */
typedef struct {
    Screen_t    screen;

    /* Main menu cursor (0=Plus, 1=Plant, 2=Droplet) */
    uint8_t     menu_index;

    /* Sub-screen cursor: index of highlighted label on current screen */
    uint8_t     sub_cursor;

    /* Plant list */
    Plant_t     plants[MAX_PLANTS];
    uint8_t     plant_count;
    uint8_t     plant_index;

    /* New-plant wizard */
    char        new_name[PLANT_NAME_LEN + 1];
    uint8_t     new_name_pos;
    char        new_name_char;
    PlantType_t new_type;

    /* Frequency picker (shared by add-wizard and freq-edit screens) */
    uint32_t    new_freq_value;
    TimeUnit_t  new_freq_unit;
    uint8_t     freq_editing_value;  */

    /* Water level sensor — updated by main.c every loop iteration */
    uint16_t    water_adc_raw;
    uint8_t     water_last_pct;
    uint8_t     water_needs_redraw;

    /* Soil moisture — updated by main.c, displayed in plant stats */
    uint16_t    soil_adc_raw;

    /* Joystick debounce */
    uint32_t    last_joy_tick;
} UI_t;

/* ── Data tables (defined in menu.c) ───────────────────────────────────── */
extern const char*    PLANT_TYPE_NAMES[PLANT_TYPE_COUNT];
extern const uint32_t PLANT_DEFAULT_INTERVAL_SEC[PLANT_TYPE_COUNT];
extern const char*    TIME_UNIT_NAMES[TUNIT_COUNT];

/* ── Public API ─────────────────────────────────────────────────────────── */
void Menu_Init  (UI_t *ui, Lcd_HandleTypeDef *lcd);
void Menu_Update(UI_t *ui, Lcd_HandleTypeDef *lcd,
                 uint16_t joy_x, uint16_t joy_y, uint8_t joy_btn_pressed);

/* Screen renderers — must only be called from Menu_Update / Menu_Init */
void Screen_MainMenu    (UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_AddPlantName(UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_AddPlantType(UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_AddPlantFreq(UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_PlantsList  (UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_PlantStats  (UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_PlantFreqEdit(UI_t *ui, Lcd_HandleTypeDef *lcd);
void Screen_WaterLevel  (UI_t *ui, Lcd_HandleTypeDef *lcd, uint16_t water_adc_raw);

#endif /* MENU_H */
