/**
 ******************************************************************************
 * @file    fx_app.h
 * @author  SIANA Systems
 * @date    2024
 * @brief   FileX application definition
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
#ifndef __FX_APP_H__
#define __FX_APP_H__
#ifdef __cplusplus
extern "C" {
#endif

#include "fx_api.h"
#include "tx_app.h"

/*-->> Public Definitions <<--------------------------------------------------*/

/*-->> Public Macros <<-------------------------------------------------------*/

/*-->> Public Types <<--------------------------------------------------------*/

/*-->> Public Data <<---------------------------------------------------------*/

/*-->> Public API <<----------------------------------------------------------*/

/**
 * @brief FileX application initialization
 */
void fx_app_init(void);

/**
 * @brief Writes data to a new file - if file already exists, adds suffix
 * @param path Path to new file
 * @param ext  File extension (e.g. ".txt")
 * @param data Data to write
 * @param size Size of the data
 * @retval Error code
 */
int32_t fx_app_write_file(char *path, char *ext, uint8_t *data, size_t size);

/**
 * @brief Writes data to a file with the exact filename given (no auto-suffix).
 *        Used when the caller already has a uniqueness scheme (e.g. SoW §7
 *        timestamped photo filenames). Fails with FX_ALREADY_CREATED if the
 *        file already exists.
 */
int32_t fx_app_write_file_exact(const char *filename, uint8_t *data, size_t size);

/**
 * @brief List filenames in the root directory. Concatenates names with newlines
 *        into the caller's buffer. Returns FX_SUCCESS or an FX_ error code.
 *        Truncates if buffer is too small (terminates with '\0').
 */
int32_t fx_app_list_root(char *out, size_t out_size);

/**
 * @brief Status check for callers that want to know if the media is mounted.
 */
bool    fx_app_is_open(void);

/**
 * @brief Re-format the SD card as FAT32 and re-mount. DESTROYS ALL DATA.
 *        Caller must pre-confirm with the user — the shell does this by
 *        requiring an explicit 'CONFIRM' token. Returns FX_SUCCESS or
 *        an FX_ error code.
 */
int32_t fx_app_format(void);

/**
 * @brief Read up to max_size bytes from filename into buf. *out_size
 *        receives the actual byte count. Returns FX_SUCCESS or an FX_
 *        error code (FX_NOT_FOUND, FX_INVALID_NAME, etc.).
 */
int32_t fx_app_read_file(const char *filename, uint8_t *buf,
                         size_t max_size, size_t *out_size);

/* -------------------------------------------------------------------------- */
#ifdef __cplusplus
}
#endif
#endif /* __FX_APP_H__ */
