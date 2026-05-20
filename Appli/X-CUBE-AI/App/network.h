/**
  ******************************************************************************
  * @file    network.h
  * @brief   Compatibility aliases for X-CUBE-AI ATON wrappers.
  ******************************************************************************
  */
#ifndef LL_ATON_NETWORK_H
#define LL_ATON_NETWORK_H

#include "alpr.h"

#define LL_ATON_DEFAULT_C_MODEL_NAME      LL_ATON_ALPR_C_MODEL_NAME
#define LL_ATON_DEFAULT_ORIGIN_MODEL_NAME LL_ATON_ALPR_ORIGIN_MODEL_NAME

#define LL_ATON_DEFAULT_IN_NUM            LL_ATON_ALPR_IN_NUM
#define LL_ATON_DEFAULT_IN_1_ALIGNMENT    LL_ATON_ALPR_IN_1_ALIGNMENT
#define LL_ATON_DEFAULT_IN_1_SIZE_BYTES   LL_ATON_ALPR_IN_1_SIZE_BYTES

#define LL_ATON_DEFAULT_OUT_NUM           LL_ATON_ALPR_OUT_NUM
#define LL_ATON_DEFAULT_OUT_1_ALIGNMENT   LL_ATON_ALPR_OUT_1_ALIGNMENT
#define LL_ATON_DEFAULT_OUT_1_SIZE_BYTES  LL_ATON_ALPR_OUT_1_SIZE_BYTES

#define LL_ATON_Set_User_Input_Buffer_Default  LL_ATON_Set_User_Input_Buffer_alpr
#define LL_ATON_Get_User_Input_Buffer_Default  LL_ATON_Get_User_Input_Buffer_alpr
#define LL_ATON_Set_User_Output_Buffer_Default LL_ATON_Set_User_Output_Buffer_alpr
#define LL_ATON_Get_User_Output_Buffer_Default LL_ATON_Get_User_Output_Buffer_alpr

#endif /* LL_ATON_NETWORK_H */
