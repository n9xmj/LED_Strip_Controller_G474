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

// Exported HAL IP handles
extern ADC_HandleTypeDef hadc1;
extern CORDIC_HandleTypeDef hcordic;
extern DAC_HandleTypeDef hdac1;
extern DAC_HandleTypeDef hdac2;
extern DAC_HandleTypeDef hdac3;
extern DAC_HandleTypeDef hdac4;
extern FMAC_HandleTypeDef hfmac;
extern I2C_HandleTypeDef hi2c1;
extern I2S_HandleTypeDef hi2s2;
extern RTC_HandleTypeDef hrtc;
extern SAI_HandleTypeDef hsai_BlockA1;
extern SPI_HandleTypeDef hspi1;
extern TIM_HandleTypeDef htim6;
extern TIM_HandleTypeDef htim7;
extern TIM_HandleTypeDef htim16;
extern TIM_HandleTypeDef htim17;
extern UART_HandleTypeDef hlpuart1;
extern UART_HandleTypeDef huart1;
extern UART_HandleTypeDef huart2;
extern UART_HandleTypeDef huart3;
extern UART_HandleTypeDef huart4;
extern UART_HandleTypeDef huart5;
extern PCD_HandleTypeDef hpcd_USB_FS;



/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED2_UART_H hlpuart1
#define DEBUG_UART_H huart2
#define MIC_ADC_H hadc1
#define I2C1_H hi2c1
#define AMP_DAC_H hdac1
#define LCD_SPI_H hspi1
#define PERIODIC_TIMER_H htim16
#define LED4_UART_H huart4
#define DELAY_TIMER_H htim17
#define LED5_UART_H huart5
#define LED1_UART_H huart1
#define LED3_UART_H huart3
#define I2S_AMP_H hsai_BlockA1
#define I2S_MIC_H hi2s2
#define NUCLEO_BUTTON_Pin GPIO_PIN_13
#define NUCLEO_BUTTON_GPIO_Port GPIOC
#define RCC_OSC32_IN_Pin GPIO_PIN_14
#define RCC_OSC32_IN_GPIO_Port GPIOC
#define RCC_OSC32_OUT_Pin GPIO_PIN_15
#define RCC_OSC32_OUT_GPIO_Port GPIOC
#define RCC_OSC_IN_Pin GPIO_PIN_0
#define RCC_OSC_IN_GPIO_Port GPIOF
#define RCC_OSC_OUT_Pin GPIO_PIN_1
#define RCC_OSC_OUT_GPIO_Port GPIOF
#define LCD_CS_Pin GPIO_PIN_0
#define LCD_CS_GPIO_Port GPIOC
#define LED_STRIP_2_Pin GPIO_PIN_1
#define LED_STRIP_2_GPIO_Port GPIOC
#define FLASH_CS_Pin GPIO_PIN_3
#define FLASH_CS_GPIO_Port GPIOC
#define ANALOG_MIC_Pin GPIO_PIN_0
#define ANALOG_MIC_GPIO_Port GPIOA
#define DEBUG_TX_Pin GPIO_PIN_2
#define DEBUG_TX_GPIO_Port GPIOA
#define DEBUG_RX_Pin GPIO_PIN_3
#define DEBUG_RX_GPIO_Port GPIOA
#define ANALOG_AMP_Pin GPIO_PIN_4
#define ANALOG_AMP_GPIO_Port GPIOA
#define SPI_SCK_Pin GPIO_PIN_5
#define SPI_SCK_GPIO_Port GPIOA
#define NUCLEO_LED_Pin GPIO_PIN_6
#define NUCLEO_LED_GPIO_Port GPIOA
#define SPI_MOSI_Pin GPIO_PIN_7
#define SPI_MOSI_GPIO_Port GPIOA
#define LED_STRIP_1_Pin GPIO_PIN_4
#define LED_STRIP_1_GPIO_Port GPIOC
#define LED_STRIP_3_Pin GPIO_PIN_10
#define LED_STRIP_3_GPIO_Port GPIOB
#define I2S_MIC_WS_Pin GPIO_PIN_12
#define I2S_MIC_WS_GPIO_Port GPIOB
#define I2S_MIC_SCK_Pin GPIO_PIN_13
#define I2S_MIC_SCK_GPIO_Port GPIOB
#define I2S_MIC_SD_Pin GPIO_PIN_15
#define I2S_MIC_SD_GPIO_Port GPIOB
#define I2S_AMP_SCK_Pin GPIO_PIN_8
#define I2S_AMP_SCK_GPIO_Port GPIOA
#define I2S_AMP_FS_Pin GPIO_PIN_9
#define I2S_AMP_FS_GPIO_Port GPIOA
#define I2S_AMP_SD_Pin GPIO_PIN_10
#define I2S_AMP_SD_GPIO_Port GPIOA
#define SWDIO_Pin GPIO_PIN_13
#define SWDIO_GPIO_Port GPIOA
#define SWCLK_Pin GPIO_PIN_14
#define SWCLK_GPIO_Port GPIOA
#define I2C_SCL_Pin GPIO_PIN_15
#define I2C_SCL_GPIO_Port GPIOA
#define LED_STRIP_4_Pin GPIO_PIN_10
#define LED_STRIP_4_GPIO_Port GPIOC
#define LED_STRIP_5__UART5__Pin GPIO_PIN_12
#define LED_STRIP_5__UART5__GPIO_Port GPIOC
#define SWO_Pin GPIO_PIN_3
#define SWO_GPIO_Port GPIOB
#define SPI_MISO_Pin GPIO_PIN_4
#define SPI_MISO_GPIO_Port GPIOB
#define I2C_SDA_Pin GPIO_PIN_7
#define I2C_SDA_GPIO_Port GPIOB
#define I2S_AMP_MCLK_Pin GPIO_PIN_8
#define I2S_AMP_MCLK_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

extern SAI_HandleTypeDef hsai_BlockA1;
extern I2S_HandleTypeDef hi2s2;

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
