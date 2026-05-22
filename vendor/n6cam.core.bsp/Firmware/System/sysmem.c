/**
 *******************************************************************************
 * @file    sysmem.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   System memory calls
 *
 * @verbatim
 * ############################################################################
 * #  .data  #  .bss  #       newlib heap       #   MSP stack (_min_stack)    #
 * ############################################################################
 * ^-- RAM start      ^-- _end                             _estack, RAM end --^
 * @endverbatim
 *
 *******************************************************************************
 * <h2><center>© COPYRIGHT 2025 SIANA Systems</center></h2>
 *******************************************************************************
 */
#include <errno.h>
#include <inttypes.h>

#include "common.h"

/* Private tunables ----------------------------------------------------------*/

#define MALLOC_WRAP_PRINT   0U

/* Private definitions -------------------------------------------------------*/

/* Private macros ------------------------------------------------------------*/

/* Private types -------------------------------------------------------------*/

/* Private data --------------------------------------------------------------*/

extern uint8_t  _end;               /*!< Linker - End of the .bss section */
extern uint8_t  _estack;            /*!< Linker - End of the RAM (MSP stack) */
extern uint32_t _min_stack;         /*!< Linker - Minimum stack size (MSP) */

static uint8_t  *__sbrk_heap_end = NULL;  /*!< Current high watermark of the heap usage */

/* Private function ----------------------------------------------------------*/

extern void *__real_malloc(size_t size);

/* Public API definitions ----------------------------------------------------*/

/**
 * @brief Allocates memory from the heap
 * @param size Memory size
 * @return Pointer to the allocated memory
 */
void *_sbrk(ptrdiff_t size)
{
  const uint32_t  stack_limit = (uint32_t)&_estack - (uint32_t)&_min_stack;
  const uint8_t   *heap_max   = (const uint8_t*)stack_limit;
  uint8_t         *heap_prev_end;

  /* Initialize heap end */
  if (__sbrk_heap_end == NULL)
  {
    __sbrk_heap_end = &_end;
  }

  /* Protect heap from growing into the reserved MSP stack */
  if ((__sbrk_heap_end + size) > heap_max)
  {
    errno = ENOMEM;
    return (void *)-1;
  }

  /* Move heap end pointer */
  heap_prev_end   = __sbrk_heap_end;
  __sbrk_heap_end += size;
  return (void *)heap_prev_end;
}

/**
 * @brief Allocates memory from the heap
 * @param size Memory size
 * @return Pointer to the allocated memory
 */
void *__wrap_malloc(size_t size)
{
  /* Allocate memory from the heap */
  void *ptr = __real_malloc(size);
  if (MALLOC_WRAP_PRINT)
  {
    LDEBUG(TRACE_SYSTEM, "[MALLOC] %p:%p in %p:%p (%u bytes)", ptr, ptr + size, &_end, &_estack, size);
  }

  /* Check allocation */
  if (
    (ptr == NULL) ||                    /* Allocation failed */
    (ptr <= (void*)&_end) ||            /* Pointer in .bss section */
    ((ptr + size) >= (void*)&_estack)   /* Allocation exceeds RAM */
  )
  {
    Error_Handler();
  }
  return ptr;
}

/* Private function definitions ----------------------------------------------*/
