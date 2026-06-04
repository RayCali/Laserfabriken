/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g4xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define DRV2_IN1_Pin GPIO_PIN_0
#define DRV2_IN1_GPIO_Port GPIOC
#define DRV2_IN2_Pin GPIO_PIN_1
#define DRV2_IN2_GPIO_Port GPIOC
#define DRV2_nSLEEP_Pin GPIO_PIN_2
#define DRV2_nSLEEP_GPIO_Port GPIOC
#define DRV2_nFAULT_Pin GPIO_PIN_3
#define DRV2_nFAULT_GPIO_Port GPIOC
#define DRV1_nFAULT_Pin GPIO_PIN_0
#define DRV1_nFAULT_GPIO_Port GPIOA
#define ADC_CS_Pin GPIO_PIN_2
#define ADC_CS_GPIO_Port GPIOA
#define DAC_CS_Pin GPIO_PIN_3
#define DAC_CS_GPIO_Port GPIOA
#define DRV2_CS_Pin GPIO_PIN_4
#define DRV2_CS_GPIO_Port GPIOC
#define DRV1_CS_Pin GPIO_PIN_5
#define DRV1_CS_GPIO_Port GPIOC
#define DRV1_IN1_Pin GPIO_PIN_0
#define DRV1_IN1_GPIO_Port GPIOB
#define DRV1_IN2_Pin GPIO_PIN_1
#define DRV1_IN2_GPIO_Port GPIOB
#define DRV1_nSLEEP_Pin GPIO_PIN_2
#define DRV1_nSLEEP_GPIO_Port GPIOB
#define EN_5V_Pin GPIO_PIN_6
#define EN_5V_GPIO_Port GPIOC
#define DISPLAY_TX_Pin GPIO_PIN_10
#define DISPLAY_TX_GPIO_Port GPIOC
#define DISPLAY_RX_Pin GPIO_PIN_11
#define DISPLAY_RX_GPIO_Port GPIOC
#define ADC_DRDY_Pin GPIO_PIN_4
#define ADC_DRDY_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
