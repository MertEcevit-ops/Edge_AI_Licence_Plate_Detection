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

typedef enum
{
  BSP_BOARD_OK    = 0,
  BSP_BOARD_ERROR = 1,
} BSP_Board_StatusTypeDef;

BSP_Board_StatusTypeDef BSP_Board_Open(void);
void BSP_Board_GPIO_Open(void);

#ifdef __cplusplus
}
#endif

#endif /* BSP_BOARD_H */
