/**
  ******************************************************************************
  * @file    bsp_board.h
  * @brief   Board-level abstraction for STM32N6570-DK platform bring-up.
  ******************************************************************************
  */

#ifndef BSP_BOARD_H
#define BSP_BOARD_H

#ifdef __cplusplus
extern "C" {
#endif

#include "main.h"

#ifndef OSC_IN_Pin
#define OSC_IN_Pin GPIO_PIN_0
#define OSC_IN_GPIO_Port GPIOH
#endif

#ifndef OSC_OUT_Pin
#define OSC_OUT_Pin GPIO_PIN_1
#define OSC_OUT_GPIO_Port GPIOH
#endif

typedef enum
{
  BSP_BOARD_OK    = 0,
  BSP_BOARD_ERROR = 1,
} BSP_Board_StatusTypeDef;

BSP_Board_StatusTypeDef BSP_Board_Open(void);
BSP_Board_StatusTypeDef BSP_Board_Close(void);
void BSP_Board_GPIO_Open(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
