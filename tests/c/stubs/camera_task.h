/* Host stub for camera_task.h, see tests/test_tile_rotate.py.
 *
 * Only tile_capture_live() uses either of these, and it is not what the test
 * exercises: the test drives the sweep over a frame of its own. The stub is
 * here so the module compiles, and camera_get_buffer() answers NULL so that
 * calling it on a PC is a "camera not streaming", not a crash.
 */
#ifndef CAMERA_TASK_H_STUB
#define CAMERA_TASK_H_STUB

#include <stdint.h>

#define DCMIPP_PIPE1  1

uint8_t *camera_get_buffer(int pipe);

#endif /* CAMERA_TASK_H_STUB */
