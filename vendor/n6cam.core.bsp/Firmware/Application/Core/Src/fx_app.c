/**
 ******************************************************************************
 * @file    fx_app.c
 * @author  SIANA Systems
 * @date    2024
 * @brief   FileX application implementation
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
#include <stdio.h>

#include "fx_app.h"
#include "fx_user.h"
#include "fx_stm32_config.h"
#include "fx_stm32_sd_driver.h"
#include "n6cam_sdio.h"

/*-->> Private Tunables <<----------------------------------------------------*/

/* Media management */
#define MEDIA_BUFF_SIZE         FX_STM32_SD_DEFAULT_SECTOR_SIZE
#define MEDIA_PATH_MAX_LENGTH   64U

/*-->> Private Definitions <<-------------------------------------------------*/

/* FileX events */
#define FX_EVT_ALL              0xFFFFFFFFU
#define FX_EVT_SD_CONNECTED     BIT(0U)
#define FX_EVT_SD_DISCONNECTED  BIT(1U)

/*-->> Private Macros <<------------------------------------------------------*/

/*-->> Private Types <<-------------------------------------------------------*/

/* FileX task handler */
typedef struct
{
  /* FileX management */
  bool                  open;
  FX_MEDIA              sdio;
  FX_FILE               file;
  size_t                size;
  uint8_t               *media;
  /* Thread handling */
  TX_EVENT_FLAGS_GROUP  evt;
  TX_MUTEX              mtx;
  TX_THREAD             thread;
  uint8_t               stack[FX_STACK_SIZE];
} t_fx_task;

/*-->> Private Data <<--------------------------------------------------------*/

static uint8_t    _fx_task_media[MEDIA_BUFF_SIZE] DMA_ALIGN;
static t_fx_task  _fx_task = {
  .open  = false,
  .media = _fx_task_media,
  .size  = sizeof(_fx_task_media),
};

/*-->> Private Functions <<---------------------------------------------------*/

static void _fx_task_init(void);
static void _fx_task_run(uint32_t input);

extern void bsp_sdio_detect_callback(bool detected);

/*-->> Public API <<----------------------------------------------------------*/

void fx_app_init(void)
{
  int32_t status;

  /* Create the main thread */
  status = tx_thread_create(
    &_fx_task.thread, "fx.task",
    _fx_task_run, 0,
    _fx_task.stack, FX_STACK_SIZE,
    FX_TASK_PRIO, FX_TASK_PRIO,
    FX_TASK_TIME_SLICE, TX_AUTO_START
  );
  if (status != FX_SUCCESS)
  {
    Error_Handler();
  }
}

int32_t fx_app_write_file_exact(const char *filename, uint8_t *data, size_t size)
{
  int32_t status;

  if (!_fx_task.open) return FX_MEDIA_NOT_OPEN;
  if (!filename || !data || (size == 0)) return FX_INVALID_NAME;

  rtos_mutex_acquire(&_fx_task.mtx, true);

  /* Fail if file already exists — we don't overwrite (per fx_app conventions) */
  status = fx_file_open(&_fx_task.sdio, &_fx_task.file, (char*)filename, FX_OPEN_FOR_READ);
  if (status == FX_SUCCESS)
  {
    (void)fx_file_close(&_fx_task.file);
    rtos_mutex_acquire(&_fx_task.mtx, false);
    return FX_ALREADY_CREATED;
  }

  status = fx_file_create(&_fx_task.sdio, (char*)filename);
  if (status == FX_SUCCESS)
  {
    status = fx_file_open(&_fx_task.sdio, &_fx_task.file, (char*)filename, FX_OPEN_FOR_WRITE);
    if (status == FX_SUCCESS)
    {
      status = fx_file_write(&_fx_task.file, (void*)data, size);
      (void)fx_file_close(&_fx_task.file);
    }
  }
  if (status == FX_SUCCESS)
  {
    status = fx_media_flush(&_fx_task.sdio);
  }

  rtos_mutex_acquire(&_fx_task.mtx, false);
  return status;
}

bool fx_app_is_open(void)
{
  return _fx_task.open;
}

int32_t fx_app_list_root(char *out, size_t out_size)
{
  CHAR    entry_name[FX_MAX_LONG_NAME_LEN];
  UINT    attr;
  ULONG   size, year, month, day, hour, minute, second;
  int32_t status;
  size_t  pos = 0U;

  if (!_fx_task.open) return FX_MEDIA_NOT_OPEN;
  if (!out || (out_size < 2U)) return FX_INVALID_NAME;
  out[0] = '\0';

  rtos_mutex_acquire(&_fx_task.mtx, true);

  /* Flush any pending FAT writes before iterating — without this the
   * directory cursor can miss files that were just created (FileX's
   * in-memory directory state lags behind what's been committed to
   * media until the next flush). Observed empirically: after
   * fx_app_write_file_exact returns SUCCESS the file is readable via
   * fx_file_open but invisible to fx_directory_first_full_entry_find
   * until a flush forces FileX to reconcile state. */
  (void)fx_media_flush(&_fx_task.sdio);

  status = fx_directory_first_full_entry_find(
    &_fx_task.sdio, entry_name, &attr, &size,
    &year, &month, &day, &hour, &minute, &second
  );
  while (status == FX_SUCCESS)
  {
    int written = snprintf(out + pos, out_size - pos,
                            "%s %lu\n", entry_name, (unsigned long)size);
    if (written < 0) break;
    if ((size_t)written >= (out_size - pos))
    {
      /* truncated */
      out[out_size - 1U] = '\0';
      break;
    }
    pos += (size_t)written;
    status = fx_directory_next_full_entry_find(
      &_fx_task.sdio, entry_name, &attr, &size,
      &year, &month, &day, &hour, &minute, &second
    );
  }

  rtos_mutex_acquire(&_fx_task.mtx, false);
  return (status == FX_NO_MORE_ENTRIES) ? FX_SUCCESS : status;
}

int32_t fx_app_format(void)
{
  int32_t status;

  if (!_fx_task.open) return FX_MEDIA_NOT_OPEN;

  rtos_mutex_acquire(&_fx_task.mtx, true);

  /* Close the open media, format, reopen */
  status = fx_media_close(&_fx_task.sdio);
  _fx_task.open = false;
  if (status == FX_SUCCESS)
  {
    /* fx_media_format with a generic FAT32-friendly geometry. The driver's
     * underlying SDIO layer knows the real disk size; FileX uses these
     * parameters mostly for cluster-size + reserved sector layout. */
    status = fx_media_format(
      &_fx_task.sdio, fx_stm32_sd_driver, NULL,
      _fx_task.media, _fx_task.size,
      "N6CAM",   /* volume name (11 chars max in FAT) */
      1,         /* FATs */
      32,        /* dir entries */
      0,         /* hidden sectors */
      0,         /* total sectors (0 = let driver report) */
      512,       /* bytes per sector */
      8,         /* sectors per cluster */
      1,         /* heads */
      1          /* sectors per track */
    );
  }
  if (status == FX_SUCCESS)
  {
    status = fx_media_open(
      &_fx_task.sdio, FX_SD_VOLUME_NAME,
      fx_stm32_sd_driver, NULL,
      _fx_task.media, _fx_task.size
    );
    if (status == FX_SUCCESS)
    {
      _fx_task.open = true;
    }
  }

  rtos_mutex_acquire(&_fx_task.mtx, false);
  return status;
}

int32_t fx_app_read_file(const char *filename, uint8_t *buf,
                         size_t max_size, size_t *out_size)
{
  int32_t  status;
  ULONG    bytes_read = 0;

  if (out_size) *out_size = 0;
  if (!_fx_task.open) return FX_MEDIA_NOT_OPEN;
  if (!filename || !buf || (max_size == 0)) return FX_INVALID_NAME;

  rtos_mutex_acquire(&_fx_task.mtx, true);

  status = fx_file_open(&_fx_task.sdio, &_fx_task.file,
                        (char*)filename, FX_OPEN_FOR_READ);
  if (status == FX_SUCCESS)
  {
    status = fx_file_read(&_fx_task.file, buf, (ULONG)max_size, &bytes_read);
    (void)fx_file_close(&_fx_task.file);
    if ((status == FX_SUCCESS) || (status == FX_END_OF_FILE))
    {
      if (out_size) *out_size = (size_t)bytes_read;
      /* End-of-file on first read = empty file, still success */
      status = FX_SUCCESS;
    }
  }

  rtos_mutex_acquire(&_fx_task.mtx, false);
  return status;
}

int32_t fx_app_write_file(char *path, char *ext, uint8_t *data, size_t size)
{
  int32_t   status;
  uint16_t  file_idx = 0;
  uint8_t   file_path[MEDIA_PATH_MAX_LENGTH] = { 0 };

  /* Validate */
  if(!_fx_task.open)
  {
    return FX_MEDIA_NOT_OPEN;
  }

  /* Store file path for initial check */
  snprintf((char*)file_path, MEDIA_PATH_MAX_LENGTH, "%s%d.%s", path, file_idx, ext);

  /* Run write logic */
  rtos_mutex_acquire(&_fx_task.mtx, true);

  /* Get available filename */
  do
  {
    status = fx_file_open(&_fx_task.sdio, &_fx_task.file, (char*)file_path, FX_OPEN_FOR_READ);
    if (status == FX_SUCCESS)
    {
      /* File already exists - wont overwrite it */
      status = fx_file_close(&_fx_task.file);
      if (status != FX_SUCCESS)
      {
        rtos_mutex_acquire(&_fx_task.mtx, false);
        return FX_ALREADY_CREATED;
      }

      /* Try next IDX */
      memset(file_path,0x00, sizeof(file_path));
      snprintf((char*)file_path, MEDIA_PATH_MAX_LENGTH, "%s%d.%s", path, ++file_idx, ext);
    }
  } while(status != FX_NOT_FOUND);

  /* Perform write */
  status = fx_file_create(&_fx_task.sdio, (char*)file_path);
  if (status == FX_SUCCESS)
  {
    status = fx_file_open(&_fx_task.sdio, &_fx_task.file, (char*)file_path, FX_OPEN_FOR_WRITE);
    if (status == FX_SUCCESS)
    {
      /* Actual write to file */
      status = fx_file_write(&_fx_task.file, (void*)data, size);

      /* Close the file regardless of result, and report the result to caller*/
      fx_file_close(&_fx_task.file);
    }
  }

  /* IF a file could be correctly created/written, flush into media */
  if (status == FX_SUCCESS)
  {
    status = fx_media_flush(&_fx_task.sdio);
  }

  rtos_mutex_acquire(&_fx_task.mtx, false);
  return status;
}


/*-->> Private Functions <<---------------------------------------------------*/

/**
 * @brief Task initialization.
 */
static void _fx_task_init(void)
{
  int32_t status;

  /*-->> DEPENDENCIES <<--*/
  task_wait_event(TX_EVT_FX_REQUIRE);

  /*-->> INITIALIZE <<--*/
  /* Create mutex */
  status = tx_mutex_create(&_fx_task.mtx, "fx.mtx", TX_INHERIT);
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Create event flags */
  status = tx_event_flags_create(&_fx_task.evt, "fx.evt");
  if (status != TX_SUCCESS)
  {
    Error_Handler();
  }

  /* Initialize FileX */
  fx_system_initialize();

  /*-->> READY <<--*/
  LINFO(TRACE_FX, "Task started");
  task_raise_event(TX_EVT_FX_READY);
}

/**
 * @brief Task loop.
 * @param input
 */
static void _fx_task_run(uint32_t input)
{
  int32_t status;

  UNUSED(input);

  /* Initialize task */
  _fx_task_init();

  /* FX task */
  while (1)
  {
    /* Initialize BSP */
    status = bsp_sdio_init_gpio();
    if (status != BSP_OK)
    {
      LERROR(TRACE_FX, "SDCard GPIO init failed!");
      Error_Handler();
    }

    /* Wait until connected */
    if (!bsp_sdio_is_detected())
    {
      rtos_wait_any_event(&_fx_task.evt, FX_EVT_SD_CONNECTED, true);
      LINFO(TRACE_FX, "SDCard connected!");
    }

    /* Initialize SD */
    status = bsp_sdio_init();
    if (status != BSP_OK)
    {
      LERROR(TRACE_FX, "SDCard init failed!");
      goto DISCONNECT;
    }
    tx_thread_sleep(500);

    /* Initialize FileX: Assume a valid SDCard */
    status = fx_media_open(
      &_fx_task.sdio, FX_SD_VOLUME_NAME,
      fx_stm32_sd_driver, NULL,
      _fx_task.media, _fx_task.size
    );
    if (status != FX_SUCCESS)
    {
      LERROR(TRACE_FX, "FS init failed!");
      goto DISCONNECT;
    }

    /* Connected and working! */
    _fx_task.open = true;

    /* Wait until disconnected to re-attempt */
    rtos_wait_any_event(&_fx_task.evt, FX_EVT_SD_DISCONNECTED, true);
    LINFO(TRACE_FX, "SDCard disconnected!");

    /* Handle reconnection */
    DISCONNECT:
      _fx_task.open = false;
      fx_media_abort(&_fx_task.sdio);
      bsp_sdio_deinit();
      tx_thread_sleep(100);
  }
}

void bsp_sdio_detect_callback(bool detected)
{
  rtos_raise_event(&_fx_task.evt, detected? FX_EVT_SD_CONNECTED : FX_EVT_SD_DISCONNECTED);
}
