/* Host stub for nn_task.h, see tests/test_tile_rotate.py.
 *
 * The real one reaches the whole ThreadX and post-processing tree for one
 * struct. tile_detect.c uses the FIELD NAMES below and nothing else about it,
 * and a rename in the real type breaks the firmware build long before it could
 * make this test lie.
 */
#ifndef NN_TASK_H_STUB
#define NN_TASK_H_STUB

#include <stdint.h>

#define NN_BOXES_MAX_NUM  20

typedef struct
{
  float   x_center;
  float   y_center;
  float   width;
  float   height;
  float   conf;
  int32_t class_index;
} t_nn_box;

#endif /* NN_TASK_H_STUB */
