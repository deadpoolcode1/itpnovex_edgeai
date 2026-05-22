/**
 *******************************************************************************
 * @file    n6cam_core.c
 * @author  SIANA Systems
 * @date    2025
 * @brief   Implements N6Cam CORE API
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
#include "common.h"
#include "n6cam_core.h"

/*-------------------------------------------------------------------------*//**
* @addtogroup SIANA
* @{
* @addtogroup BSP
* @{
* @addtogroup CORE
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

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_TUNABLES -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Definitions
* @{
*//*--------------------------------------------------------------------------*/

/** Default IO IT priority */
#define IO_IT_PRIO_DEFAULT    10U

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Definitions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Macros
* @{
*//*--------------------------------------------------------------------------*/

#define BTN_IO(id)    ((id) + IO_BTN_USER1)
#define LED_IO(id)    ((id) + IO_LED_USER1)
#if defined(N6CAM)
#define HW_ID_IO(id)  ((id) + IO_HW_ID1)
#endif /* BOARD */

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Macros -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Types
* @{
*//*--------------------------------------------------------------------------*/

/** IO IT function type */
typedef void (*t_io_it_fn)(void);

#if defined(N6CAM)
/** Available HW IDs */
typedef enum
{
  HW_ID_START = 0U,
  HW_ID1      = HW_ID_START,
  HW_ID2,
  HW_ID3,
  HW_ID_NUM,
} t_hw_id;
#endif /* BOARD */

/** IO BSP configuration */
typedef struct
{
  GPIO_TypeDef  *port;
  uint32_t      pin;
  uint32_t      mode;
  uint32_t      speed;
  uint32_t      pull;
  uint32_t      polarity;
} t_io_bsp;

/** IO IT configuration */
typedef struct
{
  uint32_t      prio;
  IRQn_Type     irq;
  bool          enable;
} t_io_it;

/** IO instance */
typedef struct
{
  t_io_bsp      bsp;
  t_io_it       it;
  bool          ready;
} t_io;

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Types -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _bsp_io_clock_enable(t_io_id id);
static void _bsp_io_power_enable(void);
static void _bsp_io_irq_handler(uint16_t pin, bool rising);

/* BSP handling */
#if defined(N6CAM)
extern void EXTI0_IRQHandler(void);
extern void EXTI4_IRQHandler(void);
extern void EXTI15_IRQHandler(void);
#elif defined(STM32N6DK_C02)
extern void EXTI12_IRQHandler(void);
extern void EXTI13_IRQHandler(void);
#endif /* BOARD */
extern void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin);
extern void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin);

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Data
* @{
*//*--------------------------------------------------------------------------*/

static uint8_t  _hw_id      = 0U;
static uint8_t  _io_powered = 0U;
static t_io     _io[IO_NUM] = {
  [IO_BTN_USER1] = {
    .bsp = {
      .port     = BTN_USER1_PORT,
      .pin      = BTN_USER1_PIN,
      .mode     = GPIO_MODE_IT_RISING_FALLING,
      .pull     = GPIO_NOPULL,
      .speed    = GPIO_SPEED_FREQ_HIGH,
      .polarity = GPIO_PIN_RESET,
    },
    .it  = {
      .enable   = true,
      .prio     = IO_IT_PRIO_DEFAULT,
      .irq      = BTN_USER1_EXTI_IRQn,
    },
  },
  [IO_LED_USER1] = {
    .bsp = {
      .port     = LED_USER1_PORT,
      .pin      = LED_USER1_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_LED_USER2] = {
    .bsp = {
      .port     = LED_USER2_PORT,
      .pin      = LED_USER2_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  #if defined(N6CAM)
  [IO_LED_USER3] = {
    .bsp = {
      .port     = LED_USER3_PORT,
      .pin      = LED_USER3_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_LED_CAMERA] = {
    .bsp = {
      .port     = LED_CAMERA_PORT,
      .pin      = LED_CAMERA_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_LED_POWER] = {
    .bsp = {
      .port     = LED_POWER_PORT,
      .pin      = LED_POWER_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_HW_ID1] = {
    .bsp = {
      .port     = HW_ID1_PORT,
      .pin      = HW_ID1_PIN,
      .mode     = GPIO_MODE_INPUT,
      .pull     = GPIO_PULLUP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_HW_ID2] = {
    .bsp = {
      .port     = HW_ID2_PORT,
      .pin      = HW_ID2_PIN,
      .mode     = GPIO_MODE_INPUT,
      .pull     = GPIO_PULLUP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_HW_ID3] = {
    .bsp = {
      .port     = HW_ID3_PORT,
      .pin      = HW_ID3_PIN,
      .mode     = GPIO_MODE_INPUT,
      .pull     = GPIO_PULLUP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  #endif /* BOARD */
  [IO_PWR_CAMERA_EN] = {
    .bsp = {
      .port     = CAMERA_PWR_EN_PORT,
      .pin      = CAMERA_PWR_EN_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  #if defined(STM32N6DK_C02)
  [IO_PWR_CAMERA_RST] = {
    .bsp = {
      .port     = CAMERA_PWR_RST_PORT,
      .pin      = CAMERA_PWR_RST_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  #endif /* BOARD */
  [IO_PWR_SD_EN] = {
    .bsp = {
      .port     = SDIO2_PWR_EN_PORT,
      .pin      = SDIO2_PWR_EN_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_PWR_SD_SEL] = {
    .bsp = {
      .port     = SDIO2_PWR_SEL_PORT,
      .pin      = SDIO2_PWR_SEL_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_PWR_USB1_EN] = {
    .bsp = {
      .port     = USB1_PWR_EN_PORT,
      .pin      = USB1_PWR_EN_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_SET,
    },
  },
  [IO_SD_DETECT] = {
    .bsp = {
      .port     = SDIO2_DETECT_PORT,
      .pin      = SDIO2_DETECT_PIN,
      .mode     = GPIO_MODE_IT_FALLING,
      .pull     = GPIO_NOPULL,
      .speed    = GPIO_SPEED_FREQ_HIGH,
      .polarity = GPIO_PIN_RESET,
    },
    .it  = {
      .enable   = true,
      .prio     = IO_IT_PRIO_DEFAULT,
      .irq      = SDIO2_DETECT_EXTI_IRQn,
    },
  },
  #if defined(N6CAM)
  [IO_SPI3_CS1] = {
    .bsp = {
      .port     = SPI3_CS1_PORT,
      .pin      = SPI3_CS1_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_SPI4_CS1] = {
    .bsp = {
      .port     = SPI4_CS1_PORT,
      .pin      = SPI4_CS1_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_SPI5_CS1] = {
    .bsp = {
      .port     = SPI5_CS1_PORT,
      .pin      = SPI5_CS1_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_SPI5_CS2] = {
    .bsp = {
      .port     = SPI5_CS2_PORT,
      .pin      = SPI5_CS2_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_SPI5_CS3] = {
    .bsp = {
      .port     = SPI5_CS3_PORT,
      .pin      = SPI5_CS3_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  [IO_SPI5_CS4] = {
    .bsp = {
      .port     = SPI5_CS4_PORT,
      .pin      = SPI5_CS4_PIN,
      .mode     = GPIO_MODE_OUTPUT_PP,
      .polarity = GPIO_PIN_RESET,
    },
  },
  #endif /* BOARD */
};

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Data -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PUBLIC_API
* @{
*//*--------------------------------------------------------------------------*/

int32_t bsp_io_init(t_io_id id)
{
  /* Validate */
  if (id >= IO_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (_io[id].ready)
  {
    return BSP_OK;
  }

  /* Enable power */
  _bsp_io_power_enable();

  /* Enable clock */
  _bsp_io_clock_enable(id);

  /* Init GPIO */
  GPIO_InitTypeDef gpio = {
    .Pin    = _io[id].bsp.pin,
    .Mode   = _io[id].bsp.mode,
    .Pull   = _io[id].bsp.pull,
    .Speed  = _io[id].bsp.speed
  };
  HAL_GPIO_Init(_io[id].bsp.port, &gpio);

  /* Configure EXTI if needed */
  if (_io[id].it.enable)
  {
    HAL_NVIC_SetPriority(_io[id].it.irq, _io[id].it.prio, 0);
    HAL_NVIC_EnableIRQ(_io[id].it.irq);
  }
  _io[id].ready = true;
  return BSP_OK;
}

int32_t bsp_io_deinit(t_io_id id)
{
  /* Validate */
  if (id >= IO_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!_io[id].ready)
  {
    return BSP_OK;
  }

  /* Disable IT and deinit GPIO */
  if (_io[id].it.enable)
  {
    HAL_NVIC_DisableIRQ(_io[id].it.irq);
  }
  HAL_GPIO_DeInit(_io[id].bsp.port, _io[id].bsp.pin);
  _io[id].ready = false;
  return BSP_OK;
}

int32_t bsp_io_toggle_state(t_io_id id)
{
  /* Validate */
  if (id >= IO_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!_io[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }
  if (
    (_io[id].bsp.mode != GPIO_MODE_OUTPUT_OD) &&
    (_io[id].bsp.mode != GPIO_MODE_OUTPUT_PP)
  )
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Toggle value */
  HAL_GPIO_TogglePin(_io[id].bsp.port, _io[id].bsp.pin);
  return BSP_OK;
}

int32_t bsp_io_set_state(t_io_id id, bool state)
{
  /* Validate */
  if (id >= IO_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }
  if (!_io[id].ready)
  {
    return BSP_ERROR_NO_INIT;
  }
  if (
    (_io[id].bsp.mode != GPIO_MODE_OUTPUT_OD) &&
    (_io[id].bsp.mode != GPIO_MODE_OUTPUT_PP)
  )
  {
    return BSP_ERROR_PERIPHERAL;
  }

  /* Set value */
  HAL_GPIO_WritePin(_io[id].bsp.port, _io[id].bsp.pin, state == _io[id].bsp.polarity);
  return BSP_OK;
}

bool bsp_io_get_state(t_io_id id)
{
  /* Validate */
  if (id >= IO_NUM)
  {
    return false;
  }
  if (!_io[id].ready)
  {
    return false;
  }

  /* Get value */
  return (HAL_GPIO_ReadPin(_io[id].bsp.port, _io[id].bsp.pin) == _io[id].bsp.polarity);
}

WEAK void bsp_io_irq_handler(t_io_id id, bool rising)
{
  /* For user to implement */
  UNUSED(id);
  UNUSED(rising);
}

WEAK void bsp_io_unknown_irq_handler(uint16_t gpio, bool rising)
{
  /* For user to implement */
  UNUSED(gpio);
  UNUSED(rising);
}

int32_t bsp_btn_init(t_btn_id id)
{
  /* Validate */
  if (id >= BTN_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Init BTN */
  return bsp_io_init(BTN_IO(id));
}

int32_t bsp_btn_deinit(t_btn_id id)
{
  /* Validate */
  if (id >= BTN_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Deinit BTN */
  return bsp_io_deinit(BTN_IO(id));
}

bool bsp_btn_get_state(t_btn_id id)
{
  /* Validate */
  if (id >= BTN_NUM)
  {
    return 0U;
  }

  /* Get value */
  return bsp_io_get_state(BTN_IO(id));
}

WEAK void bsp_btn_irq_handler(t_btn_id id, bool rising)
{
  /* For user to implement */
  UNUSED(id);
  UNUSED(rising);
}

void bsp_hw_id_init(void)
{
  #if defined(N6CAM)
  _hw_id = 0U;
  for (size_t idx = 0U; idx < HW_ID_NUM; idx++)
  {
    uint32_t io = HW_ID_IO(idx);
    if (bsp_io_init(io) == BSP_OK)
    {
      _hw_id |= (uint8_t)(bsp_io_get_state(io) << idx);
      bsp_io_deinit(io);
    }
  }
  #elif defined(STM32N6DK_C02)
  _hw_id = 0xF0U;
  #endif /* BOARD */
}

uint8_t bsp_hw_id_get(void)
{
  return _hw_id;
}

const char *bsp_hw_id_get_string(void)
{
  switch (_hw_id)
  {
    case 0x00U: return "RevA";
    case 0xF0U: return "STM32N6DK_C02";
    default:    return "Unknown";
  }
}

int32_t bsp_led_init(t_led_id id)
{
  /* Validate */
  if (id >= LED_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Init LED */
  uint32_t io = LED_IO(id);
  int32_t  status = bsp_io_init(io);
  if (status == BSP_OK)
  {
    bsp_io_set_state(io, false);
  }
  return status;
}

int32_t bsp_led_deinit(t_led_id id)
{
  /* Validate */
  if (id >= LED_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Deinit LED */
  return bsp_io_deinit(LED_IO(id));
}

int32_t bsp_led_on(t_led_id id)
{
  return bsp_led_set_state(id, true);
}

int32_t bsp_led_off(t_led_id id)
{
  return bsp_led_set_state(id, false);
}

int32_t bsp_led_toggle(t_led_id id)
{
  /* Validate */
  if (id >= LED_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Toggle value */
  return bsp_io_toggle_state(LED_IO(id));
}

int32_t bsp_led_set_state(t_led_id id, bool state)
{
  /* Validate */
  if (id >= LED_NUM)
  {
    return BSP_ERROR_PARAMETER;
  }

  /* Set value */
  return bsp_io_set_state(LED_IO(id), state);
}

bool bsp_led_get_state(t_led_id id)
{
  /* Validate */
  if (id >= LED_NUM)
  {
    return false;
  }

  /* Get value */
  return bsp_io_get_state(LED_IO(id));
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PUBLIC_API -->
*//*-----------------------------------------------------------------------*//**
* @addtogroup PRIVATE_Functions
* @{
*//*--------------------------------------------------------------------------*/

static void _bsp_io_clock_enable(t_io_id id)
{
  switch (id)
  {
    case IO_BTN_USER1     : BTN_USER1_CLK_ENABLE();     break;
    case IO_LED_USER1     : LED_USER1_CLK_ENABLE();     break;
    case IO_LED_USER2     : LED_USER2_CLK_ENABLE();     break;
    #if defined(N6CAM)
    case IO_LED_USER3     : LED_USER3_CLK_ENABLE();     break;
    case IO_LED_CAMERA    : LED_CAMERA_CLK_ENABLE();    break;
    case IO_LED_POWER     : LED_POWER_CLK_ENABLE();     break;
    case IO_HW_ID1        : HW_ID1_CLK_ENABLE();        break;
    case IO_HW_ID2        : HW_ID2_CLK_ENABLE();        break;
    case IO_HW_ID3        : HW_ID3_CLK_ENABLE();        break;
    #endif /* BOARD */
    case IO_PWR_CAMERA_EN : CAMERA_PWR_EN_CLK_ENABLE(); break;
    #if defined(STM32N6DK_C02)
    case IO_PWR_CAMERA_RST: CAMERA_PWR_RST_CLK_ENABLE();break;
    #endif /* BOARD */
    case IO_PWR_SD_EN     : SDIO2_PWR_EN_CLK_ENABLE();  break;
    case IO_PWR_SD_SEL    : SDIO2_PWR_SEL_CLK_ENABLE(); break;
    case IO_PWR_USB1_EN   : USB1_PWR_EN_CLK_ENABLE();   break;
    case IO_SD_DETECT     : SDIO2_DETECT_CLK_ENABLE();  break;
    #if defined(N6CAM)
    case IO_SPI3_CS1      : SPI3_CS1_CLK_ENABLE();      break;
    case IO_SPI4_CS1      : SPI4_CS1_CLK_ENABLE();      break;
    case IO_SPI5_CS1      : SPI5_CS1_CLK_ENABLE();      break;
    case IO_SPI5_CS2      : SPI5_CS2_CLK_ENABLE();      break;
    case IO_SPI5_CS3      : SPI5_CS3_CLK_ENABLE();      break;
    case IO_SPI5_CS4      : SPI5_CS4_CLK_ENABLE();      break;
    #endif /* BOARD */
    default: Error_Handler();
  }
}

static void _bsp_io_power_enable(void)
{
  if (_io_powered == 0U)
  {
    HAL_PWREx_EnableVddA();
    HAL_PWREx_EnableVddIO2();
    HAL_PWREx_EnableVddIO3();
    HAL_PWREx_EnableVddIO4();
    HAL_PWREx_EnableVddIO5();
    _io_powered = 1U;
  }
}

/**
 * @brief BSP IO IRQ handler
 * @param pin     GPIO pin
 * @param rising  True if rising edge, false if falling edge
 */
static void _bsp_io_irq_handler(uint16_t pin, bool rising)
{
  switch (pin)
  {
    case BTN_USER1_PIN:
      rising = _io[IO_BTN_USER1].bsp.polarity == rising;
      bsp_btn_irq_handler(BTN_USER1, rising);
      bsp_io_irq_handler(IO_BTN_USER1, rising);
      break;

    case SDIO2_DETECT_PIN:
      rising = _io[IO_SD_DETECT].bsp.polarity == rising;
      bsp_io_irq_handler(IO_SD_DETECT, rising);
      break;

    default:
      /* Unknown GPIO */
      bsp_io_unknown_irq_handler(pin, rising);
      break;
  }
}

/*-->> BSP handling <<---------------*/
#if defined(N6CAM)
/**
 * @brief This function handles EXTI0 interrupt request.
 * @note  VL53L7 IRQ handler.
 */
void EXTI0_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(VL53L7_INT_PIN);
}

/**
 * @brief This function handles EXTI4 interrupt request.
 * @note  User button IRQ handler.
 */
void EXTI4_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(BTN_USER1_PIN);
}

/**
 * @brief This function handles EXTI15 interrupt request.
 * @note  SD detect IRQ handler.
 */
void EXTI15_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(SDIO2_DETECT_PIN);
}
#elif defined(STM32N6DK_C02)

/**
 * @brief This function handles EXTI12 interrupt request.
 * @note  SD detect IRQ handler.
 */
void EXTI12_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(SDIO2_DETECT_PIN);
}

/**
 * @brief This function handles EXTI13 interrupt request.
 * @note  User button IRQ handler.
 */
void EXTI13_IRQHandler(void)
{
  HAL_GPIO_EXTI_IRQHandler(BTN_USER1_PIN);
}
#endif /* BOARD */

/**
 * @brief EXTI line rising detection callback.
 * @param gpio Specifies the port pin connected to corresponding EXTI line.
 */
void HAL_GPIO_EXTI_Falling_Callback(uint16_t pin)
{
  _bsp_io_irq_handler(pin, false);
}

/**
 * @brief EXTI line falling detection callback.
 * @param gpio Specifies the port pin connected to corresponding EXTI line.
 */
void HAL_GPIO_EXTI_Rising_Callback(uint16_t pin)
{
  _bsp_io_irq_handler(pin, true);
}

/*-------------------------------------------------------------------------*//**
* @} <!-- End: PRIVATE_Functions -->
*//*-----------------------------------------------------------------------*//**
* @} <!-- End: SIANA -->
* @} <!-- End: BSP -->
* @} <!-- End: CORE -->
*//*--------------------------------------------------------------------------*/
