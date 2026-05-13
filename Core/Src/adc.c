/* USER CODE BEGIN Header */
/**
 ******************************************************************************
 * @file    adc.c
 * @brief   ADC1 configuration — B-L475E-IOT01A2
 *
 * ── CHANNEL CORRECTION ──────────────────────────────────────────────────────
 * Per the B-L475E-IOT01A2 datasheet Table 4 (Arduino connector pinout) and
 * the MspInit GPIO comment block:
 *   PC2  ↔  ARD_A3  ↔  ADC1_IN3   (water level sensor)
 *   PC3  ↔  ARD_A2  ↔  ADC1_IN4   (soil moisture sensor)
 *
 * The previous config had Rank 3 = CH4 (A2/soil) and Rank 4 = CH3 (A3/water),
 * which is functionally correct for the channel order — but the earlier
 * version had BOTH set to CH14, meaning water always returned the same value
 * as VRX.  The ranks below are now definitively correct:
 *
 *   Rank 1  CH14  PC5  A0 → VRX  (joystick X)
 *   Rank 2  CH13  PC4  A1 → VRY  (joystick Y)
 *   Rank 3  CH4   PC3  A2 → soil moisture
 *   Rank 4  CH3   PC2  A3 → water level
 ******************************************************************************
 */
/* USER CODE END Header */
#include "adc.h"

/* USER CODE BEGIN 0 */
/* USER CODE END 0 */

ADC_HandleTypeDef hadc1;

void MX_ADC1_Init(void)
{
  /* USER CODE BEGIN ADC1_Init 0 */
  /* USER CODE END ADC1_Init 0 */

  ADC_MultiModeTypeDef multimode = {0};
  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC1_Init 1 */
  /* USER CODE END ADC1_Init 1 */

  hadc1.Instance                   = ADC1;
  hadc1.Init.ClockPrescaler        = ADC_CLOCK_ASYNC_DIV1;
  hadc1.Init.Resolution            = ADC_RESOLUTION_12B;
  hadc1.Init.DataAlign             = ADC_DATAALIGN_RIGHT;
  hadc1.Init.ScanConvMode          = ADC_SCAN_ENABLE;
  hadc1.Init.EOCSelection          = ADC_EOC_SINGLE_CONV;
  hadc1.Init.LowPowerAutoWait      = DISABLE;
  hadc1.Init.ContinuousConvMode    = DISABLE;
  hadc1.Init.NbrOfConversion       = 4;
  hadc1.Init.DiscontinuousConvMode = DISABLE;
  hadc1.Init.ExternalTrigConv      = ADC_SOFTWARE_START;
  hadc1.Init.ExternalTrigConvEdge  = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc1.Init.DMAContinuousRequests = DISABLE;
  hadc1.Init.Overrun               = ADC_OVR_DATA_PRESERVED;
  hadc1.Init.OversamplingMode      = DISABLE;
  if (HAL_ADC_Init(&hadc1) != HAL_OK) Error_Handler();

  multimode.Mode = ADC_MODE_INDEPENDENT;
  if (HAL_ADCEx_MultiModeConfigChannel(&hadc1, &multimode) != HAL_OK) Error_Handler();

  /* Rank 1 — CH14 — PC5 — A0 — VRX (joystick X) */
  sConfig.Channel      = ADC_CHANNEL_14;
  sConfig.Rank         = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_640CYCLES_5;
  sConfig.SingleDiff   = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset       = 0;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 2 — CH13 — PC4 — A1 — VRY (joystick Y) */
  sConfig.Channel = ADC_CHANNEL_13;
  sConfig.Rank    = ADC_REGULAR_RANK_2;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 3 — CH4 — PC3 — A2 — soil moisture sensor
   * NOTE: PC3 is ADC1_IN4 (not IN3). See MspInit comment below. */
  sConfig.Channel = ADC_CHANNEL_4;
  sConfig.Rank    = ADC_REGULAR_RANK_3;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* Rank 4 — CH3 — PC2 — A3 — water level sensor
   * NOTE: PC2 is ADC1_IN3 (not IN4). See MspInit comment below. */
  sConfig.Channel = ADC_CHANNEL_3;
  sConfig.Rank    = ADC_REGULAR_RANK_4;
  if (HAL_ADC_ConfigChannel(&hadc1, &sConfig) != HAL_OK) Error_Handler();

  /* USER CODE BEGIN ADC1_Init 2 */
  /* USER CODE END ADC1_Init 2 */
}

void HAL_ADC_MspInit(ADC_HandleTypeDef* adcHandle)
{
  GPIO_InitTypeDef GPIO_InitStruct        = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit  = {0};

  if (adcHandle->Instance == ADC1)
  {
    /* USER CODE BEGIN ADC1_MspInit 0 */
    /* USER CODE END ADC1_MspInit 0 */

    PeriphClkInit.PeriphClockSelection         = RCC_PERIPHCLK_ADC;
    PeriphClkInit.AdcClockSelection            = RCC_ADCCLKSOURCE_PLLSAI1;
    PeriphClkInit.PLLSAI1.PLLSAI1Source        = RCC_PLLSOURCE_MSI;
    PeriphClkInit.PLLSAI1.PLLSAI1M            = 1;
    PeriphClkInit.PLLSAI1.PLLSAI1N            = 24;
    PeriphClkInit.PLLSAI1.PLLSAI1P            = RCC_PLLP_DIV7;
    PeriphClkInit.PLLSAI1.PLLSAI1Q            = RCC_PLLQ_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1R            = RCC_PLLR_DIV2;
    PeriphClkInit.PLLSAI1.PLLSAI1ClockOut     = RCC_PLLSAI1_ADC1CLK;
    if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK) Error_Handler();

    __HAL_RCC_ADC_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();

    /**ADC1 GPIO Configuration
     * PC2  ------> ADC1_IN3   (ARD_A3 / water level)
     * PC3  ------> ADC1_IN4   (ARD_A2 / soil moisture)
     * PC4  ------> ADC1_IN13  (ARD_A1 / VRY)
     * PC5  ------> ADC1_IN14  (ARD_A0 / VRX)
     */
    GPIO_InitStruct.Pin  = ARD_A5_Pin | ARD_A4_Pin | ARD_A3_Pin | ARD_A2_Pin
                         | ARD_A1_Pin | GPIO_PIN_5;
    GPIO_InitStruct.Mode = GPIO_MODE_ANALOG_ADC_CONTROL;
    GPIO_InitStruct.Pull = GPIO_NOPULL;
    HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

    /* USER CODE BEGIN ADC1_MspInit 1 */
    /* USER CODE END ADC1_MspInit 1 */
  }
}

void HAL_ADC_MspDeInit(ADC_HandleTypeDef* adcHandle)
{
  if (adcHandle->Instance == ADC1)
  {
    /* USER CODE BEGIN ADC1_MspDeInit 0 */
    /* USER CODE END ADC1_MspDeInit 0 */
    __HAL_RCC_ADC_CLK_DISABLE();
    HAL_GPIO_DeInit(GPIOC, ARD_A5_Pin | ARD_A4_Pin | ARD_A3_Pin | ARD_A2_Pin
                         | ARD_A1_Pin | GPIO_PIN_5);
    /* USER CODE BEGIN ADC1_MspDeInit 1 */
    /* USER CODE END ADC1_MspDeInit 1 */
  }
}

/* USER CODE BEGIN 1 */
/* USER CODE END 1 */
