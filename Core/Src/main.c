/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file           : main.c
 * @brief          : Main program body — B-L475E-IOT01A2
 ******************************************************************************
 */
/* USER CODE END Header */

#include "main.h"
#include "adc.h"
#include "tim.h"
#include "gpio.h"

/* USER CODE BEGIN Includes */
#include "lcd.h"
#include "menu.h"
/* USER CODE END Includes */

/* USER CODE BEGIN PV */
Lcd_PortType lcd_data_ports[] = {
    LCD_D4_GPIO_Port, LCD_D5_GPIO_Port,
    LCD_D6_GPIO_Port, LCD_D7_GPIO_Port
};
Lcd_PinType lcd_data_pins[] = {
    LCD_D4_Pin, LCD_D5_Pin,
    LCD_D6_Pin, LCD_D7_Pin
};

Lcd_HandleTypeDef hlcd;
UI_t ui;

uint32_t joy_x_raw  = 2048;
uint32_t joy_y_raw  = 2048;
uint32_t soil_raw   = 0;
uint32_t water_raw  = 0;

uint16_t soil_filtered = 0;

uint32_t last_keepalive_tick = 0;

/*
 * ── ADC channel → Arduino pin mapping for B-L475E-IOT01A2 ──────────────
 *   Rank 1  Ch14  PC5  A0  → VRX  (joystick X)
 *   Rank 2  Ch13  PC4  A1  → VRY  (joystick Y)
 *   Rank 3  Ch4   PC3  A2  → Soil moisture sensor
 *   Rank 4  Ch3   PC2  A3  → Water level sensor
 *
 * ── Sensor thresholds ──────────────────────────────────────────────────
 *   Resistive soil sensors: HIGH ADC = DRY, LOW ADC = WET.
 *   Resistive water sensors: HIGH ADC = MORE WATER.
 *   Adjust both thresholds after reading the raw ADC on the water screen.
 */
#define SOIL_DRY_THRESHOLD    180u   /* soil_raw above this → dry        */
#define PUMP_ON_DURATION_MS   3000u   /* ms per pump activation           */
#define WATER_FULL_THRESH 850u
#define WATER_LOW_THRESH  250u

#define DEMO_FORCE_WATERING 1

/* USER CODE END PV */

void SystemClock_Config(void);

/* USER CODE BEGIN 0 */

/* ── RGB LED (common-cathode, SET = ON) ────────────────────────────────
 * main.h:  LED_R = PA1,  LED_G = PA0,  LED_B = PB0               */
static inline void rgb_set(uint8_t r, uint8_t g, uint8_t b)
{
    HAL_GPIO_WritePin(LED_R_GPIO_Port, LED_R_Pin, r ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_G_GPIO_Port, LED_G_Pin, g ? GPIO_PIN_SET : GPIO_PIN_RESET);
    HAL_GPIO_WritePin(LED_B_GPIO_Port, LED_B_Pin, b ? GPIO_PIN_SET : GPIO_PIN_RESET);
}
static inline void rgb_off(void) { rgb_set(0,0,0); }

static inline void pump_on (void)
{ HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_SET);   }
static inline void pump_off(void)
{ HAL_GPIO_WritePin(RELAY_GPIO_Port, RELAY_Pin, GPIO_PIN_RESET); }

/* ── ADC polling helper ────────────────────────────────────────────────
 *
 * ROOT CAUSE of water/soil always reading 0:
 *   With ADC_OVR_DATA_PRESERVED + EOC_SINGLE_CONV, an overrun flag (OVR)
 *   left over from a previous sequence will cause PollForConversion to
 *   time out silently on every subsequent call.  The if() guard then
 *   fails and the variable is never updated (stays 0).
 *
 * Fix:
 *   1. HAL_ADCEx_Calibration_Start() once at boot (removes gain error).
 *   2. __HAL_ADC_CLEAR_FLAG(OVR) before every Start.
 *   3. After Stop, clear OVR again so the next cycle is clean.
 */
static void read_all_adc(void)
{
    /* Clear any stale overrun flag before starting */
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);

    HAL_ADC_Start(&hadc1);

    /* Rank 1 → joy_x_raw (VRX, Ch14, A0) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        joy_x_raw = HAL_ADC_GetValue(&hadc1);

    /* Rank 2 → joy_y_raw (VRY, Ch13, A1) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        joy_y_raw = HAL_ADC_GetValue(&hadc1);

    /* Rank 3 → soil_raw  (Soil, Ch4, A2) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        soil_raw  = HAL_ADC_GetValue(&hadc1);

    /* Rank 4 → water_raw (Water, Ch3, A3) */
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
        water_raw = HAL_ADC_GetValue(&hadc1);

    HAL_ADC_Stop(&hadc1);

    /* Clear OVR after Stop so it doesn't bleed into the next cycle */
    __HAL_ADC_CLEAR_FLAG(&hadc1, ADC_FLAG_OVR);
}

/* USER CODE END 0 */

int main(void)
{
    HAL_Init();
    SystemClock_Config();
    MX_GPIO_Init();
    MX_ADC1_Init();
    MX_TIM3_Init();

/* USER CODE BEGIN 2 */
    pump_off();
    rgb_off();
    HAL_Delay(100);

    /* Self-calibration — must run before first HAL_ADC_Start */
    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    hlcd = Lcd_create(lcd_data_ports, lcd_data_pins,
                      LCD_RS_GPIO_Port, LCD_RS_Pin,
                      LCD_EN_GPIO_Port, LCD_EN_Pin,
                      LCD_4_BIT_MODE);

    /* Drain first conversion so rank registers are stable */
    read_all_adc();

    Menu_Init(&ui, &hlcd);
    last_keepalive_tick = HAL_GetTick();
/* USER CODE END 2 */

    while (1)
    {
/* USER CODE BEGIN WHILE */
        /* 1. Read all four ADC channels */
        read_all_adc();

        if (soil_filtered == 0u)
        {
            soil_filtered = (uint16_t)soil_raw;
        }
        else
        {
            soil_filtered = (uint16_t)((soil_filtered * 7u + (uint16_t)soil_raw) / 8u);
        }

//        char l0[17];
//		char l1[17];
//
//		snprintf(l0, sizeof(l0), "X%4lu Y%4lu", joy_x_raw, joy_y_raw);
//		snprintf(l1, sizeof(l1), "S%4lu W%4lu", soil_raw, water_raw);
//
//		Lcd_clear(&hlcd);
//		Lcd_cursor(&hlcd, 0, 0);
//		Lcd_string(&hlcd, l0);
//		Lcd_cursor(&hlcd, 1, 0);
//		Lcd_string(&hlcd, l1);
//
//		HAL_Delay(250);
//		continue;

        /* 2. Joystick button — active LOW, internal pull-up */
        uint8_t btn = (HAL_GPIO_ReadPin(JOY_SW_GPIO_Port, JOY_SW_Pin)
                       == GPIO_PIN_RESET) ? 1u : 0u;

        /* 3. Hand sensor values to UI */
        ui.water_adc_raw = (uint16_t)water_raw;
        ui.soil_adc_raw  = soil_filtered;

        /* 4. Per-plant watering logic
         *    Frequency timer gates the check; soil sensor gates the pump.
         *    If soil is already moist at schedule time, we skip watering
         *    but still reset the timer so the cadence stays regular. */
        if (ui.plant_count > 0)
        {
            uint32_t now = HAL_GetTick();
            for (uint8_t i = 0; i < ui.plant_count; i++)
            {
                Plant_t *p = &ui.plants[i];
                if (!p->active) continue;

                uint32_t interval_ms = p->watering_interval_sec * 1000u;
                if ((now - p->last_watered_tick) >= interval_ms)
                {
                    p->last_watered_tick = HAL_GetTick();   /* reset regardless */
                    if ((DEMO_FORCE_WATERING) || (soil_filtered < SOIL_DRY_THRESHOLD))
                    {
                        rgb_set(0,0,1);                     /* blue = watering  */
                        pump_on();
                        HAL_Delay(PUMP_ON_DURATION_MS);
                        pump_off();
                        rgb_off();
                    }
                }
            }
        }

        /* 5. RGB water-level status
         *    If your sensor reads HIGH when empty and LOW when full,
         *    invert the conditions below. */
        if      (water_raw >= WATER_FULL_THRESH) rgb_set(0,1,0); /* green  */
        else if (water_raw >= WATER_LOW_THRESH)  rgb_set(1,1,0); /* orange */
        else                                     rgb_set(1,0,0); /* red    */

        /* 6. UI state machine */
        Menu_Update(&ui, &hlcd,
                    (uint16_t)joy_x_raw,
                    (uint16_t)joy_y_raw,
                    btn);

        HAL_Delay(50);
/* USER CODE END WHILE */

/* USER CODE BEGIN 3 */
        /* Keepalive: 200 ms red blink every 10 s */
        if ((HAL_GetTick() - last_keepalive_tick) >= 10000u)
        {
            rgb_off();
            rgb_set(1,0,0);
            HAL_Delay(200);
            rgb_off();
            last_keepalive_tick = HAL_GetTick();
        }
    }
/* USER CODE END 3 */
}

void SystemClock_Config(void)
{
    RCC_OscInitTypeDef RCC_OscInitStruct = {0};
    RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
    if (HAL_PWREx_ControlVoltageScaling(PWR_REGULATOR_VOLTAGE_SCALE1) != HAL_OK) Error_Handler();
    HAL_PWR_EnableBkUpAccess();
    __HAL_RCC_LSEDRIVE_CONFIG(RCC_LSEDRIVE_LOW);
    RCC_OscInitStruct.OscillatorType      = RCC_OSCILLATORTYPE_LSE | RCC_OSCILLATORTYPE_MSI;
    RCC_OscInitStruct.LSEState            = RCC_LSE_ON;
    RCC_OscInitStruct.MSIState            = RCC_MSI_ON;
    RCC_OscInitStruct.MSICalibrationValue = 0;
    RCC_OscInitStruct.MSIClockRange       = RCC_MSIRANGE_6;
    RCC_OscInitStruct.PLL.PLLState        = RCC_PLL_ON;
    RCC_OscInitStruct.PLL.PLLSource       = RCC_PLLSOURCE_MSI;
    RCC_OscInitStruct.PLL.PLLM            = 1;
    RCC_OscInitStruct.PLL.PLLN            = 40;
    RCC_OscInitStruct.PLL.PLLP            = RCC_PLLP_DIV7;
    RCC_OscInitStruct.PLL.PLLQ            = RCC_PLLQ_DIV2;
    RCC_OscInitStruct.PLL.PLLR            = RCC_PLLR_DIV2;
    if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK) Error_Handler();
    RCC_ClkInitStruct.ClockType      = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                                     | RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
    RCC_ClkInitStruct.SYSCLKSource   = RCC_SYSCLKSOURCE_PLLCLK;
    RCC_ClkInitStruct.AHBCLKDivider  = RCC_SYSCLK_DIV1;
    RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV1;
    RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;
    if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_4) != HAL_OK) Error_Handler();
    HAL_RCCEx_EnableMSIPLLMode();
}

void Error_Handler(void)
{
    __disable_irq();
    while (1) {}
}
#ifdef USE_FULL_ASSERT
void assert_failed(uint8_t *file, uint32_t line) {}
#endif
