/**
******************************************************************************
* @file    ai_wrapper_ATON.c
* @author  GPM/AIS Application Team
* @brief   AI Atonn wrapper
******************************************************************************
* @attention
*
* Copyright (c) 2023,2024,2025 STMicroelectronics.
* All rights reserved.
*
* This software is licensed under terms that can be found in the LICENSE file
* in the root directory of this software component.
* If no LICENSE file comes with this software, it is provided AS-IS.
*
******************************************************************************
*/

#include "ai_wrapper_ATON.h"
#include "stm32n6xx_hal.h"

#include "ll_aton.h"
#include "ll_aton_runtime.h"
#include "ll_aton_cipher.h"
#include "ll_aton_dbgtrc.h"
#include "ll_aton_version.h"
#include "ll_aton_caches_interface.h"


#define ATONN_RT_VERSION (LL_ATON_VERSION_MAJOR << 24 | LL_ATON_VERSION_MINOR << 16 | LL_ATON_VERSION_MICRO << 8 | 0)
#define ATONN_RT_DESC    LL_ATON_VERSION_NAME
// Neural-Art encryption configuration (encryption ID + bus interface keys)
#define ENCRYPTION_ID                       0
#define BUSIF_LSB_KEY      0xAABBCCDDAABBCCDD
#define BUSIF_MSB_KEY      0xAABBCCDDAABBCCDD


#include "app_config.h"

#include "aiTestUtility.h"

#if !defined(LL_ATON_RT_RELOC) && defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
#warning "LL_ATON_RT_RELOC should be set to use the RELOC MODE"
#undef USE_RELOC_MODE
#endif

#ifdef CACHEAXI

#define USE_COUNTER_NPU_CACHE           1

int npu_cache_counters_enable(void)
{
  if (CACHEAXI->CR1 & CACHEAXI_CR1_EN) {
    CACHEAXI->CR1 |= 0x33330000UL;
    CACHEAXI->CR1 |= 0xCCCC0000UL;
    return 0;
  }
  return 1;
}

int npu_cache_counters_disable(void)
{
  if (CACHEAXI->CR1 & CACHEAXI_CR1_EN) {
    CACHEAXI->CR1 &= ~0xFFFF0000UL;
    return 0;
  }
  return 1;
}

int npu_cache_counters_get(uint32_t *counters)
{
  if ((CACHEAXI->CR1 & CACHEAXI_CR1_EN) && counters) {
    __IO uint32_t *base = &(CACHEAXI->RHMONR);
    for (int i=0; i<8; i++) {
      counters[i] = base[i];
    }
    return 0;
  }
  return 1;
}
#else
#define USE_COUNTER_NPU_CACHE           0

#endif

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1

#include "ll_aton_reloc_network.h"


#define _RELOC_EXT_RAM_ADDR    (0x90800000)      // 8MB // + 16M Bytes
#define _RELOC_MAX_EXT_RAM_SZ  (16 * 1024 * 1024)  // 16 MB // 8M Bytes

#define _RELOC_BASE_ADDR_0            (0x71000000)      // (0x70000000) - max 8MB
#define _RELOC_BASE_PARAMS_ADDR_0     (0x71800000)      // ...

#define _RELOC_BASE_ADDR_1            (0x90000000)
// #define _RELOC_BASE_PARAMS_ADDR_1     (0x90800000)



#define _RELOC_MAX_EXE_RAM_SZ  ((512 + 128) * 1024)  /* 512 + 128 KB */

// uint64_t exec_ram[_RELOC_MAX_EXE_RAM_SZ / 8];

// #define _RELOC_EXE_RAM_ADDR     (&exec_ram[0])
// #define _RELOC_EXE_RAM_ADDR  (0x24350000)  /* npuRAM6 */
// #define _RELOC_EXE_RAM_ADDR  (0x34100000)  /* AXIRAM2 */
#define _RELOC_EXE_RAM_ADDR  (0x34060000)     /* AXIRAM1 + 256+128 KB (512+128 KB is reserved for the exec RAM */
// #define _RELOC_EXE_RAM_ADDR     (0x90000000)


static NN_Instance_TypeDef NN_Instance_alpr2;

#else /* !USE_RELOC_MODE */

LL_ATON_DECLARE_NAMED_NN_INSTANCE_AND_INTERFACE(alpr2)

#endif


uint32_t get_ll_buffer_size(const LL_Buffer_InfoTypeDef *aton_buf)
{
  return LL_Buffer_len(aton_buf);
}

uint32_t get_ll_element_size(const LL_Buffer_InfoTypeDef *aton_buf)
{
  return (size_t)(aton_buf->nbits / 8);
}

/*
* Return a pointer of the instance for a given model
*/

static NN_Instance_TypeDef* _get_nn_instance(int idx)
{
  if ((idx < 0) || (idx >= NPU_NETWORK_NUMBER))
    return NULL;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1

  int res;
  uint32_t *rom_addr = (uint32_t *)_RELOC_BASE_ADDR_1;
  uintptr_t ext_param_addr = NULL;

  if (AI_RELOC_MAGIC != *rom_addr) {
    rom_addr = (uint32_t *)_RELOC_BASE_ADDR_0;
  }

  if (AI_RELOC_MAGIC != *rom_addr)
    return NULL;

  ll_aton_reloc_info rt;

  ll_aton_reloc_log_info((uintptr_t)rom_addr);

  // Retrieve the info from the binary objects. Requested RAM size,...
  res = ll_aton_reloc_get_info((uintptr_t)rom_addr, &rt);

  if (rt.params_off == 0)
    ext_param_addr = _RELOC_BASE_PARAMS_ADDR_0;

  if (res || rt.ext_ram_sz > _RELOC_MAX_EXT_RAM_SZ)
    return NULL;

  // Create and install an instance of the relocatable model
  ll_aton_reloc_config config;
  config.exec_ram_addr = (uintptr_t)_RELOC_EXE_RAM_ADDR;
  config.exec_ram_size = rt.rt_ram_copy;
  config.ext_ram_addr = (uintptr_t)_RELOC_EXT_RAM_ADDR;
  config.ext_ram_size = rt.ext_ram_sz;
  config.ext_param_addr = ext_param_addr;
  config.mode = AI_RELOC_RT_LOAD_MODE_COPY; // AI_RELOC_RT_LOAD_MODE_CLEAR, AI_RELOC_RT_LOAD_MODE_COPY or AI_RELOC_RT_LOAD_MODE_XIP;

  res = ll_aton_reloc_install((uintptr_t)rom_addr, &config, &NN_Instance_alpr2);

  if (res)
    return NULL;

  return &NN_Instance_alpr2;

#else
  return &NN_Instance_alpr2;
#endif
}

#if !defined(USE_RELOC_MODE) || (defined(USE_RELOC_MODE) && USE_RELOC_MODE != 1)

/*
* Return the size of the parameters/weights in bytes
*/
static uint32_t _get_nn_params_size(const struct npu_instance *instance, bool *has_no_desc)
{
  const LL_Buffer_InfoTypeDef *aton_buf;

  uint32_t total = 0;

  if ((!instance) || (!has_no_desc))
    return 0;

  *has_no_desc = true;

  const LL_Buffer_InfoTypeDef* input_buffs = npu_get_input_buffers_info(instance, -1);

  if (!input_buffs) {
    return total;
  }

  for (aton_buf = input_buffs; aton_buf->name != NULL; aton_buf++) {
    if (aton_buf->is_param == 1) {
      *has_no_desc = false;
      total += get_ll_buffer_size(aton_buf);
    }
  }
  return total;
}

/*
* Return the size of the used memory region
*/
static uint32_t _get_used_size_from_region(uintptr_t addr_min, uintptr_t addr_max,
                                           const LL_Buffer_InfoTypeDef *buffs)
{
  const LL_Buffer_InfoTypeDef *aton_buf;
  uintptr_t min = addr_max;
  uintptr_t max = addr_min;
  uint32_t total = 0;

  for (aton_buf = buffs; aton_buf->name != NULL; aton_buf++) {

    const uintptr_t start_addr = (uintptr_t)LL_Buffer_addr_start(aton_buf);
    const uintptr_t end_addr = (uintptr_t)LL_Buffer_addr_end(aton_buf) - 1;

    if ((start_addr <= addr_min) && (end_addr >= addr_max)) {
      return (uint32_t)(addr_max - addr_min + 1);
    }
    if ((start_addr >= addr_min) && (end_addr <= addr_max)) {
      min = start_addr < min ? start_addr : min;
      max = end_addr > max ? end_addr : max;
      total = max - min + 1;
    }
    else if ((start_addr >= addr_min) && (start_addr <= addr_max)) {
      min = start_addr < min ? start_addr : min;
      max = addr_max;
      total = max - min + 1;
    }
    else if ((end_addr >= addr_min) && (end_addr <= addr_max)) {
      min = addr_min;
      max = end_addr > max ? end_addr : max;
      total = max - min + 1;
    }
  }

  return total;
}

/*
* Return the size of the activations in bytes
*/
static uint32_t _get_nn_activations_size(const struct npu_instance *instance, bool *has_no_desc)
{
  uint32_t total = 0;

  if ((!instance) || (!has_no_desc))
    return 0;

  *has_no_desc = false;

  const LL_Buffer_InfoTypeDef* internal_buffs = npu_get_internal_buffers_info(instance);

  if ((!internal_buffs)) {
    *has_no_desc = true;
    return 0;
  }

  /* AXIRAM1-2 */
  for (uintptr_t addr = 0x34000000UL; addr < 0x34200000UL; addr += (1024 * 1024)) {
    total += _get_used_size_from_region(addr, addr + (1024 * 1024) - 1,
                                        internal_buffs);
    uintptr_t ns_addr = addr - 0x10000000UL;
    total += _get_used_size_from_region(ns_addr, ns_addr + (1024 * 1024) - 1,
                                        internal_buffs);

  }

  /* AXIRAM3-4-5-6 */
  for (uintptr_t addr = 0x34200000UL; addr < 0x343C0000UL; addr += (448 * 1024)) {
    total += _get_used_size_from_region(addr, addr + (448 * 1024) - 1,
                                        internal_buffs);
    uintptr_t ns_addr = addr - 0x10000000UL;
    total += _get_used_size_from_region(ns_addr, ns_addr + (448 * 1024) - 1,
                                        internal_buffs);
  }

  /* NPU Cache */
  total += _get_used_size_from_region((uintptr_t)0x343C0000UL, (uintptr_t)0x34400000UL - 1,
                                        internal_buffs);

  total += _get_used_size_from_region((uintptr_t)0x243C0000UL, (uintptr_t)0x24400000UL - 1,
                                        internal_buffs);

  /* External */
  total += _get_used_size_from_region((uintptr_t)0x60000000UL, (uintptr_t)0xA0000000UL,
                                        internal_buffs);
  return total;
}

#endif


static uint32_t _get_nn_epochs_num(const NN_Instance_TypeDef* nn_inst)
{

  if (!nn_inst)
    return 0;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
  const EpochBlock_ItemTypeDef* epochs_list = ai_rel_network_get_epoch_items(nn_inst->exec_state.inst_reloc);
#else
  const EpochBlock_ItemTypeDef* epochs_list = nn_inst->network->epoch_block_items();
#endif

  int16_t epoch_num = 0;
  const EpochBlock_ItemTypeDef* epochs = epochs_list;
  while (epochs->flags != EpochBlock_Flags_last_eb)
  {
    epoch_num++;
    epochs++;
  }

  return (uint32_t)epoch_num;
}

static void _populate_nn_info(struct npu_instance *instance)
{
  const LL_Buffer_InfoTypeDef *aton_buf;

  if ((!instance) || (!instance->impl))
    return;

  const NN_Instance_TypeDef* nn_inst = instance->impl;
  struct npu_model_info *nn_info = &instance->info;

  nn_info->flags |= NPU_MODEL_INFO_FLAGS_NO_MACC;
  nn_info->compile_datetime =  __DATE__ " " __TIME__;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
  nn_info->flags |= NPU_MODEL_INFO_FLAGS_RELOC;

  if (ll_aton_reloc_is_valid(nn_inst) <= 0)
    return;

  ll_aton_reloc_info m_info;
  uintptr_t file_ptr;

  ll_aton_reloc_get_file_ptr(nn_inst, &file_ptr);
  ll_aton_reloc_get_info(file_ptr, &m_info);

  nn_info->version = m_info.rt_version;
  nn_info->rt_desc = m_info.rt_version_desc;
  nn_info->params = m_info.params_sz;
  nn_info->name = m_info.c_name;
  nn_info->activations = m_info.acts_sz;

#else  /* !USE_RELOC_MODE */

  nn_info->version = ATONN_RT_VERSION;
  nn_info->rt_desc = ATONN_RT_DESC;

  nn_info->name = "network";

  bool has_no_desc;

  nn_info->params = _get_nn_params_size(instance, &has_no_desc);
  if (has_no_desc)
    nn_info->flags |= NPU_MODEL_INFO_FLAGS_NO_PARAMS;

  nn_info->activations = _get_nn_activations_size(instance, &has_no_desc);
  if (has_no_desc)
    nn_info->flags |= NPU_MODEL_INFO_FLAGS_NO_ACTS;

#endif

  nn_info->n_inputs = 0;
  while (aton_buf = npu_get_input_buffers_info(instance, nn_info->n_inputs))
  {
    nn_info->in_bufs[nn_info->n_inputs] = aton_buf;
    nn_info->n_inputs += 1;
  }

  nn_info->n_outputs = 0;
  while (aton_buf = npu_get_output_buffers_info(instance, nn_info->n_outputs))
  {
    nn_info->out_bufs[nn_info->n_outputs] = aton_buf;
    nn_info->n_outputs += 1;
  }

  nn_info->n_epochs = _get_nn_epochs_num(nn_inst);

  /* no IO configuration */

  if (nn_info->n_inputs) {
    uint8_t *input_addr = LL_Buffer_addr_start(nn_info->in_bufs[0]);
    if (!input_addr)
      nn_info->flags |= NPU_MODEL_INFO_FLAGS_NO_IN_ALLOC;
  }

  if (nn_info->n_inputs) {
    uint8_t *input_addr = LL_Buffer_addr_start(nn_info->in_bufs[0]);
    if (!input_addr)
      nn_info->flags |= NPU_MODEL_INFO_FLAGS_NO_OUT_ALLOC;
  }

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1

#if 0  // TEST purpose

#define YOLOV5

#ifdef YOLOV5
#define S_IN_0      150528
#define S_OUT_0     18522
#endif

#ifdef MOBV2
#define S_IN_0      150528
#define S_OUT_0     20
#endif


  _Pragma("data_alignment=32") static uint64_t buff_in[S_IN_0 / 8 + 1];
  _Pragma("data_alignment=32") static uint64_t buff_out[S_OUT_0 / 8 + 1];

  if (nn_info->flags & NPU_MODEL_INFO_FLAGS_NO_IN_ALLOC) {
      LL_ATON_User_IO_Result_t res = ll_aton_reloc_set_input(nn_inst, 0, &buff_in[0], S_IN_0);
      void *addr = ll_aton_reloc_get_input(nn_inst, 0);
      if (addr != (void*)&buff_in[0])
        return;
  }

  if (nn_info->flags & NPU_MODEL_INFO_FLAGS_NO_OUT_ALLOC) {
      LL_ATON_User_IO_Result_t res = ll_aton_reloc_set_output(nn_inst, 0, &buff_out[0], S_OUT_0);
      void *addr = ll_aton_reloc_get_output(nn_inst, 0);
      if (addr != (void*)&buff_out[0])
        return;
  }
#endif
#endif
}


/*
* Internal structure to handle
* the current ATON execution context
*/
struct _npu_exec_context
{
  uint32_t mode;                      /* exec mode */
  uint64_t cpu_cycles_start;          /* Accumulated number of CPU cycles between PRE_START and POST_START */
  uint64_t cpu_cycles_npu;            /* Accumulated number of CPU cycles between POST_START and PRE_END (npu core) */
  uint64_t cpu_cycles_end;            /* Accumulated number of CPU cycles between PRE_END and POST_END */

  uint64_t cpu_cycles_all;
  uint64_t npu_cycles_all;

  uint16_t exec_epoch_idx;

  npu_user_cb user_cb;

  struct npu_epoch_counters cur_epoch;
};


struct _npu_exec_context g_npu_exec_ctx;


#define _NPU_CLK_COUNTER         0
#define _STRENG_COUNTER_IDX      1


void _dump_streng_counters(uint32_t mask, uint32_t *counters)
{
  int i;
  int counter = _STRENG_COUNTER_IDX;
  uint32_t *pw = counters;
  for (i = 0; i < ATON_STRENG_NUM; i++)
  {
    if (mask & (1 << i))
      *pw++ = LL_Dbgtrc_Counter_Read(0, counter++);
  }
}

__STATIC_INLINE void _init_npu_free_counter(void)
{
    LL_Dbgtrc_Counter_InitTypdef counter_init;
    counter_init.signal = DBGTRC_VDD;
    counter_init.evt_type = DBGTRC_EVT_HI;
    counter_init.wrap = 0;
    counter_init.countdown = 0;
    counter_init.int_disable = 1;
    counter_init.counter = 0;
    LL_Dbgtrc_Counter_Init(0, _NPU_CLK_COUNTER, &counter_init);
    LL_Dbgtrc_Counter_Start(0, _NPU_CLK_COUNTER);
}

__STATIC_INLINE void _reset_npu_free_counter(uint32_t fmt)
{
  if (fmt & COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN)) {
    volatile uint32_t *reg = (volatile uint32_t *)(ATON_DEBUG_TRACE_EVENT_CNT_ADDR(0, _NPU_CLK_COUNTER));
    *reg = 0;
  }
}

__STATIC_INLINE void _deinit_npu_free_counter(void)
{
    LL_Dbgtrc_Counter_Stop(0, _NPU_CLK_COUNTER);
}

__STATIC_INLINE uint32_t _get_cycles_npu_free_counter(uint32_t fmt)
{
  if (fmt & COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN))
    return LL_Dbgtrc_Counter_Read(0, _NPU_CLK_COUNTER);
  return 0;
}


static void _npu_counters_PRE_START(struct _npu_exec_context *ctx,
                                    const LL_ATON_RT_EpochBlockItem_t *epoch_block)
{

  /* Set/check the supported config for the counters */

  ctx->cur_epoch.counter_fmt = 0;

  if ((ctx->mode & COUNTER_OPT_EPOCH_LEN) && (!(ctx->mode & COUNTER_OPT_BUSIF_RW_DATA))) // EPOCH_LEN only
  {
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN);
  }

  if (ctx->mode & COUNTER_OPT_STRG_I_ACTIVE) // STRENG_ACTIVE ALL IN
  {
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_STRG_I_ACTIVE);
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN);
  }
  else if (ctx->mode & COUNTER_OPT_STRG_O_ACTIVE) // STRENG_ACTIVE ALL OUT
  {
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_STRG_O_ACTIVE);
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN);
  }
  else if (ctx->mode & COUNTER_OPT_STRG_ACTIVE) // STRENG_ACTIVE
  {
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_STRG_ACTIVE);
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN);
  }
  else if (ctx->mode & COUNTER_OPT_STRG_HENV) // STRENG_HENV
  {
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_STRG_HENV);
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN);
  }
  else if (ctx->mode & COUNTER_OPT_BUSIF_RW_DATA) // PORT_RW_BURSTLEN
  {
    // all counters are used, EPOCH_LEN can be not used
    ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_BUSIF_RW_DATA);
  }

#if USE_COUNTER_NPU_CACHE
  if (ctx->mode & COUNTER_OPT_NPU_CACHE) // NPU_CACHE
  {
	ctx->cur_epoch.counter_fmt |= COUNTER_FMT_OPT(COUNTER_OPT_NPU_CACHE);
  }
#endif

  /* Enable the counters - PRE_START */

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN)) {
    _init_npu_free_counter();
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_BUSIF_RW_DATA))
  {
    LL_Dbgtrc_BurstLenBenchStart(0);
    g_npu_exec_ctx.cur_epoch.counter_fmt |= NPU_MAX_COUNTERS;
  }

#if USE_COUNTER_NPU_CACHE
  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_NPU_CACHE))
  {
    npu_cache_counters_enable();
  }
#endif

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_I_ACTIVE))
  {
    ctx->cur_epoch.counter_fmt |= LL_Dbgtrc_Count_StrengActive_Config(0x3FF, 0,
                                                                    _STRENG_COUNTER_IDX);
    if (COUNTER_FMT_NUMBER(ctx->cur_epoch.counter_fmt) > 0) {
      LL_Dbgtrc_Count_StrengActive_Start(0x3FF, 0, _STRENG_COUNTER_IDX);
    }
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_O_ACTIVE))
  {
    ctx->cur_epoch.counter_fmt |= LL_Dbgtrc_Count_StrengActive_Config(0, 0x3FF,
                                                                    _STRENG_COUNTER_IDX);
    if (COUNTER_FMT_NUMBER(ctx->cur_epoch.counter_fmt) > 0) {
      LL_Dbgtrc_Count_StrengActive_Start(0, 0x3FF, _STRENG_COUNTER_IDX);
    }
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_ACTIVE))
  {
    ctx->cur_epoch.counter_fmt |= LL_Dbgtrc_Count_StrengActive_Config(epoch_block->in_streng_mask,
                                                                      epoch_block->out_streng_mask,
                                                                      _STRENG_COUNTER_IDX);
    if (COUNTER_FMT_NUMBER(ctx->cur_epoch.counter_fmt) > 0) {
      LL_Dbgtrc_Count_StrengActive_Start(epoch_block->in_streng_mask,
                                         epoch_block->out_streng_mask,
                                         _STRENG_COUNTER_IDX);
    }
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_HENV))
  {
    ctx->cur_epoch.counter_fmt |= LL_Dbgtrc_Count_StrengHENV_Config(epoch_block->in_streng_mask,
                                                                    _STRENG_COUNTER_IDX);
    if (COUNTER_FMT_NUMBER(ctx->cur_epoch.counter_fmt) > 0) {
      LL_Dbgtrc_Count_StrengHENV_Start(epoch_block->in_streng_mask,
                                       _STRENG_COUNTER_IDX);

    }
  }
}

static void _npu_counters_POST_START(struct _npu_exec_context *ctx,
                                    const LL_ATON_RT_EpochBlockItem_t *epoch_block)
{

}

static void _npu_counters_PRE_END(struct _npu_exec_context *ctx, const uint32_t ts_npu,
                                  const LL_ATON_RT_EpochBlockItem_t *epoch_block)
{
}

static void _npu_counters_POST_END(struct _npu_exec_context *ctx, const uint32_t ts_npu,
                                   const LL_ATON_RT_EpochBlockItem_t *epoch_block)
{
  /* Read the counters */
#if USE_COUNTER_NPU_CACHE
  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_NPU_CACHE))
  {
    npu_cache_counters_get(&g_npu_exec_ctx.cur_epoch.cache_counters[0]);
    npu_cache_counters_disable();
  }
#endif

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_I_ACTIVE))
  {
    LL_Dbgtrc_Count_StrengActive_Stop(0x3FF, 0x0, _STRENG_COUNTER_IDX);
    _dump_streng_counters(0x3FF, &ctx->cur_epoch.counters[0]);
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_O_ACTIVE))
  {
    LL_Dbgtrc_Count_StrengActive_Stop(0x0, 0x3FF, _STRENG_COUNTER_IDX);
    _dump_streng_counters(0x3FF, &ctx->cur_epoch.counters[0]);
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_ACTIVE))
  {
    LL_Dbgtrc_Count_StrengActive_Stop(epoch_block->in_streng_mask,
                                      epoch_block->out_streng_mask,
                                      _STRENG_COUNTER_IDX);
    _dump_streng_counters(epoch_block->in_streng_mask | epoch_block->out_streng_mask,
                          &ctx->cur_epoch.counters[0]);
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_STRG_HENV))
  {
    LL_Dbgtrc_Count_StrengHENV_Stop(epoch_block->in_streng_mask, _STRENG_COUNTER_IDX);
    _dump_streng_counters(epoch_block->in_streng_mask,
                          &ctx->cur_epoch.counters[0]);
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_BUSIF_RW_DATA))
  {
    LL_Dbgtrc_BurstLenGet(0, (unsigned int *)&ctx->cur_epoch.counters[0]);
  }

  if (ctx->cur_epoch.counter_fmt & COUNTER_FMT_OPT(COUNTER_OPT_EPOCH_LEN)) {
    _deinit_npu_free_counter();
  }
}

void _rt_callback(LL_ATON_RT_Callbacktype_t ctype)
{
  if(ctype == LL_ATON_RT_Callbacktype_RT_Init){
    /* Initialize Debug and Trace Unit counter */
    LL_Dbgtrc_EnableClock();
    LL_Dbgtrc_Init(0);
  }
  else {
    LL_Dbgtrc_Deinit(0);
    LL_Dbgtrc_DisableClock();
  }
}


/* Callbacks used for benchmarking purposes */
static void _epoch_callback(LL_ATON_RT_Callbacktype_t ctype,
                            const NN_Instance_TypeDef *nn_instance,
                            const LL_ATON_RT_EpochBlockItem_t *epoch_block)
{
  const uint32_t ts = port_dwt_get_cycles();
  const uint32_t ts_npu = _get_cycles_npu_free_counter(g_npu_exec_ctx.cur_epoch.counter_fmt);

  if (ctype == LL_ATON_RT_Callbacktype_PRE_START)
  {
    g_npu_exec_ctx.cur_epoch.npu_core = 0;
    g_npu_exec_ctx.cpu_cycles_all += ts;

    if (g_npu_exec_ctx.user_cb != NULL) {
      g_npu_exec_ctx.user_cb(ctype, g_npu_exec_ctx.exec_epoch_idx, epoch_block, NULL);
    }

    g_npu_exec_ctx.cur_epoch.counter_fmt = 0;
#if defined(USE_NPU_COUNTERS) && USE_NPU_COUNTERS == 1
    _npu_counters_PRE_START(&g_npu_exec_ctx, epoch_block);
#endif

    g_npu_exec_ctx.exec_epoch_idx += 1;
    _reset_npu_free_counter(g_npu_exec_ctx.cur_epoch.counter_fmt);
  }

  else if (ctype == LL_ATON_RT_Callbacktype_POST_START)
  {
    g_npu_exec_ctx.cur_epoch.npu_start = ts_npu;
    g_npu_exec_ctx.cpu_cycles_start += ts;
    g_npu_exec_ctx.cur_epoch.cpu_start = ts;

    if (g_npu_exec_ctx.mode)
      _npu_counters_POST_START(&g_npu_exec_ctx, epoch_block);
  }

  else if (ctype == LL_ATON_RT_Callbacktype_PRE_END)
  {
    g_npu_exec_ctx.cur_epoch.npu_core = ts_npu - g_npu_exec_ctx.cur_epoch.npu_start;
    g_npu_exec_ctx.cur_epoch.npu_end = ts_npu;

    if (g_npu_exec_ctx.mode)
      _npu_counters_PRE_END(&g_npu_exec_ctx, ts_npu, epoch_block);

    g_npu_exec_ctx.npu_cycles_all += g_npu_exec_ctx.cur_epoch.npu_core;
    g_npu_exec_ctx.cpu_cycles_npu += ts;
    g_npu_exec_ctx.cur_epoch.cpu_core = ts;
  }

  else if (ctype == LL_ATON_RT_Callbacktype_POST_END)
  {
    g_npu_exec_ctx.cur_epoch.npu_end = ts_npu - g_npu_exec_ctx.cur_epoch.npu_end;
    g_npu_exec_ctx.cpu_cycles_end += ts;
    g_npu_exec_ctx.cur_epoch.cpu_end = ts;

    if (g_npu_exec_ctx.mode)
      _npu_counters_POST_END(&g_npu_exec_ctx, ts_npu, epoch_block);

    if (g_npu_exec_ctx.user_cb != NULL) {
      g_npu_exec_ctx.user_cb(ctype, g_npu_exec_ctx.exec_epoch_idx - 1, epoch_block, &g_npu_exec_ctx.cur_epoch);
    }
  };

  port_dwt_reset();
}

/* ATON software reset */
static void _npu_internal_reset(void)
{
  uint32_t t;

  /* Clear pipeline */
  t = ATON_CLKCTRL_CTRL_GET(0);
  t = ATON_CLKCTRL_CTRL_SET_CLR(t, 1);
  ATON_CLKCTRL_CTRL_SET(0, t);
}

#define AI_RELOC_ROUND_UP_32B(_v)   (((_v) + 31) & ~31)   /* 32-Bytes aligned */
#define AI_RELOC_ROUND_DOWN_32B(_v) ((_v) & ~31)          /* 32-Bytes aligned */

static void _prepare_input_buffers(struct npu_instance *instance)
{
  if (!instance)
    return;

  for (int i=0; i < instance->info.n_inputs; i++) {
    const LL_Buffer_InfoTypeDef *ll_buf = instance->info.in_bufs[i];
    LL_ATON_Cache_MCU_Clean_Invalidate_Range((uintptr_t)LL_Buffer_addr_start(ll_buf), LL_Buffer_len(ll_buf));
  }

  for (int i=0; i < instance->info.n_outputs; i++) {
    const LL_Buffer_InfoTypeDef *ll_buf = instance->info.out_bufs[i];
    LL_ATON_Cache_MCU_Invalidate_Range((uintptr_t)LL_Buffer_addr_start(ll_buf), LL_Buffer_len(ll_buf));
  }

#ifdef USE_NPU_CACHE
  npu_cache_invalidate();
#endif
}

static void _prepare_output_buffers(struct npu_instance *instance)
{
  return;
}

static void _force_clean_cache_subsystem(struct npu_instance *instance)
{
#ifdef USE_NPU_CACHE
  npu_cache_invalidate();
#endif
  mcu_cache_clean_invalidate();
}


/* -------------------------------------------------------------------------
* Wrapper entry points
* -------------------------------------------------------------------------
*/

/*
* Retrieve and populate an instance of the model.
*
*  'idx'        index of the expected model 0..N
*  'instance'   structure to handle the instance. If the index is
*               valid, the model info is populated.
*
*   if invalid index is provided, -2 is returned else 0.
*
*/
int npu_get_instance_by_index(int idx, struct npu_instance *instance)
{
  if (!instance)
    return -1;

  port_dwt_reset();
  instance->impl = _get_nn_instance(idx);
  instance->install_cycles = port_dwt_get_cycles();

  if (!instance->impl)
    return -2;

  _populate_nn_info(instance);

  instance->state = 0;
  instance->user_cb = NULL;

  return 0;
}

/*
* Retrieve the description of the input LL buffer(s)
*
*  'instance'   structure to handle the instance.
*  'num'        index of the tensor (if =-1, all are returned)
*
*   if invalid NULL is returned
*
*/
const LL_Buffer_InfoTypeDef* npu_get_input_buffers_info(const struct npu_instance *instance, int32_t num)
{
  const LL_Buffer_InfoTypeDef *aton_buf;
  int32_t idx = 0;

  if (!instance)
    return NULL;

  const NN_Instance_TypeDef *nn_inst = instance->impl;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
  if (nn_inst->exec_state.inst_reloc != 0)
    aton_buf = &ll_aton_reloc_get_input_buffers_info(nn_inst, -1)[0];
  else
    aton_buf = nn_inst->network->input_buffers_info();
#else
  aton_buf = nn_inst->network->input_buffers_info();
#endif

  if (num < 0)
    return aton_buf;

  for (; aton_buf->name != NULL; aton_buf++) {
    if (aton_buf->is_param == 0) {
      if (idx == num) {
        return aton_buf;
      }
      idx++;
    } else {
      return NULL;
    }
  }
  return NULL;
}

/*
* Retrieve the description of the output LL buffer(s)
*
*  'instance'   structure to handle the instance.
*  'num'        index of the tensor (if =-1, all are returned)
*
*   if invalid NULL is returned
*
*/
const LL_Buffer_InfoTypeDef* npu_get_output_buffers_info(const struct npu_instance *instance, int32_t num)
{
  const LL_Buffer_InfoTypeDef *aton_buf;
  int32_t idx = 0;

  if (!instance)
    return NULL;

  const NN_Instance_TypeDef *nn_inst = instance->impl;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
  if (nn_inst->exec_state.inst_reloc != 0)
    aton_buf = &ll_aton_reloc_get_output_buffers_info(nn_inst, -1)[0];
  else
    aton_buf = nn_inst->network->output_buffers_info();
#else
  aton_buf = nn_inst->network->output_buffers_info();
#endif

  if (num < 0)
    return aton_buf;

  for (; aton_buf->name != NULL; aton_buf++) {
    if (aton_buf->is_param == 0) {
      if (idx == num) {
        return aton_buf;
      }
      idx++;
    } else {
      return NULL;
    }
  }
  return NULL;
}

/*
* Retrieve the description of the internal LL buffer(s)
*
*  'instance'   structure to handle the instance.
*  'num'        index of the tensor (if =-1, all are returned)
*
*   if invalid NULL is returned
*
*/
const LL_Buffer_InfoTypeDef* npu_get_internal_buffers_info(const struct npu_instance *instance)
{
  const LL_Buffer_InfoTypeDef *aton_buf;

  if (!instance)
    return NULL;

  const NN_Instance_TypeDef *nn_inst = instance->impl;

#if defined(USE_RELOC_MODE) && USE_RELOC_MODE == 1
  if (nn_inst->exec_state.inst_reloc != 0)
    aton_buf = &ll_aton_reloc_get_internal_buffers_info(nn_inst)[0];
  else
    aton_buf = nn_inst->network->internal_buffers_info();
#else
  aton_buf = nn_inst->network->internal_buffers_info();
#endif

  return aton_buf;
}

/*
* Register the user callback for a given instance.
*/
int npu_set_callback(struct npu_instance *instance, npu_user_cb user_cb)
{
  if (!instance)
    return -1;

  instance->user_cb = user_cb;
  return 0;
}

/*
* Initialize/reset the instance
*
*  mode = 0 - reset/disable the instance
*  mode = 1 - init/install the instance
*             if state==1, instance is re-initialized
*  mode = 2 - reset the instance if state==1
*
*/
int npu_init(struct npu_instance *instance, uint32_t mode)
{
  if (!instance)
    return -1;

  port_dwt_reset();
  if (mode == 0) {
    /* Disable TOP NPU/CACHE clocks */
    instance->state = 0;
    LL_ATON_RT_DeInit_Network(instance->impl);
    LL_ATON_RT_RuntimeDeInit();
    LL_ATON_RT_SetRuntimeCallback((TraceRuntime_FuncPtr_t)NULL);

  } else if (mode == 1) {
    /* reset the NPU IP, CACHE .. */
    /* copy the params/weights in the internal N6 RAM memories */
    /* enable the clocks */
    _force_clean_cache_subsystem(instance);
    port_dwt_init();
    _npu_internal_reset();

    LL_ATON_RT_SetRuntimeCallback(_rt_callback);
    LL_ATON_RT_RuntimeInit();                       // Initialize runtime
    LL_ATON_RT_Init_Network(instance->impl);  // Initialize passed network instance object

    instance->state = 1;
  } else if (mode == 2) {
    if (instance->state == 1) {
      /* reset the NPU cache ... */
      _force_clean_cache_subsystem(instance);
    }
  }
  instance->init_cycles = port_dwt_get_cycles();

  return 0;
}

/*
* Run a simple inference
*/
int npu_run(struct npu_instance *instance, struct npu_counters *counters)
{
  bool should_be_deinit = false;
  LL_ATON_RT_RetValues_t ll_aton_rt_ret = LL_ATON_RT_DONE;

  if (counters)
    memset(counters, 0, sizeof(struct npu_counters));

  if (!instance)
    return -1;

  memset(&g_npu_exec_ctx, 0, sizeof(struct _npu_exec_context));

  if (instance->state == 0) {
    npu_init(instance, 1);
    should_be_deinit = true;
  }

  if (instance->user_cb) {
    g_npu_exec_ctx.user_cb = instance->user_cb;
    g_npu_exec_ctx.mode = instance->option;
    LL_ATON_RT_SetEpochCallback(_epoch_callback, instance->impl);
  } else {
    g_npu_exec_ctx.mode = 0;
  }

  /* --   LL_ATON_RT_Main(); -- */

  uint32_t tick = port_hal_get_tick();
  g_npu_exec_ctx.cpu_cycles_all = 0;
  port_dwt_reset();

  _prepare_input_buffers(instance);

  LL_ATON_RT_Reset_Network(instance->impl);

  // Set bus interface keys -- used for encrypted inference only
  LL_Busif_SetKeys ( 0 , 0 , BUSIF_LSB_KEY , BUSIF_MSB_KEY );
  LL_Busif_SetKeys ( 0 , 1 , BUSIF_LSB_KEY , BUSIF_MSB_KEY );
  LL_Busif_SetKeys ( 1 , 0 , BUSIF_LSB_KEY , BUSIF_MSB_KEY );
  LL_Busif_SetKeys ( 1 , 1 , BUSIF_LSB_KEY , BUSIF_MSB_KEY );

  do {
    /* Execute first/next step of Cube.AI/ATON runtime */
    ll_aton_rt_ret = LL_ATON_RT_RunEpochBlock(instance->impl);

    /* Wait for next event */
    if (ll_aton_rt_ret == LL_ATON_RT_WFE) {
      LL_ATON_OSAL_WFE();
    }
  } while (ll_aton_rt_ret != LL_ATON_RT_DONE);

  _prepare_output_buffers(instance);

  g_npu_exec_ctx.cpu_cycles_all += port_dwt_get_cycles();
  tick = port_hal_get_tick() - tick;

  /* --   LL_ATON_RT_Main(); -- */

  LL_ATON_RT_SetEpochCallback((TraceEpochBlock_FuncPtr_t)NULL, instance->impl);
  g_npu_exec_ctx.user_cb = NULL;

  if (should_be_deinit) {
    npu_init(instance, 0);
  }

  if (ll_aton_rt_ret != LL_ATON_RT_DONE)
    return -1;

  if (counters) {
    g_npu_exec_ctx.cpu_cycles_all += g_npu_exec_ctx.cpu_cycles_start;
    g_npu_exec_ctx.cpu_cycles_all += g_npu_exec_ctx.cpu_cycles_npu;
    g_npu_exec_ctx.cpu_cycles_all += g_npu_exec_ctx.cpu_cycles_end;

    if (instance->user_cb || tick < 3000)  /* CPU cycles are used in this case */
      counters->cpu_all = g_npu_exec_ctx.cpu_cycles_all;
    else { /* get_tick() is used to avoid CPU counter overflow - precision 1ms */
      counters->cpu_all = (uint64_t)tick * (port_hal_get_cpu_freq() / 1000);
    }

    counters->cpu_start = g_npu_exec_ctx.cpu_cycles_start;
    counters->cpu_core = g_npu_exec_ctx.cpu_cycles_npu;
    counters->cpu_end = g_npu_exec_ctx.cpu_cycles_end;
    counters->extra = g_npu_exec_ctx.npu_cycles_all;
  }

  return tick;
}
