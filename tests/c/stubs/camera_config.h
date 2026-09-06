/* Host stub for camera_config.h, see tests/test_tile_rotate.py.
 *
 * The three sizes tile_detect.c reads are NOT written here. The Python runner
 * greps them out of the real header and passes them in with -D, so this stub
 * cannot drift away from the firmware's geometry without the build failing.
 */
#ifndef CAMERA_CONFIG_H_STUB
#define CAMERA_CONFIG_H_STUB

#if !defined(CAMERA_MAIN_WIDTH) || !defined(CAMERA_MAIN_HEIGHT) || \
    !defined(CAMERA_ANCILLARY_WIDTH)
#error "the runner passes the real camera_config.h values in with -D"
#endif

#define CAMERA_ANCILLARY_HEIGHT       CAMERA_ANCILLARY_WIDTH
#define CAMERA_ANCILLARY_BPP          3U
#define CAMERA_ANCILLARY_BUFFER_SIZE  (CAMERA_ANCILLARY_WIDTH * \
                                       CAMERA_ANCILLARY_HEIGHT * \
                                       CAMERA_ANCILLARY_BPP)

#endif /* CAMERA_CONFIG_H_STUB */
