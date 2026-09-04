/**
 *******************************************************************************
 * @file    n6cam_uart.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam UART BSP
 *******************************************************************************
 * @attention
 *
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
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
 *******************************************************************************
 */
#include <string.h>

#include "n6cam_core.h"
#include "n6cam_mcu.h"
#include "n6cam_uart.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup UART
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

/* UART defaults */
#define UART_MODE_DEFAULT     UART_MODE_DMA   /*!< UART operation mode */
#define UART_IT_PRIO          5U              /*!< UART interrupt priority */

/** RX ring size, per UART (ScopusQA #23).
 *
 * 1024 bytes is 89 ms of line time at 115200, two orders of magnitude more
 * than any gap the modem loop leaves today, and enough that the ring survives
 * a caller that goes away for a while. The whole thing costs 2 KB of the
 * 256 KB uncached SRAM region. bsp_uart_ring_stats() reports the deepest fill
 * ever reached, so this number can be argued with evidence rather than
 * guessed at again. */
#define UART_RX_RING_SIZE     1024U

/** RX FIFO threshold.
 *
 * The silicon has a 16-byte RX FIFO per USART and the BSP configured a
 * threshold and then called HAL_UARTEx_DisableFifoMode(), which made both
 * threshold calls inert and left the peripheral servicing one byte at a time.
 * The FIFO is now on.
 *
 * 1/2 (8 bytes) is the interrupt threshold. It does NOT gate the DMA: the
 * USART raises its DMA request on RXFNE, FIFO not empty, so a partial
 * FIFO is still drained and receive-to-idle still completes on the gap after
 * the last character. What the FIFO buys is eight characters of slack before
 * an overrun, on a link whose ORE/FE/NE counter is not always zero. */
#define UART_RX_FIFO_THRESH   UART_RXFIFO_THRESHOLD_1_2
#define UART_TX_FIFO_THRESH   UART_TXFIFO_THRESHOLD_1_8

/** Where the ring and its DMA descriptor live.
 *
 * The Application declares this in main.h, which is Application-private; this
 * file is BSP and is compiled into the FSBL as well, so it names the section
 * itself. Both linker scripts place `.uncached_bss`: in the Application it is
 * the MPU's non-cacheable SRAM region, in the FSBL it is ordinary SRAM that
 * nothing uses, the FSBL never starts a ring. */
#ifndef IN_SRAM_UNCACHED
  #define IN_SRAM_UNCACHED    IN_SECTION(".uncached_bss")
#endif

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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

/** UART devices */
t_uart                  uart[UART_NUM] = { 0 };

/** UART RTOS naming */
static const char*      _uart_name[UART_NUM][4U] = {
  {"tx.evt.uart1.rx", "tx.mtx.uart1.rx", "tx.mtx.uart1.tx", "tx.evt.uart1.tx"},
  {"tx.evt.uart2.rx", "tx.mtx.uart2.rx", "tx.mtx.uart2.tx", "tx.evt.uart2.tx"},
};

/** AUX for RX complete */
static volatile size_t  _uart_recv[UART_NUM];

/** Peripheral error counter (ORE/FE/NE), per UART. Exposed via
 *  bsp_uart_get_errors() so a stalled link can be told apart from a silent
 *  one -- a rising count means bytes ARE hitting the pin but are malformed
 *  (wrong baud, bad levels, noise), which no other diagnostic surfaces. */
static volatile uint32_t _uart_errors[UART_NUM];

/* ── RX ring (ScopusQA #23) ────────────────────────────────────────────────
 *
 * A GPDMA single-node CIRCULAR linked list writes here for as long as the
 * UART is up; readers copy out. The N6 GPDMA has no DMA_CIRCULAR Init.Mode -
 * looping is done with the linked-list engine, which is the mechanism ITP
 * asked for, and the HAL supports it under HAL_UARTEx_ReceiveToIdle_DMA:
 * in DMA_LINKEDLIST_CIRCULAR it reports idle/half/complete and leaves the
 * channel running.
 *
 * IN_SRAM_UNCACHED, not cached SRAM with maintenance around it. The DMA
 * writes this buffer continuously and asynchronously, so there is no moment
 * at which "invalidate now, read now" is race-free: a line invalidated while
 * the DMA is mid-line is a line that may be re-read stale. Uncached memory
 * removes the question. The linked-list NODES live here too, the DMA
 * fetches them itself, so they must not sit behind the D-cache either. */
static uint8_t _uart_rx_ring[UART_NUM][UART_RX_RING_SIZE]
  DMA_ALIGN IN_SRAM_UNCACHED;
static DMA_NodeTypeDef  _uart_rx_node[UART_NUM] DMA_ALIGN IN_SRAM_UNCACHED;
static DMA_QListTypeDef _uart_rx_qlist[UART_NUM];

/* Monotonic byte counts. The difference between them is what is waiting;
 * keeping them monotonic rather than keeping head/tail indices is what makes
 * "the DMA lapped the reader" detectable at all, two indices cannot tell a
 * full ring from an empty one. */
static volatile uint32_t _uart_rx_written[UART_NUM];  /* DMA has written    */
static volatile uint32_t _uart_rx_taken[UART_NUM];    /* callers have taken */
static uint32_t          _uart_rx_last_head[UART_NUM];
static volatile uint32_t _uart_rx_lost[UART_NUM];     /* overwritten unread */
static volatile uint32_t _uart_rx_peak[UART_NUM];     /* deepest fill seen  */
static volatile uint32_t _uart_rx_restarts[UART_NUM]; /* re-arms after error */
static int32_t           _uart_rx_start_rc[UART_NUM];  /* last ring_start result */
/* Where `bytes` is counted from. `mdm stats reset` moves this rather than
 * touching _uart_rx_taken, which is a live ring pointer. */
static volatile uint32_t _uart_rx_base[UART_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/* Internal */
static int32_t _bsp_uart_init_gpio(t_uart_id id);
static int32_t _bsp_uart_init_peripheral(t_uart_id id, uint32_t baud, bool swap);

/* Ring */
static void    _ring_sample(t_uart_id id);
static int32_t _ring_arm(t_uart_id id);
static bool    _ring_running(t_uart_id id);

/* Stream */
static int32_t _bsp_uart1_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart1_write(const uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart2_read(uint8_t *buff, size_t size, uint32_t timeout);
static int32_t _bsp_uart2_write(const uint8_t *buff, size_t size, uint32_t timeout);

/* BSP handling */
extern void GPDMA1_Channel0_IRQHandler(void);
extern void GPDMA1_Channel1_IRQHandler(void);
extern void USART1_IRQHandler(void);
extern void GPDMA1_Channel2_IRQHandler(void);
extern void GPDMA1_Channel3_IRQHandler(void);
extern void USART2_IRQHandler(void);
extern void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size);
extern void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart);
extern void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_uart_reinit(t_uart_id id, uint32_t baud, bool swap)
{
  int32_t status;

  /* Validate */
  if (id >= UART_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    /* Never brought up — a plain init is what is wanted. */
    return bsp_uart_init(id, baud, swap);
  }

  /* Stop anything in flight before the peripheral is reconfigured under it. */
  (void)HAL_UART_AbortReceive(&uart[id].bsp.huart);
  (void)HAL_UART_AbortTransmit(&uart[id].bsp.huart);
  (void)HAL_UART_DeInit(&uart[id].bsp.huart);

  /* Redo the hardware half only. The RTOS objects (event flags, mutexes,
   * stream bindings) are still valid and must NOT be recreated — doing so
   * would leak them and orphan anyone blocked on them.
   *
   * Re-running the GPIO init is the part that matters for the CN805 link:
   * it briefly returns TX to its reset state, which is what lets the FXMA108
   * auto-direction translator re-sense the line. That is why a camera reboot
   * clears the wedge, and this is the same effect without the reboot. */
  status = _bsp_uart_init_gpio(id);
  if (status != BSP_OK)
  {
    uart[id].ready = false;
    return status;
  }
  status = _bsp_uart_init_peripheral(id, baud, swap);
  if (status != BSP_OK)
  {
    uart[id].ready = false;
    return status;
  }

  return BSP_OK;
}

int32_t bsp_uart_init(t_uart_id id, uint32_t baud, bool swap)
{
  int32_t status;

  /* Validate */
  if (id >= UART_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (uart[id].ready)
  {
    return BSP_OK;
  }

  /* Init BSP */
  status = _bsp_uart_init_gpio(id);
  if (status != BSP_OK)
  {
    return status;
  }
  status = _bsp_uart_init_peripheral(id, baud, swap);
  if (status != BSP_OK)
  {
    return status;
  }

  /* Init RTOS */
  status = tx_event_flags_create(&uart[id].rtos.evt_rx, (char*)_uart_name[id][0U]);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_event_flags_create(&uart[id].rtos.evt_tx, (char*)_uart_name[id][3U]);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&uart[id].rtos.mtx_rx, (char*)_uart_name[id][1U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }
  status = tx_mutex_create(&uart[id].rtos.mtx_tx, (char*)_uart_name[id][2U], TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    return status;
  }

  /* Configure streams */
  switch (id)
  {
    case UART_1: status = stream_init(&uart[id].stream, _bsp_uart1_read, _bsp_uart1_write); break;
    case UART_2: status = stream_init(&uart[id].stream, _bsp_uart2_read, _bsp_uart2_write); break;
    default:     return BSP_ERROR_PARAMETER;
  }

  /* Set params */
  uart[id].bsp.irq_cb  = NULL;
  uart[id].bsp.mode_rx = UART_MODE_DEFAULT;
  uart[id].bsp.mode_tx = UART_MODE_DEFAULT;
  uart[id].ready       = true;
  return BSP_OK;
}

int32_t bsp_uart_set_mode(t_uart_id id, t_uart_mode mode_rx, t_uart_mode mode_tx)
{
  /* Validate */
  if (id >= UART_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Configure */
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);
  uart[id].bsp.mode_rx = mode_rx;
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, true);
  uart[id].bsp.mode_tx = mode_tx;
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, false);
  return BSP_OK;
}

uint32_t bsp_uart_get_errors(t_uart_id id)
{
  if (id >= UART_NUM)
  {
    return 0U;
  }
  return _uart_errors[id];
}

uint32_t bsp_uart_get_kernel_clock(t_uart_id id)
{
  switch (id)
  {
    case UART_1: return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART1);
    case UART_2: return HAL_RCCEx_GetPeriphCLKFreq(RCC_PERIPHCLK_USART2);
    default:     return 0U;
  }
}

uint32_t bsp_uart_get_actual_baud(t_uart_id id)
{
  /* Recover the line rate the peripheral is REALLY running at, from the live
   * BRR and the live kernel clock. Init.BaudRate only records what was asked
   * for -- if the kernel clock changed after HAL_UART_Init() computed BRR
   * (see TX_EVT_MODEM_REQUIRE), the two disagree and this is what exposes it.
   * A TX->RX loopback cannot: both directions share the same wrong divisor. */
  if ((id >= UART_NUM) || !uart[id].ready)
  {
    return 0U;
  }

  uint32_t fck = bsp_uart_get_kernel_clock(id);
  uint32_t brr = uart[id].bsp.huart.Instance->BRR;
  if ((fck == 0U) || (brr == 0U))
  {
    return 0U;
  }

  if (uart[id].bsp.huart.Init.OverSampling == UART_OVERSAMPLING_8)
  {
    /* OVER8: BRR[3] is forced 0 and BRR[2:0] hold the fraction in 1/8ths,
     * so USARTDIV = (BRR & 0xFFF0) + ((BRR & 0x7) << 1), rate = 2*fck/DIV. */
    uint32_t usartdiv = (brr & 0xFFF0U) + ((brr & 0x0007U) << 1U);
    if (usartdiv == 0U)
    {
      return 0U;
    }
    return (uint32_t)(((uint64_t)fck * 2ULL) / (uint64_t)usartdiv);
  }
  return fck / brr;
}

t_stream *bsp_uart_get_stream(t_uart_id id)
{
  /* Validate */
  if ((id >= UART_NUM) || !uart[id].ready)
  {
    return NULL;
  }
  return &uart[id].stream;
}

/* ── RX ring (ScopusQA #23) ───────────────────────────────────────────── */

/* Where the DMA has reached, and how far it has advanced since we last
 * looked. CBR1 counts DOWN from the block size and reloads on each lap, so
 * the write position is size - counter.
 *
 * The advance is folded into a monotonic total instead of being used as a
 * head index, and that is the whole trick: two indices can say how much is
 * waiting but cannot say whether the writer has been all the way round, so
 * they cannot detect loss. A monotonic pair can, see bsp_uart_read().
 *
 * Correctness rests on being called at least once per lap. The half-transfer
 * and transfer-complete events do that (every 512 bytes, ~44 ms at 115200),
 * which is why HAL_UARTEx_RxEventCallback samples too and not just readers. */
static void _ring_sample(t_uart_id id)
{
  /* Runs on the ISR AND on readers, and it is read-modify-write on state
   * both of them share, so it is short and it is atomic. The mutex cannot
   * do this job: an ISR must never wait on one. */
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();

  const uint32_t sz   = UART_RX_RING_SIZE;
  uint32_t       rem  = __HAL_DMA_GET_COUNTER(&uart[id].bsp.hdmarx);
  if (rem > sz) { rem = sz; }
  const uint32_t head = sz - rem;
  const uint32_t prev = _uart_rx_last_head[id];

  _uart_rx_last_head[id] = head;
  _uart_rx_written[id]  += (head >= prev) ? (head - prev) : (sz - prev + head);

  const uint32_t fill = _uart_rx_written[id] - _uart_rx_taken[id];
  if (fill > _uart_rx_peak[id]) { _uart_rx_peak[id] = fill; }

  __set_PRIMASK(primask);
}

/* Is the receiver still running? An RX error (ORE/FE/NE) makes the HAL abort
 * the DMA even in circular mode, UART_EndRxTransfer clears CR3.DMAR, so
 * "the ring runs forever" is only true if something puts it back. */
static bool _ring_running(t_uart_id id)
{
  return (uart[id].bsp.huart.Instance->CR3 & USART_CR3_DMAR) != 0U;
}

/* Point the DMA at the ring and start it. Also used to recover after an
 * error, which is why it resets the head shadow but NOT the byte totals:
 * the counters are cumulative for the life of the unit. */
static int32_t _ring_arm(t_uart_id id)
{
  t_uart_bsp *bsp = &uart[id].bsp;

  (void)HAL_UART_AbortReceive(&bsp->huart);

  _uart_rx_last_head[id] = 0U;
  /* Whatever the DMA wrote and nobody read is gone with the restart; say so
   * rather than let it look like a clean link. */
  const uint32_t fill = _uart_rx_written[id] - _uart_rx_taken[id];
  _uart_rx_lost[id]  += fill;
  _uart_rx_taken[id]  = _uart_rx_written[id];

  if (HAL_UARTEx_ReceiveToIdle_DMA(&bsp->huart, _uart_rx_ring[id],
                                   (uint16_t)UART_RX_RING_SIZE) != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

int32_t bsp_uart_ring_start(t_uart_id id)
{
  if (id >= UART_NUM)      { return BSP_ERROR_PARAMETER; }
  if (!uart[id].ready)     { return BSP_ERROR_NO_INIT;   }

  t_uart_bsp *bsp = &uart[id].bsp;
  int32_t     rc  = BSP_OK;

  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);

  /* Rebuild the RX channel as a linked-list channel. The plain HAL_DMA_Init
   * done at bring-up produces a one-shot channel; a looping one is a
   * different object to the HAL and has to be initialised as such.
   *
   * Every step is checked and reports WHICH step failed. A single
   * "peripheral error" out of a seven-call sequence is not a diagnosis, and
   * this sequence has to survive being run again on every relink. */
  (void)HAL_UART_AbortReceive(&bsp->huart);
  (void)HAL_DMA_DeInit(&bsp->hdmarx);

  bsp->hdmarx.InitLinkedList.Priority          = DMA_HIGH_PRIORITY;
  bsp->hdmarx.InitLinkedList.LinkStepMode      = DMA_LSM_FULL_EXECUTION;
  bsp->hdmarx.InitLinkedList.LinkAllocatedPort = DMA_LINK_ALLOCATED_PORT0;
  bsp->hdmarx.InitLinkedList.TransferEventMode = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmarx.InitLinkedList.LinkedListMode    = DMA_LINKEDLIST_CIRCULAR;

  if (HAL_DMAEx_List_Init(&bsp->hdmarx) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_LIST_INIT;
    goto EXIT;
  }
  __HAL_LINKDMA(&bsp->huart, hdmarx, bsp->hdmarx);
  if (HAL_DMA_ConfigChannelAttributes(&bsp->hdmarx,
        (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC |
         DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC)) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_ATTRS;
    goto EXIT;
  }

  /* One node, and the queue is made circular so the node re-links to itself.
   * Source/destination/size are overwritten by the HAL out of the
   * ReceiveToIdle_DMA arguments, so what matters here is the control half:
   * peripheral-to-memory, source fixed, destination incrementing, byte wide.
   *
   * The node config is static rather than a local: it is ~140 bytes and this
   * runs on the shell task, whose stack is 2 KB and already carries a command
   * parser and a print path.
   *
   * The queue is memset rather than HAL_DMAEx_List_ResetQ'd. ResetQ refuses a
   * queue that LinkQ has converted to dynamic, and this whole sequence runs
   * again on every `mdm relink`, so on the second pass the reset would fail,
   * the old node would still be in the list, and the rebuild would be
   * inserting a node that is already there. A zeroed queue is by definition a
   * fresh static one, and nothing else holds a pointer into it. */
  static DMA_NodeConfTypeDef node;
  memset(&node, 0, sizeof(node));
  node.NodeType                        = DMA_GPDMA_LINEAR_NODE;
  node.Init                            = bsp->hdmarx.Init;
  node.Init.Mode                       = DMA_NORMAL;
  node.DataHandlingConfig.DataExchange = DMA_EXCHANGE_NONE;
  node.DataHandlingConfig.DataAlignment= DMA_DATA_RIGHTALIGN_ZEROPADDED;
  node.TriggerConfig.TriggerPolarity   = DMA_TRIG_POLARITY_MASKED;
  node.SrcAddress                      = (uint32_t)&uart[id].bsp.huart.Instance->RDR;
  node.DstAddress                      = (uint32_t)_uart_rx_ring[id];
  node.DataSize                        = UART_RX_RING_SIZE;
#if defined (CPU_IN_SECURE_STATE)
  /* Not optional, and zero is not "leave it alone": this part is built
   * CPU_IN_SECURE_STATE, so HAL_DMAEx_List_BuildNode asserts on these two,
   * and IS_DMA_ATTRIBUTES rejects 0. Leaving them out ran assert_failed() on
   * the modem task at 181 ms of every boot, which spins that thread for ever
   * inside Error_Handler() while the rest of the system carries on, the link
   * looked dead and the trace said only "FAILURE (modem.task)!". They match
   * the attributes HAL_DMA_ConfigChannelAttributes() sets above. */
  node.SrcSecure                       = DMA_CHANNEL_SRC_SEC;
  node.DestSecure                      = DMA_CHANNEL_DEST_SEC;
#endif /* CPU_IN_SECURE_STATE */

  memset(&_uart_rx_qlist[id], 0, sizeof(_uart_rx_qlist[id]));
  memset(&_uart_rx_node[id],  0, sizeof(_uart_rx_node[id]));

  if (HAL_DMAEx_List_BuildNode(&node, &_uart_rx_node[id]) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_BUILD;
    goto EXIT;
  }
  if (HAL_DMAEx_List_InsertNode_Tail(&_uart_rx_qlist[id],
                                     &_uart_rx_node[id]) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_INSERT;
    goto EXIT;
  }
  if (HAL_DMAEx_List_SetCircularMode(&_uart_rx_qlist[id]) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_CIRCULAR;
    goto EXIT;
  }
  if (HAL_DMAEx_List_LinkQ(&bsp->hdmarx, &_uart_rx_qlist[id]) != HAL_OK)
  {
    rc = BSP_UART_RING_ERR_LINK;
    goto EXIT;
  }

  _uart_rx_written[id]   = 0U;
  _uart_rx_taken[id]     = 0U;
  _uart_rx_base[id]      = 0U;
  _uart_rx_last_head[id] = 0U;
  _uart_rx_lost[id]      = 0U;
  _uart_rx_peak[id]      = 0U;
  _uart_rx_restarts[id]  = 0U;

  uart[id].bsp.mode_rx = UART_MODE_RING;
  if (_ring_arm(id) != BSP_OK)
  {
    rc = BSP_UART_RING_ERR_ARM;
    goto EXIT;
  }
  /* _ring_arm charges whatever was pending to `lost`; on the very first arm
   * there is nothing pending and nothing to charge. */
  _uart_rx_lost[id] = 0U;

EXIT:
  if (rc != BSP_OK) { uart[id].bsp.mode_rx = UART_MODE_IT; }
  _uart_rx_start_rc[id] = rc;
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
  return rc;
}

void bsp_uart_ring_reset_stats(t_uart_id id)
{
  if (id >= UART_NUM) { return; }
  /* With interrupts off: `peak` is read-modify-written from the ISR, and a
   * reset that races one loses the write it was zeroing.
   *
   * `bytes` is rebased rather than zeroed. _uart_rx_taken is a live pointer
   * into the ring, not a statistic: setting it to anything would either
   * discard bytes that are waiting or re-deliver bytes already handed out. */
  const uint32_t primask = __get_PRIMASK();
  __disable_irq();
  _uart_rx_base[id]     = _uart_rx_taken[id];
  _uart_rx_lost[id]     = 0U;
  _uart_rx_peak[id]     = 0U;
  _uart_rx_restarts[id] = 0U;
  __set_PRIMASK(primask);
}

void bsp_uart_ring_health(t_uart_id id, int32_t *start_rc, uint32_t *restarts)
{
  if (id >= UART_NUM) { return; }
  if (start_rc) { *start_rc = _uart_rx_start_rc[id];  }
  if (restarts) { *restarts = _uart_rx_restarts[id];  }
}

void bsp_uart_ring_stats(t_uart_id id, uint32_t *bytes, uint32_t *lost,
                         uint32_t *peak, uint32_t *capacity)
{
  if (id >= UART_NUM) { return; }
  if (bytes)    { *bytes    = _uart_rx_taken[id] - _uart_rx_base[id]; }
  if (lost)     { *lost     = _uart_rx_lost[id];  }
  if (peak)     { *peak     = _uart_rx_peak[id];  }
  if (capacity) { *capacity = UART_RX_RING_SIZE;  }
}

/* Copy out of the ring. Never arms anything, so reception carries on
 * regardless of whether anyone is here. */
static int32_t _ring_read(t_uart_id id, uint8_t *buff, size_t size,
                          uint32_t timeout)
{
  const uint32_t sz    = UART_RX_RING_SIZE;
  const ULONG    t0    = tx_time_get();
  const ULONG    limit = (ULONG)((timeout * TX_TIMER_TICKS_PER_SECOND) / 1000U);

  for (;;)
  {
    rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);

    /* An RX error aborts the channel even in circular mode. Put it back
     * before looking, so a noisy burst costs the bytes it corrupted and not
     * the link. */
    if (!_ring_running(id))
    {
      _uart_rx_restarts[id]++;
      (void)_ring_arm(id);
    }

    _ring_sample(id);

    uint32_t fill = _uart_rx_written[id] - _uart_rx_taken[id];
    if (fill > sz)
    {
      /* The writer went all the way round and past the reader. The oldest
       * (fill - sz) bytes are already overwritten; keep the newest ring-full
       * and count the rest as lost, because pretending they were delivered
       * would put a splice in the middle of an HDLC frame. */
      _uart_rx_lost[id]  += (fill - sz);
      _uart_rx_taken[id]  = _uart_rx_written[id] - sz;
      fill = sz;
    }

    if (fill > 0U)
    {
      uint32_t n = (fill > (uint32_t)size) ? (uint32_t)size : fill;
      uint32_t t = _uart_rx_taken[id] % sz;
      uint32_t first = ((t + n) > sz) ? (sz - t) : n;
      memcpy(buff, &_uart_rx_ring[id][t], first);
      if (n > first)
      {
        memcpy(&buff[first], &_uart_rx_ring[id][0], n - first);
      }
      _uart_rx_taken[id] += n;
      rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
      return (int32_t)n;
    }

    rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);

    /* Signed compare, so this stays correct across the tick counter's wrap. */
    const ULONG spent = (ULONG)(tx_time_get() - t0);
    if (spent >= limit) { return BSP_ERROR_TIMEOUT; }

    /* Sleep until the ISR says something happened, idle, half, complete or
     * error, then look again. Nothing is cleared before the wait: a flag
     * left over from a burst we already drained costs one extra pass round
     * this loop, whereas clearing it would open a window in which an arrival
     * between the check above and the wait below is never announced. The cap
     * is belt and braces on top of that. */
    ULONG slice = limit - spent;
    if (slice > (ULONG)(TX_TIMER_TICKS_PER_SECOND / 20U))
    {
      slice = (ULONG)(TX_TIMER_TICKS_PER_SECOND / 20U);   /* 50 ms */
    }
    if (slice == 0U) { slice = 1U; }
    ULONG flags = 0U;
    (void)tx_event_flags_get(&uart[id].rtos.evt_rx,
                             UART_STATUS_RX_CPLT | UART_STATUS_ERROR,
                             TX_OR_CLEAR, &flags, slice);
  }
}

int32_t bsp_uart_read(t_uart_id id, uint8_t* buff, size_t size, uint32_t timeout)
{
  uint32_t flags = 0U;    /* must be zeroed: the EXIT path reads it even when
                           * we jump there before tx_event_flags_get() runs */
  int32_t  status;

  /* Validate */
  if (!buff || (id >= UART_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  if (uart[id].bsp.mode_rx == UART_MODE_RING)
  {
    return _ring_read(id, buff, size, timeout);
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&uart[id].rtos.mtx_rx, true);
  rtos_clear_event(&uart[id].rtos.evt_rx, UART_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean_invalidate((uint32_t*)buff, size);

  /* Execute */
  switch (uart[id].bsp.mode_rx)
  {
    case UART_MODE_DMA: status = HAL_UARTEx_ReceiveToIdle_DMA(&uart[id].bsp.huart, buff, size); break;
    case UART_MODE_IT:  status = HAL_UARTEx_ReceiveToIdle_IT (&uart[id].bsp.huart, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_UARTEx_ReceiveToIdle(&uart[id].bsp.huart, buff, size, (uint16_t*)&_uart_recv[id], timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  status = tx_event_flags_get(&uart[id].rtos.evt_rx, UART_STATUS_RX_CPLT | UART_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
  if ((status == TX_NO_EVENTS) || (flags & UART_STATUS_ERROR))
  {
    /* Abort on timeout AND on error. For a framing/noise error in IT mode the
     * HAL takes its "non-blocking" branch and leaves RxState == BUSY_RX, so
     * without this abort every subsequent HAL_UARTEx_ReceiveToIdle_IT()
     * returns HAL_BUSY and the caller spins while bytes arrive unread. */
    HAL_UART_AbortReceive(&uart[id].bsp.huart);
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&uart[id].rtos.mtx_rx, false);
    if ((status == HAL_ERROR) || (flags & UART_STATUS_ERROR))
    {
      return BSP_ERROR_PERIPHERAL;
    }
    else if ((status == HAL_TIMEOUT) || (status == TX_NO_EVENTS))
    {
      return BSP_ERROR_TIMEOUT;
    }
    else if ((status == HAL_OK) || (status == TX_SUCCESS))
    {
      return (int32_t)_uart_recv[id];
    }
    return BSP_ERROR_UNKNOWN;
  }
}

int32_t bsp_uart_write(t_uart_id id, const uint8_t* buff, size_t size, uint32_t timeout)
{
  uint32_t flags = 0U;    /* see bsp_uart_read(): the EXIT path reads this */
  int32_t  status;

  /* Validate */
  if (!buff || (id >= UART_NUM) || (size == 0))
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!uart[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }

  /* Acquire and clear events */
  rtos_mutex_acquire(&uart[id].rtos.mtx_tx, true);
  rtos_clear_event(&uart[id].rtos.evt_tx, UART_STATUS_ALL);

  /* Handle cache */
  bsp_mcu_cache_clean((uint32_t*)buff, size);

  /* Execute */
  switch (uart[id].bsp.mode_tx)
  {
    case UART_MODE_DMA: status = HAL_UART_Transmit_DMA(&uart[id].bsp.huart, buff, size); break;
    case UART_MODE_IT:  status = HAL_UART_Transmit_IT (&uart[id].bsp.huart, buff, size); break;
    default:
      /* Blocking mode, go directly to exit */
      status = HAL_UART_Transmit(&uart[id].bsp.huart, buff, size, timeout);
      goto EXIT;
  }

  /* Handle start */
  if (status != HAL_OK)
  {
    goto EXIT;
  }

  /* Wait for event */
  status = tx_event_flags_get(&uart[id].rtos.evt_tx, UART_STATUS_TX_CPLT | UART_STATUS_ERROR, TX_OR_CLEAR, &flags, timeout);
  if (status == TX_NO_EVENTS)
  {
    /* On timeout, abort the TRANSMITTER. This used to call
     * HAL_UART_AbortReceive(), which tore down the receiver on every TX
     * timeout -- so a slow far end killed the RX path as a side effect. */
    HAL_UART_AbortTransmit(&uart[id].bsp.huart);
  }

  /* Handle exit */
  EXIT:
  {
    /* Release and return */
    rtos_mutex_acquire(&uart[id].rtos.mtx_tx, false);
    if ((status == HAL_ERROR) || (flags & UART_STATUS_ERROR))
    {
      return BSP_ERROR_PERIPHERAL;
    }
    else if ((status == HAL_TIMEOUT) || (status == TX_NO_EVENTS))
    {
      return BSP_ERROR_TIMEOUT;
    }
    else if ((status == HAL_OK) || (status == TX_SUCCESS))
    {
      return size;
    }
    return BSP_ERROR_UNKNOWN;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

/*-->> Internal <<-------------------*/
static int32_t _bsp_uart_init_gpio(t_uart_id id)
{
  GPIO_InitTypeDef gpio = {
    .Mode   = GPIO_MODE_AF_PP,
    .Pull   = GPIO_PULLUP,
    .Speed  = GPIO_SPEED_FREQ_MEDIUM,
  };

  /* Configure */
  switch (id)
  {
    case UART_1:
      /* Start clocks */
      USART1_RX_CLK_ENABLE();
      USART1_TX_CLK_ENABLE();

      /* Set specifics */
      gpio.Pin       = USART1_RX_PIN;
      gpio.Alternate = USART1_RX_AF;
      HAL_GPIO_Init(USART1_RX_PORT, &gpio);

      gpio.Pin       = USART1_TX_PIN;
      gpio.Alternate = USART1_TX_AF;
      HAL_GPIO_Init(USART1_TX_PORT, &gpio);
      break;

    case UART_2:
      /* Start clocks */
      USART2_RX_CLK_ENABLE();
      USART2_TX_CLK_ENABLE();

      /* No internal pull on the USART2 (MangOH modem) lines. This link runs
       * through the mangOH Yellow CN805 FXMA108 auto-direction level shifter,
       * which requires any resistor on its data lines to be >50k; the STM32's
       * ~40k internal pull-up disturbs its direction/one-shot circuitry and
       * kills the modem->camera RX path. UART1 keeps its default pull. */
      gpio.Pull      = GPIO_NOPULL;

      /* Set specifics */
      gpio.Pin       = USART2_RX_PIN;
      gpio.Alternate = USART2_RX_AF;
      HAL_GPIO_Init(USART2_RX_PORT, &gpio);

      gpio.Pin       = USART2_TX_PIN;
      gpio.Alternate = USART2_TX_AF;
      HAL_GPIO_Init(USART2_TX_PORT, &gpio);
      break;

    default:
      return BSP_ERROR_PARAMETER;
  }
  return BSP_OK;
}

static int32_t _bsp_uart_init_peripheral(t_uart_id id, uint32_t baud, bool swap)
{
  int32_t     status;
  t_uart_bsp  *bsp;

  /* Get instance */
  bsp = &uart[id].bsp;

  /* Set defaults */
  /* > Peripheral */
  bsp->huart.Init.BaudRate                = baud;
  bsp->huart.Init.Mode                    = UART_MODE_TX_RX;
  bsp->huart.Init.Parity                  = UART_PARITY_NONE;
  bsp->huart.Init.WordLength              = UART_WORDLENGTH_8B;
  bsp->huart.Init.StopBits                = UART_STOPBITS_1;
  bsp->huart.Init.HwFlowCtl               = UART_HWCONTROL_NONE;
  bsp->huart.Init.OverSampling            = UART_OVERSAMPLING_8;

  /* > Advanced options */
  if (swap)
  {
    bsp->huart.AdvancedInit.AdvFeatureInit|= UART_ADVFEATURE_SWAP_INIT;
    bsp->huart.AdvancedInit.Swap           = UART_ADVFEATURE_SWAP_ENABLE;
  }

  /* > DMA - RX */
  bsp->hdmarx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmarx.Init.Direction              = DMA_PERIPH_TO_MEMORY;
  bsp->hdmarx.Init.SrcInc                 = DMA_SINC_FIXED;
  bsp->hdmarx.Init.DestInc                = DMA_DINC_INCREMENTED;
  bsp->hdmarx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmarx.Init.Priority               = DMA_HIGH_PRIORITY;
  bsp->hdmarx.Init.SrcBurstLength         = 1;
  bsp->hdmarx.Init.DestBurstLength        = 1;
  bsp->hdmarx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1);
  bsp->hdmarx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmarx.Init.Mode                   = DMA_NORMAL;

  /* > DMA -TX */
  bsp->hdmatx.Init.BlkHWRequest           = DMA_BREQ_SINGLE_BURST;
  bsp->hdmatx.Init.Direction              = DMA_MEMORY_TO_PERIPH;
  bsp->hdmatx.Init.SrcInc                 = DMA_SINC_INCREMENTED;
  bsp->hdmatx.Init.DestInc                = DMA_DINC_FIXED;
  bsp->hdmatx.Init.SrcDataWidth           = DMA_SRC_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.DestDataWidth          = DMA_DEST_DATAWIDTH_BYTE;
  bsp->hdmatx.Init.Priority               = DMA_HIGH_PRIORITY;
  bsp->hdmatx.Init.SrcBurstLength         = 1;
  bsp->hdmatx.Init.DestBurstLength        = 1;
  bsp->hdmatx.Init.TransferAllocatedPort  = (DMA_SRC_ALLOCATED_PORT0 | DMA_DEST_ALLOCATED_PORT1);
  bsp->hdmatx.Init.TransferEventMode      = DMA_TCEM_BLOCK_TRANSFER;
  bsp->hdmatx.Init.Mode                   = DMA_NORMAL;

  /* Configure */
  switch (id)
  {
    case UART_1:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_USART1_CLK_ENABLE();
      __HAL_RCC_USART1_FORCE_RESET();
      __HAL_RCC_USART1_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel0_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel0_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel1_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel1_IRQn);
      HAL_NVIC_SetPriority(USART1_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (USART1_IRQn);

      /* Set specifics */
      bsp->huart.Instance       = USART1;
      bsp->hdmarx.Instance      = GPDMA1_Channel0;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_USART1_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel1;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_USART1_TX;
      break;

    case UART_2:
      /* Start clocks */
      __HAL_RCC_GPDMA1_CLK_ENABLE();
      __HAL_RCC_USART2_CLK_ENABLE();
      __HAL_RCC_USART2_FORCE_RESET();
      __HAL_RCC_USART2_RELEASE_RESET();

      /* Start NVIC */
      HAL_NVIC_SetPriority(GPDMA1_Channel2_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel2_IRQn);
      HAL_NVIC_SetPriority(GPDMA1_Channel3_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (GPDMA1_Channel3_IRQn);
      HAL_NVIC_SetPriority(USART2_IRQn, UART_IT_PRIO, 0U);
      HAL_NVIC_EnableIRQ  (USART2_IRQn);

      /* Set specifics */
      bsp->huart.Instance       = USART2;
      bsp->hdmarx.Instance      = GPDMA1_Channel2;
      bsp->hdmarx.Init.Request  = GPDMA1_REQUEST_USART2_RX;
      bsp->hdmatx.Instance      = GPDMA1_Channel3;
      bsp->hdmatx.Init.Request  = GPDMA1_REQUEST_USART2_TX;
      break;

    default:
      return BSP_ERROR_NOT_SUPPORTED;
  }

  /* Init */
  /* > DMA - RX */
  status = HAL_DMA_Init(&bsp->hdmarx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->huart, hdmarx, bsp->hdmarx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmarx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* DMA -TX */
  status = HAL_DMA_Init(&bsp->hdmatx);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  __HAL_LINKDMA(&bsp->huart, hdmatx, bsp->hdmatx);
  status = HAL_DMA_ConfigChannelAttributes(&bsp->hdmatx, (DMA_CHANNEL_PRIV | DMA_CHANNEL_SEC | DMA_CHANNEL_SRC_SEC | DMA_CHANNEL_DEST_SEC));
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* > Peripheral */
  status = HAL_UART_Init(&bsp->huart);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  /* The 16-byte hardware FIFOs, ON (ScopusQA #23).
   *
   * They used to be configured and then switched off one line later:
   * SetTxFifoThreshold / SetRxFifoThreshold followed by
   * HAL_UARTEx_DisableFifoMode(), which made both thresholds inert and left
   * the peripheral moving one character per interrupt or DMA request. At
   * 115200 that is comfortable and it costs nothing measurable, what it
   * costs is margin, and margin is exactly what the modem link needs: its
   * ORE/FE/NE counter is not always zero, and a URC is not re-sent.
   *
   * Order matters, and not for the obvious reason: all three of these
   * functions call UARTEx_SetNbDataToProcess(), which decides how many
   * characters an ISR moves per entry and returns 1 unless huart->FifoMode
   * is already ENABLE. Enable first, thresholds after, or the thresholds are
   * computed against a FIFO the handle still thinks is off. */
  status = HAL_UARTEx_EnableFifoMode(&bsp->huart);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_UARTEx_SetTxFifoThreshold(&bsp->huart, UART_TX_FIFO_THRESH);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  status = HAL_UARTEx_SetRxFifoThreshold(&bsp->huart, UART_RX_FIFO_THRESH);
  if (status != HAL_OK)
  {
    return BSP_ERROR_PERIPHERAL;
  }
  return BSP_OK;
}

/*-->> Stream <<---------------------*/
static int32_t _bsp_uart1_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_read(UART_1, buff, size, timeout);
}

static int32_t _bsp_uart1_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_write(UART_1, buff, size, timeout);
}

static int32_t _bsp_uart2_read(uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_read(UART_2, buff, size, timeout);
}

static int32_t _bsp_uart2_write(const uint8_t *buff, size_t size, uint32_t timeout)
{
  return bsp_uart_write(UART_2, buff, size, timeout);
}

/*-->> BSP handling <<---------------*/
void GPDMA1_Channel0_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_1].bsp.hdmarx);
}

void GPDMA1_Channel1_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_1].bsp.hdmatx);
}

void USART1_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart[UART_1].bsp.huart);
}

void GPDMA1_Channel2_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_2].bsp.hdmarx);
}

void GPDMA1_Channel3_IRQHandler(void)
{
  HAL_DMA_IRQHandler(&uart[UART_2].bsp.hdmatx);
}

void USART2_IRQHandler(void)
{
  HAL_UART_IRQHandler(&uart[UART_2].bsp.huart);
}

void HAL_UARTEx_RxEventCallback(UART_HandleTypeDef *huart, uint16_t size)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal */
    if (uart[idx].bsp.mode_rx == UART_MODE_RING)
    {
      /* Circular mode: this fires on idle, half and complete, and `size` is
       * a position in the ring rather than a length for a caller. Fold the
       * DMA's advance in from here as well as from readers, that is what
       * keeps the monotonic total exact when nobody is reading, which is
       * precisely the case the ring exists for. */
      _ring_sample((t_uart_id)idx);
    }
    else
    {
      _uart_recv[idx] = size;
    }
    rtos_raise_event(&uart[idx].rtos.evt_rx, UART_STATUS_RX_CPLT);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_RX_CPLT);
    }
    return;
  }
}

void HAL_UART_TxCpltCallback(UART_HandleTypeDef *huart)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal */
    rtos_raise_event(&uart[idx].rtos.evt_tx, UART_STATUS_TX_CPLT);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_TX_CPLT);
    }
    return;
  }
}

void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart)
{
  /* Find instance */
  for (uint8_t idx = 0U; idx < UART_NUM; idx++)
  {
    /* Validate */
    if (!uart[idx].ready || (&uart[idx].bsp.huart != huart))
    {
      continue;
    }
    /* Handle internal. An error can strand a waiter on either side, so raise
     * it on both groups -- whichever direction is blocked needs to see it. */
    _uart_errors[idx]++;
    rtos_raise_event(&uart[idx].rtos.evt_rx, UART_STATUS_ERROR);
    rtos_raise_event(&uart[idx].rtos.evt_tx, UART_STATUS_ERROR);
    if (uart[idx].bsp.irq_cb)
    {
      uart[idx].bsp.irq_cb(idx, UART_STATUS_ERROR);
    }
    return;
  }
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: UART -->
*//*--------------------------------------------------------------------------*/
