/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    gpio.c
  * @brief   This file provides code for the configuration
  *          of all used GPIO pins.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "gpio.h"

/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/*----------------------------------------------------------------------------*/
/* Configure GPIO                                                             */
/*----------------------------------------------------------------------------*/
/* USER CODE BEGIN 1 */

/* USER CODE END 1 */

/** Configure pins
*/
void MX_GPIO_Init(void)
{

  GPIO_InitTypeDef GPIO_InitStruct = {0};

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();

  /* SPI CS pins start deasserted (HIGH) */
  HAL_GPIO_WritePin(GPIOA, ADC_CS_Pin|DAC_CS_Pin, GPIO_PIN_SET);

  /* Control outputs start LOW, except M6_EN (5V output active at startup per spec 3.5) */
  HAL_GPIO_WritePin(GPIOB, DISPLAY_DO1_Pin|DISPLAY_DO2_Pin|DISPLAY_DO3_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(GPIOB, M6_EN_Pin, GPIO_PIN_SET);
  HAL_GPIO_WritePin(GPIOC, TEC_EN_Pin|POLARITY_Pin, GPIO_PIN_RESET);

  /* ADC_CS (PA4), DAC_CS (PA3) — push-pull outputs */
  GPIO_InitStruct.Pin = ADC_CS_Pin|DAC_CS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* TEC_PG (PA2) — open-drain power-good from TEC buck, needs pullup */
  GPIO_InitStruct.Pin = TEC_PG_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_PULLUP;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /* M6_EN (PB0), DISPLAY_DO1/2/3 (PB13/14/15) */
  GPIO_InitStruct.Pin = M6_EN_Pin|DISPLAY_DO1_Pin|DISPLAY_DO2_Pin|DISPLAY_DO3_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOB, &GPIO_InitStruct);

  /* TEC_EN (PC5), POLARITY (PC6) */
  GPIO_InitStruct.Pin = TEC_EN_Pin|POLARITY_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

}

/* USER CODE BEGIN 2 */

/* USER CODE END 2 */
