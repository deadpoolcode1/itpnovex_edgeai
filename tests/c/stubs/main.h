/* Host stub for the firmware's main.h, see tests/test_tile_rotate.py.
 *
 * tile_detect.c wants three things from it: the two PSRAM section attributes,
 * the alignment pair, and the cache maintenance call. On a PC all four are
 * nothing at all, which is exactly what makes the module testable here: the
 * arithmetic under test never touches a register.
 */
#ifndef MAIN_H_STUB
#define MAIN_H_STUB

#include <stdint.h>

#define IN_PSRAM
#define IN_PSRAM_HI
#define __ALIGN_BEGIN
#define __ALIGN_END

static inline void SCB_InvalidateDCache_by_Addr(uint32_t *addr, int32_t size)
{
  (void)addr;
  (void)size;
}

/* The firmware's millisecond tick. The test drives it by hand, see
 * test_tile_rotate.c, so an expiry can be checked without waiting for one. */
extern uint32_t host_tick_ms;
static inline uint32_t HAL_GetTick(void) { return host_tick_ms; }

#endif /* MAIN_H_STUB */
