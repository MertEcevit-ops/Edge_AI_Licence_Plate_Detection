/**
  ******************************************************************************
  * @file    bsp_storage.c
  * @brief   Storage and memory peripheral driver wrapper.
  ******************************************************************************
  */

#include "bsp_storage.h"

static XSPI_HandleTypeDef hxspi1;
static CACHEAXI_HandleTypeDef hcacheaxi;
static SD_HandleTypeDef hsd1;
static uint8_t storage_opened = 0U;

static BSP_Storage_StatusTypeDef BSP_Storage_CACHEAXI_Open(void);
static BSP_Storage_StatusTypeDef BSP_Storage_SD_Open(void);
static BSP_Storage_StatusTypeDef BSP_Storage_XSPI_Open(void);

BSP_Storage_StatusTypeDef BSP_Storage_Open(void)
{
  if (storage_opened != 0U)
  {
    return BSP_STORAGE_OK;
  }

  if (BSP_Storage_SD_Open() != BSP_STORAGE_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  if (BSP_Storage_XSPI_Open() != BSP_STORAGE_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  if (BSP_Storage_CACHEAXI_Open() != BSP_STORAGE_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  storage_opened = 1U;
  return BSP_STORAGE_OK;
}

BSP_Storage_StatusTypeDef BSP_Storage_Close(void)
{
  BSP_Storage_StatusTypeDef status = BSP_STORAGE_OK;

  if (storage_opened == 0U)
  {
    return BSP_STORAGE_OK;
  }

  if (HAL_CACHEAXI_DeInit(&hcacheaxi) != HAL_OK)
  {
    status = BSP_STORAGE_ERROR;
  }

  if (HAL_XSPI_DeInit(&hxspi1) != HAL_OK)
  {
    status = BSP_STORAGE_ERROR;
  }

  if (HAL_SD_DeInit(&hsd1) != HAL_OK)
  {
    status = BSP_STORAGE_ERROR;
  }

  storage_opened = 0U;
  return status;
}

XSPI_HandleTypeDef *BSP_Storage_GetXSPIHandle(void)
{
  return &hxspi1;
}

SD_HandleTypeDef *BSP_Storage_GetSDHandle(void)
{
  return &hsd1;
}

CACHEAXI_HandleTypeDef *BSP_Storage_GetCacheAXIHandle(void)
{
  return &hcacheaxi;
}

static BSP_Storage_StatusTypeDef BSP_Storage_CACHEAXI_Open(void)
{
  hcacheaxi.Instance = CACHEAXI;

  if (HAL_CACHEAXI_Init(&hcacheaxi) != HAL_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  return BSP_STORAGE_OK;
}

static BSP_Storage_StatusTypeDef BSP_Storage_SD_Open(void)
{
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_DISABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv = 0;

  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  return BSP_STORAGE_OK;
}

static BSP_Storage_StatusTypeDef BSP_Storage_XSPI_Open(void)
{
  XSPIM_CfgTypeDef sXspiManagerCfg = {0};

  hxspi1.Instance = XSPI1;
  hxspi1.Init.FifoThresholdByte = 1;
  hxspi1.Init.MemoryMode = HAL_XSPI_SINGLE_MEM;
  hxspi1.Init.MemoryType = HAL_XSPI_MEMTYPE_MICRON;
  hxspi1.Init.MemorySize = HAL_XSPI_SIZE_16B;
  hxspi1.Init.ChipSelectHighTimeCycle = 1;
  hxspi1.Init.FreeRunningClock = HAL_XSPI_FREERUNCLK_DISABLE;
  hxspi1.Init.ClockMode = HAL_XSPI_CLOCK_MODE_0;
  hxspi1.Init.WrapSize = HAL_XSPI_WRAP_NOT_SUPPORTED;
  hxspi1.Init.ClockPrescaler = 0;
  hxspi1.Init.SampleShifting = HAL_XSPI_SAMPLE_SHIFT_NONE;
  hxspi1.Init.DelayHoldQuarterCycle = HAL_XSPI_DHQC_DISABLE;
  hxspi1.Init.ChipSelectBoundary = HAL_XSPI_BONDARYOF_NONE;
  hxspi1.Init.MaxTran = 0;
  hxspi1.Init.Refresh = 0;
  hxspi1.Init.MemorySelect = HAL_XSPI_CSSEL_NCS1;

  if (HAL_XSPI_Init(&hxspi1) != HAL_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  sXspiManagerCfg.nCSOverride = HAL_XSPI_CSSEL_OVR_NCS1;
  sXspiManagerCfg.IOPort = HAL_XSPIM_IOPORT_1;
  sXspiManagerCfg.Req2AckTime = 1;

  if (HAL_XSPIM_Config(&hxspi1, &sXspiManagerCfg,
                       HAL_XSPI_TIMEOUT_DEFAULT_VALUE) != HAL_OK)
  {
    return BSP_STORAGE_ERROR;
  }

  return BSP_STORAGE_OK;
}
