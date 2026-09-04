/**
 *******************************************************************************
 * @file    n6cam_uart.h
 * @author  SIANA Systems
 * @date    2025
 * @brief   N6Cam UART API
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
#ifndef _N6CAM_UART_H_
#define _N6CAM_UART_H_
#ifdef __cplusplus
extern "C" {
#endif

#include "n6cam_rtos.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup UART
* @{
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Macros
* @{
*//*--------------------------------------------------------------------------*/

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_Types
* @{
*//*--------------------------------------------------------------------------*/

/** Available UARTs */
typedef enum
{
  /* Peripherals */
  UART_1 = 0x00U,
  UART_2,
  UART_NUM,
  /* Alias */
  UART_STLINK = UART_1,
#if defined(N6CAM_WIFI_MURATA)
  UART_WIFI   = UART_2,
#else
  UART_APP    = UART_2,
#endif /* WIFI */
} t_uart_id;

/** UART status */
typedef enum
{
  UART_STATUS_RX_CPLT = BIT(0U),
  UART_STATUS_TX_CPLT = BIT(1U),
  UART_STATUS_ERROR   = BIT(2U),
  UART_STATUS_ALL     = BITMASK(3U),
} t_uart_status;

/** UART modes
 *
 * UART_MODE_RING is receive-only and is what the modem link runs (ScopusQA
 * #23). The other three arm a transfer per call, so nothing is receiving
 * between calls; RING keeps the peripheral receiving continuously into a
 * driver-owned ring and bsp_uart_read() copies out of it. See the note on
 * bsp_uart_ring_stats() for why that matters.
 */
typedef enum
{
  UART_MODE_BLOCK = 0x00U,
  UART_MODE_IT,
  UART_MODE_DMA,
  UART_MODE_RING,
} t_uart_mode;

/**
 * @brief UART IRQ callback
 * @param id      UART ID
 * @param status  Status flag
 */
typedef void (*t_uart_irq_cb)(t_uart_id id, t_uart_status status);

/** UART BSP instance */
typedef struct
{
  UART_HandleTypeDef    huart;
  DMA_HandleTypeDef     hdmarx;
  DMA_HandleTypeDef     hdmatx;
  t_uart_mode           mode_rx;
  t_uart_mode           mode_tx;
  t_uart_irq_cb         irq_cb;
} t_uart_bsp;

/** UART RTOS instance */
typedef struct
{
  /* RX and TX own SEPARATE event-flag groups on purpose. They used to share
   * one, and since both bsp_uart_read() and bsp_uart_write() clear
   * UART_STATUS_ALL on entry, a transmit from one task would wipe the
   * RX-complete/error flags out from under a concurrent reader in another
   * task -- silently losing the burst that carried the reply. The mtx_rx /
   * mtx_tx mutexes do not serialise the two directions against each other. */
  TX_EVENT_FLAGS_GROUP  evt_rx;
  TX_EVENT_FLAGS_GROUP  evt_tx;
  TX_MUTEX              mtx_rx;
  TX_MUTEX              mtx_tx;
} t_uart_rtos;

/** UART instance */
typedef struct
{
  bool                  ready;
  t_uart_bsp            bsp;
  t_uart_rtos           rtos;
  t_stream              stream;
} t_uart;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_DATA
* @{
*//*--------------------------------------------------------------------------*/

/** UARTs */
extern t_uart uart[UART_NUM];

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_DATA -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

/**
 * @brief Initialize UART
 * @param id    UART ID
 * @param baud  Baudrate in bps
 * @param swap  If true, swap RX/TX pins
 * @return Error code
 */
int32_t bsp_uart_init(t_uart_id id, uint32_t baud, bool swap);

/**
 * @brief Reconfigure an already-initialised UART (GPIO + peripheral).
 *
 *        bsp_uart_init() returns BSP_OK immediately when the UART is already
 *        up, so it cannot be used to change the line rate or to bounce the
 *        pins — it silently does nothing. Use this when the peripheral must
 *        genuinely be torn down and brought back, which is what recovers the
 *        CN805 translator's direction latch on the modem link.
 *
 *        RTOS objects created by bsp_uart_init() are left alone; only the
 *        hardware is redone. Falls back to a full init if the UART was never
 *        brought up.
 *
 * @param  id    UART to reconfigure.
 * @param  baud  Line rate.
 * @param  swap  Swap TX/RX pins.
 * @return BSP_OK on success.
 */
int32_t bsp_uart_reinit(t_uart_id id, uint32_t baud, bool swap);

/**
 * @brief Configure UART RX/TX modes
 * @param id      UART ID
 * @param mode_rx RX mode
 * @param mode_tx TX mode
 * @return Error code
 */
int32_t bsp_uart_set_mode(t_uart_id id, t_uart_mode mode_rx, t_uart_mode mode_tx);

/**
 * @brief Get the UART stream
 * @param id      UART ID
 * @return Stream instance
 */
t_stream *bsp_uart_get_stream(t_uart_id id);

/**
 * @brief Get the cumulative peripheral error count (ORE/FE/NE) for a UART
 * @param id      UART ID
 * @return Error count since boot, or 0 for an invalid id
 */
uint32_t bsp_uart_get_errors(t_uart_id id);

/**
 * @brief Get the UART's kernel (source) clock frequency in Hz
 * @param id      UART ID
 * @return Frequency in Hz, or 0 for an invalid id
 */
uint32_t bsp_uart_get_kernel_clock(t_uart_id id);

/**
 * @brief Get the line rate the UART is ACTUALLY running at, derived from the
 *        live BRR and kernel clock.
 *
 *        Differs from Init.BaudRate whenever the kernel clock changed after
 *        HAL_UART_Init() computed BRR. A TX->RX loopback cannot detect that
 *        condition, since both directions share the same wrong divisor.
 *
 * @param id      UART ID
 * @return Actual baud rate, or 0 if unknown/uninitialised
 */
uint32_t bsp_uart_get_actual_baud(t_uart_id id);

/**
 * @brief Read from UART
 * @param id      UART ID
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Bytes read or error code
 */
int32_t bsp_uart_read(t_uart_id id, uint8_t* buff, size_t size, uint32_t timeout);

/**
 * @brief Write to UART
 * @param id      UART ID
 * @param buff    Data buffer
 * @param size    Data size in bytes
 * @param timeout Timeout in ms
 * @return Bytes written or error code
 */
int32_t bsp_uart_write(t_uart_id id, const uint8_t* buff, size_t size, uint32_t timeout);

/**
 * @brief Put a UART's receiver into continuous ring mode (ScopusQA #23).
 *
 * The other three RX modes are demand-driven: bsp_uart_read() arms a transfer
 * into the CALLER's buffer and disarms it when the call returns, so nothing is
 * receiving between calls and anything that arrives in that window has nowhere
 * to go. With the FIFO off as well, the tolerance was one character, 87 us at
 * 115200.
 *
 * That gap is survivable today only because the modem link is request/response
 * and the layer above re-sends what it does not get an answer to. Two things
 * make it a real risk rather than a theoretical one, both of which ITP raised:
 *
 *   - A URC (`+SDVRNET: UP`, `+SDVRRDY`, an incoming command) is not a reply
 *     to anything. Nobody re-sends it, so a lost one is lost.
 *   - The moment the CPU is allowed to idle with the camera off, the window
 *     between reads stops being microseconds.
 *
 * In ring mode the GPDMA runs a single-node CIRCULAR linked list into a
 * driver-owned buffer and never stops. The hardware FIFO absorbs bursts ahead
 * of it. bsp_uart_read() then copies out what has arrived rather than arming
 * anything, so there is no window at all: reception continues while the caller
 * is parsing, sleeping, or not there.
 *
 * TX is unaffected, it stays whatever mode_tx says.
 *
 * @param id  UART to switch. The ring is per-UART and statically allocated.
 * @return BSP_OK, or an error if the UART is not up or the DMA refuses.
 */
/** Which step of bsp_uart_ring_start() failed. Distinct codes because the
 *  call is a seven-step HAL sequence and "peripheral error" names none of
 *  them; the modem task prints whichever comes back. */
#define BSP_UART_RING_ERR_LIST_INIT   (-101)
#define BSP_UART_RING_ERR_ATTRS       (-102)
#define BSP_UART_RING_ERR_BUILD       (-103)
#define BSP_UART_RING_ERR_INSERT      (-104)
#define BSP_UART_RING_ERR_CIRCULAR    (-105)
#define BSP_UART_RING_ERR_LINK        (-106)
#define BSP_UART_RING_ERR_ARM         (-107)

int32_t bsp_uart_ring_start(t_uart_id id);

/**
 * @brief Ring-mode counters, for proving the link is not losing bytes.
 *
 * @param id       UART ID.
 * @param bytes    Bytes handed to callers since boot.
 * @param lost     Bytes the DMA overwrote before a reader took them. This is
 *                 the number that must stay 0; anything else means the ring is
 *                 too small or nobody is reading.
 * @param peak     Deepest the ring has ever been, in bytes. Sizing evidence.
 * @param capacity Ring size in bytes.
 *
 * Any pointer may be NULL. Silently does nothing for an invalid id.
 */
void bsp_uart_ring_stats(t_uart_id id, uint32_t *bytes, uint32_t *lost,
                         uint32_t *peak, uint32_t *capacity);

/**
 * @brief What bsp_uart_ring_start() returned last time, and how many times the
 *        ring has been re-armed after an RX error.
 *
 * The start code is kept because the fallback is silent by design, the link
 * still works in interrupt mode, so without this a unit that quietly lost its
 * ring looks exactly like one that has it. `restarts` climbing means the line
 * is throwing ORE/FE/NE often enough to abort the DMA, which is a wiring or
 * noise problem, not a software one.
 */
void bsp_uart_ring_health(t_uart_id id, int32_t *start_rc, uint32_t *restarts);

/**
 * @brief Zero the ring's byte, loss, peak and restart counters.
 *
 * Not the start code: that describes the ring's configuration and survives.
 * Called by `mdm stats reset`, so a measurement run can be scoped to itself
 * rather than to the life of the unit.
 */
void bsp_uart_ring_reset_stats(t_uart_id id);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: UART -->
*//*--------------------------------------------------------------------------*/
#ifdef  __cplusplus
}
#endif
#endif /* _N6CAM_UART_H_ */
