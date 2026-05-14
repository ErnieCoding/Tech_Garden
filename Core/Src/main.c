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
 * ── Sensor thresholds ──────────────────────────────────────────────────
 *   Resistive soil sensors: HIGH ADC = DRY, LOW ADC = WET.
 *   Resistive water sensors: HIGH ADC = MORE WATER.
 */
#define SOIL_DRY_THRESHOLD    180u
#define PUMP_ON_DURATION_MS   3000u
#define WATER_FULL_THRESH 850u
#define WATER_LOW_THRESH  250u

#define DEMO_FORCE_WATERING 1

/* USER CODE END PV */

void SystemClock_Config(void);

/* USER CODE BEGIN 0 */

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

/* ── ADC polling helper ──*/
static void read_all_adc(void)
{
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

    HAL_ADCEx_Calibration_Start(&hadc1, ADC_SINGLE_ENDED);

    hlcd = Lcd_create(lcd_data_ports, lcd_data_pins,
                      LCD_RS_GPIO_Port, LCD_RS_Pin,
                      LCD_EN_GPIO_Port, LCD_EN_Pin,
                      LCD_4_BIT_MODE);

    read_all_adc();

    Menu_Init(&ui, &hlcd);
    last_keepalive_tick = HAL_GetTick();
/* USER CODE END 2 */

    while (1)
    {
/* USER CODE BEGIN WHILE */

    	read_all_adc();

        if (soil_filtered == 0u) {
            soil_filtered = (uint16_t)soil_raw;
        }
        else {
            soil_filtered = (uint16_t)((soil_filtered * 7u + (uint16_t)soil_raw) / 8u);
        }

        /* Sensor readings tests: Joystick axis, Water level, soil moisture */

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

        uint8_t btn = (HAL_GPIO_ReadPin(JOY_SW_GPIO_Port, JOY_SW_Pin)
                       == GPIO_PIN_RESET) ? 1u : 0u;

        ui.water_adc_raw = (uint16_t)water_raw;
        ui.soil_adc_raw  = soil_filtered;

        if (ui.plant_count > 0) {
            uint32_t now = HAL_GetTick();
            for (uint8_t i = 0; i < ui.plant_count; i++) {
                Plant_t *p = &ui.plants[i];

                if (!p->active)
                	continue;

                uint32_t interval_ms = p->watering_interval_sec * 1000u;
                if ((now - p->last_watered_tick) >= interval_ms) {
                    p->last_watered_tick = HAL_GetTick();
                    if ((DEMO_FORCE_WATERING) || (soil_filtered < SOIL_DRY_THRESHOLD)) {
                        rgb_set(0,0,1);
                        pump_on();
                        HAL_Delay(PUMP_ON_DURATION_MS);
                        pump_off();
                        rgb_off();
                    }
                }
            }
        }

        /* Water level indicator through RGB LED */
        if (water_raw >= WATER_FULL_THRESH)
        	rgb_set(0,1,0);
        else if (water_raw >= WATER_LOW_THRESH)
        	rgb_set(1,1,0);
        else
        	rgb_set(1,0,0);


        Menu_Update(&ui, &hlcd, (uint16_t)joy_x_raw, (uint16_t)joy_y_raw, btn);

        HAL_Delay(50);
/* USER CODE END WHILE */

/* USER CODE BEGIN 3 */
        /* Keepalive for powerbank: 200 ms red blink every 10 s */
        if ((HAL_GetTick() - last_keepalive_tick) >= 10000u){
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
