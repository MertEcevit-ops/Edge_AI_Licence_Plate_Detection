/**
  ******************************************************************************
  * @file    alpr2.h
  * @author  STEdgeAI
  * @date    2026-05-21 17:16:00
  * @brief   Minimal description of the generated c-implemention of the network
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  ******************************************************************************
  */
#ifndef LL_ATON_ALPR2_H
#define LL_ATON_ALPR2_H

/******************************************************************************/
#define LL_ATON_ALPR2_C_MODEL_NAME        "alpr2"
#define LL_ATON_ALPR2_ORIGIN_MODEL_NAME   "best_int8"

/************************** USER ALLOCATED IOs ********************************/
// No user allocated inputs
// No user allocated outputs

/************************** INPUTS ********************************************/
#define LL_ATON_ALPR2_IN_NUM        (1)    // Total number of input buffers
// Input buffer 1 -- Input_3_out_0
#define LL_ATON_ALPR2_IN_1_ALIGNMENT   (32)
#define LL_ATON_ALPR2_IN_1_SIZE_BYTES  (1228800)

/************************** OUTPUTS *******************************************/
#define LL_ATON_ALPR2_OUT_NUM        (1)    // Total number of output buffers
// Output buffer 1 -- Transpose_268_out_0
#define LL_ATON_ALPR2_OUT_1_ALIGNMENT   (32)
#define LL_ATON_ALPR2_OUT_1_SIZE_BYTES  (42000)

#endif /* LL_ATON_ALPR2_H */
