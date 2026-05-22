/**
 ******************************************************************************
 * @file    nn_task.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   Defines the API for the NN module.
 *          This module is responsible for:
 *          - Initialize NN model and PP
 *          - Run inference
 ******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2024 SIANA Systems</center></h2>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 ******************************************************************************  
 */
#include "nn_task.h"

#if ENABLE_NN == 1U
#include "camera_task.h"
#include "flash.h"
#include "n6cam_npu.h"
#include "stai.h"
#include "stai_network.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup Tasks
* @{
* @addtogroup NN
* @{
* @defgroup PUBLIC_Definitions          PUBLIC constants
* @defgroup PUBLIC_Macros               PUBLIC macros
* @defgroup PUBLIC_Types                PUBLIC data-types
* @defgroup PUBLIC_Data                 PUBLIC data / variables
* @defgroup PUBLIC_API                  PUBLIC API
* @defgroup PRIVATE_TUNABLES            PRIVATE compile-time tunables
* @defgroup PRIVATE_Definitions         PRIVATE constants
* @defgroup PRIVATE_Macros              PRIVATE macros
* @defgroup PRIVATE_Types               PRIVATE data-types
* @defgroup PRIVATE_Data                PRIVATE data / variables
* @defgroup PRIVATE_Functions           PRIVATE functions
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_TUNABLES
* @{
*//*--------------------------------------------------------------------------*/

/* NN task tunables
 * TODO: Optimize stack size
 */
#define NN_TASK_STACK_SIZE      (2U * 1024U)
#define NN_TASK_PRIO            APP_PRIO_IMPORTANT
#define NN_TASK_TIME_SLICE      APP_TIME_SLICE_DEFAULT

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** NN task handler */
typedef struct
{
  TX_MUTEX      pp_box_mtx;
  TX_SEMAPHORE  aton_sem;
  TX_THREAD     thread;
  uint8_t       stack[NN_TASK_STACK_SIZE];
} t_nn_task;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/* Task handler */
static t_nn_task                _nn_task = { 0 };

/* Internals */
static uint8_t*                 _nn_frame = NULL;
static uint8_t*                 _nn_in;
static uint32_t                 _nn_in_len;
static uint8_t*                 _nn_out[STAI_NETWORK_OUT_NUM] = { 0 };
static uint32_t                 _nn_out_len[STAI_NETWORK_OUT_NUM] = { 0 };
static uint32_t                 _nn_out_nb = 0;

static size_t                   _pp_box_count;
static t_nn_box                 _pp_box_buff[NN_BOXES_MAX_NUM];
static t_nn_params              _pp_params;

/* Model and Model Interface Instantiation */
STAI_NETWORK_CONTEXT_DECLARE(network_context, STAI_NETWORK_CONTEXT_SIZE)

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _nn_task_run(uint32_t args);
static void _nn_task_init(void);
static void _nn_config_npu(void);

static void _nn_frame_process(void);

static void _pp_publish_objects(t_nn_boxes *out);

extern void ll_aton_osal_wfe(void);
extern void ll_aton_osal_signal_event(void);
extern void NPU_END_OF_EPOCH_IRQHandler(void);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t nn_task_start(void)
{
  return (int32_t)tx_thread_create(
    &_nn_task.thread, "tx.task.nn",
    _nn_task_run, 0,
    _nn_task.stack, NN_TASK_STACK_SIZE,
    NN_TASK_PRIO, NN_TASK_PRIO,
    NN_TASK_TIME_SLICE, TX_AUTO_START
  );
}

uint32_t nn_get_detections(t_nn_box* box_buff)
{
  /* Validate */
  if (!box_buff || (_pp_box_count == 0))
  {
    return 0;
  }

  /* Capture detections */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);

  memcpy(box_buff, &_pp_box_buff, _pp_box_count * sizeof(t_nn_box));

  /* Release and return */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
  return _pp_box_count;
}

uint32_t nn_task_suspend_thread(void)
{
  return tx_thread_suspend(&_nn_task.thread);
}

uint32_t nn_task_resume_thread(void)
{
  return tx_thread_resume(&_nn_task.thread);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief NN task entry point.
 * @param args Task arguments
 */
static void _nn_task_run(uint32_t args)
{
  UNUSED(args);

  /* Initialize task */
  _nn_task_init();

  /* NN task */
  while (1)
  {
    /* Wait for a new frame */
    camera_wait_event(CAMERA_EVT_FRAME_ANCILLARY, true);

    /* Process frame */
    _nn_frame = camera_get_buffer(camera.ancillary.id);
    if (_nn_frame != NULL)
    {
      stat_time_start(STAT_TIME_NN_TOTAL);
      _nn_frame_process();
      stat_time_stop(STAT_TIME_NN_TOTAL);
    }
  }
}

/**
 * @brief NN task initialization.
 */
static void _nn_task_init(void)
{
  const LL_Buffer_InfoTypeDef *in_info;
  const LL_Buffer_InfoTypeDef *out_info;
  int32_t                     status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_NN_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Initialize NPU */
  _nn_config_npu();

  /* Initialize ATON */
  status = tx_semaphore_create(&_nn_task.aton_sem, "tx.sem.nn", 1U);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize boxes */
  _pp_box_count = 0;
  memset(_pp_box_buff, 0x00U, sizeof(_pp_box_buff));
  status = tx_mutex_create(&_nn_task.pp_box_mtx, "tx.mtx.nn.pp", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize STAI */
  status = stai_runtime_init();
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI runtime init failed");
    Error_Handler();
  }
  /* Initialize network */
  status = stai_network_init(network_context);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network init failed");
    Error_Handler();
  }

  /* Get network buffers info */
  stai_network_info info;
  status = stai_network_get_info(network_context, &info);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get info failed");
    Error_Handler();
  }

  _nn_out_nb = STAI_NETWORK_OUT_NUM;

  /* Get the input buffer size & address */
  _nn_in_len = info.inputs[0].size_bytes;
  status = stai_network_get_inputs(network_context, &_nn_in, (stai_size *)&info.n_inputs);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get inputs failed");
    Error_Handler();
  }

  /* Get the output buffers size & address */
  status = stai_network_get_outputs(network_context, _nn_out, &_nn_out_nb);
  if (status != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network get outputs failed");
    Error_Handler();
  }
  for (int i = 0; i < _nn_out_nb; i++)
  {
    _nn_out_len[i] = info.outputs[i].size_bytes;
  }



  /* Initialize PP */
  status = app_postprocess_init(&_pp_params, &info);
  if (status != AI_OD_POSTPROCESS_ERROR_NO)
  {
    LERROR(TRACE_NN, "PP init failed");
    Error_Handler();
  }

  /*-->> READY <<--*/
  LINFO(TRACE_NN, "Task started");
  task_raise_event(TX_EVT_NN_READY);
}

/**
 * @brief NPU configuration.
 */
static void _nn_config_npu(void)
{
  bsp_npu_init();
  bsp_npu_ram_enable();
  bsp_npu_cache_enable();
}

/**
 * @brief Process a new ancillary frame in the NPU
 */
static void _nn_frame_process(void)
{
  t_nn_boxes  pp_output;
  int32_t     status;

  /* Invalidate data and prepare NN input */
  stat_time_start(STAT_TIME_NN_COPY);
  SCB_InvalidateDCache_by_Addr(_nn_frame, CAMERA_ANCILLARY_BUFFER_SIZE);
  memcpy(_nn_in, _nn_frame, CAMERA_ANCILLARY_BUFFER_SIZE);
  SCB_CleanDCache_by_Addr(_nn_in, CAMERA_ANCILLARY_BUFFER_SIZE);
  stat_time_stop(STAT_TIME_NN_COPY);

  /* Acquire FLASH (weights) */
  flash_acquire(true);

  /* Run ATON */
  stat_time_start(STAT_TIME_NN_MODEL);
  stai_return_code ret;

  do {
    ret = stai_network_run(network_context, STAI_MODE_ASYNC);
    if (ret == STAI_RUNNING_WFE)
      LL_ATON_OSAL_WFE();
  } while (ret == STAI_RUNNING_WFE || ret == STAI_RUNNING_NO_WFE);

  ret = stai_ext_network_new_inference(network_context);
  if (ret != STAI_SUCCESS)
  {
    LERROR(TRACE_NN, "STAI network new inference failed");
    Error_Handler();
  }
  stat_time_stop(STAT_TIME_NN_MODEL);

  /* Release FLASH */
  flash_acquire(false);

  /* Run PP */
  pp_output.pOutBuff = NULL;
  stat_time_start(STAT_TIME_NN_PP);
  status = app_postprocess_run((void**)_nn_out, _nn_out_nb, &pp_output, &_pp_params);
  if (status != AI_OD_POSTPROCESS_ERROR_NO)
  {
    LERROR(TRACE_NN, "PP process failed");
    Error_Handler();
  }
  stat_time_stop(STAT_TIME_NN_PP);

  /* Store detections */
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, true);
  _pp_publish_objects(&pp_output);
  rtos_mutex_acquire(&_nn_task.pp_box_mtx, false);
}

/**
 * @brief Publish PP output from PP temporal memory to nn_task output buffer  
 *
 * @param out PP output
 */
static void _pp_publish_objects(t_nn_boxes *out)
{
  /* Convert boxes */
  memset(_pp_box_buff, 0x00U, sizeof(_pp_box_buff));
  for (int32_t idx = 0; idx < out->nb_detect; idx++)
  {
    _pp_box_buff[idx] = (t_nn_box)(out->pOutBuff[idx]);
  }
  _pp_box_count = out->nb_detect;

}

/**
 * @brief ATON started handler.
 */
void ll_aton_osal_wfe(void)
{
  rtos_semaphore_acquire(&_nn_task.aton_sem, true);
}

/**
 * @brief ATON completed handler.
 */
void ll_aton_osal_signal_event(void)
{
  rtos_semaphore_acquire(&_nn_task.aton_sem, false);
}

/**
 * @brief This function handles NPU interrupt request.
 */
void NPU_END_OF_EPOCH_IRQHandler(void)
{
  CDNN0_IRQHandler();
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: Tasks -->
* @} <!-- End: NN -->
*//*--------------------------------------------------------------------------*/
#endif /* ENABLE_NN */
