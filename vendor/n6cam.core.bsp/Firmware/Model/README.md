# The network on the device — what it is, and how to rebuild it

Three files in this directory name three *different* models, and only one of
them describes what is actually flashed. That ambiguity is what this file
exists to remove; it was raised as a build question on 2026-08-21 and it was a
fair question.

## What is actually in `network_data.hex`

Read it out of the generated header rather than from any of the scripts — the
generator stamps the truth into `stai_network.h` and cannot get it wrong:

```
$ grep STAI_NETWORK_ORIGIN_MODEL_NAME stai_network.h
#define STAI_NETWORK_ORIGIN_MODEL_NAME "gen_best_OE_3_3_1"
```

| | |
|---|---|
| **Source model** | `gen_best.onnx` — a YOLOv8n variant retrained for person **and** vehicle, genuine-ReLU, 256×256 |
| **Committed by** | `2cc30d6` (2026-05-26) *"Multiclass people+vehicles: genuine-ReLU yolov8n @256 (ATON-stable)"* |
| **Input** | 256×256×3, `uint8`, channel-last |
| **Output** | `(84, 1344)` `float32` — 4 box coordinates + 80 COCO classes |
| **Toolchain** | ST Edge AI Core **v4.0** (`STAI-4.0`, `GIT_SHA 7cc65410`) |

`_OE_3_3_1` is not part of the file name you would supply — it is a suffix the
ST quantisation pipeline appends to the network it emits. The input file was
`gen_best.onnx`.

The application reads the same 80-class output: `_class_passes_mask()` in
`nn_task.c` maps COCO class 0 to the people bit and COCO 1–8 (bicycle, car,
motorcycle, bus, truck, airplane, train, boat) to the vehicle bit.

## The two files that say something else, and why

Neither is wrong about its own build; both are simply older than the artifacts
next to them, and neither was refreshed when the final model was generated.

| File | Names | Reality |
|---|---|---|
| `generate.sh` | `yolov8n_192_quant_pc_uf_od_coco-person-st.tflite` | the **original person-only** baseline, archived in `_backup_person_only/`. The script was never updated after the model changed. |
| `network_generate_report.txt` | `pv_epoch0.onnx` (2026-05-25 13:18) | an **intermediate** build, one iteration before the shipped one. The artifacts beside it are from 2026-05-26; the report was not regenerated. |

`generate.sh` now takes the model as an argument, so it can no longer drift
from the model it builds. The report is left as generated — it is a tool
output, and rewriting it by hand would make it a worse record, not a better
one. Trust `stai_network.h`.

## The archived variants

Each `_backup_*` directory is a complete, working set from an earlier
experiment. They carry their own provenance in the same place:

| Directory | `STAI_NETWORK_ORIGIN_MODEL_NAME` |
|---|---|
| `_backup_person_only/` | `yolov8n_192_quant_pc_uf_od_coco-person-st_OE_3_3_1` |
| `_backup_relu30/` | `yolov8n_relu30_u8in_f32out_OE_3_3_1` |
| `_backup_ssd/` | `ssd_voc_OE_3_3_1` |

## Rebuilding

Everything the generator needs is in this directory except the source model
itself: `user_neuralart.json` (the optimisation profile), `my_mpools/` (the
memory pool) and `my_mdescs/` are all committed, and `generate.sh` runs the
same two steps the shipped artifacts came out of.

```bash
./generate.sh <model.onnx|model.tflite>     # stedgeai generate + objcopy → network_data.hex
make flash_weights                          # writes the .hex over SWD
```

The exact invocation behind the committed build, as recorded in the header of
`network.c`:

```
stedgeai generate --model gen_best.onnx --target stm32n6 \
                  --st-neural-art default@user_neuralart.json \
                  --input-data-type uint8 --inputs-ch-position chlast \
                  --output-data-type float32
```

with `user_neuralart.json` supplying
`-O3 --all-buffers-info --cache-maintenance --Oauto-sched --native-float
--enable-virtual-mem-pools --Omax-ca-pipe 4 --Ocache-opt --Os
--enable-epoch-controller`.

The weights are placed at `0x70600000` in external xSPI2 flash, which is what
the `arm-none-eabi-objcopy --change-addresses` step at the end of
`generate.sh` encodes into the `.hex`.

## `gen_best.onnx` itself is not here

It has never been committed, and it is not recoverable from this side. The
generation ran out of `/tmp/ssd_build/` — visible in the recorded command line
in `network.c` (`--onnx-input = "/tmp/ssd_build/st_ai_output/gen_best_OE_3_3_1.onnx"`)
— and that directory has not survived. No `.onnx` has ever been tracked in this
repository, and none is present on the build host or the test bench.

What that does and does not block: the **firmware** builds and links with no
source model at all, because `network.c`, `network_data.hex` and the `stai_*`
files are all committed. Only *retraining or re-quantising* needs the `.onnx`,
and for that the pipeline above is complete the moment a source model is
supplied.

## What it was trained on

The only contemporaneous record of the training run is the message of the
commit that shipped it, `2cc30d6`:

> Replace the person-only NN with a custom yolov8n trained on full COCO for
> cars+people, using GENUINE ReLU (`Conv.default_act`, built from yaml so the
> swap survives ultralytics training). ReLU avoids the SiLU LOGISTIC+MUL
> (`sigmoid*x`) pattern that deadlocks the kit's LL_ATON runtime; runs stably
> on the NPU at ~88 ms (15/15 inferences, no hang). int8 is conv-only (head
> kept float so yolov8's box+cls output doesn't collapse under PTQ).

| | |
|---|---|
| **Dataset** | COCO 2017, the public Ultralytics `coco.yaml` set — all 80 classes trained, classes 0 and 1–8 are the ones the application reads |
| **Architecture** | Ultralytics YOLOv8n, built **from yaml** with `Conv.default_act = ReLU` |
| **Input size** | 256×256 |
| **Quantisation** | PTQ, int8 on the convolutions only; the detection head stays float |

No customer imagery, no proprietary or licensed dataset, and nothing
site-specific was used at any point — COCO 2017 throughout, on the training
host, and COCO128 as the PTQ calibration set.

### What is not recorded, and what that costs

Epochs, learning rate, batch size, augmentation, split and seed were never
written down, and neither the `.pt` nor the `.onnx` survived the build host.
So the recipe above is enough to **reconstruct** a model of the same design —
it is not enough to **replay** the same run. A retrain produces a *new* model
with its own accuracy, and it has to be re-verified on the bench
(`tests/run_tests.py`, then `scopus/run_scopus_tests.py` and
`scopus/run_integration_tests.py`) rather than assumed equivalent.

### What a retrain can start from today

| Committed | What it gives you |
|---|---|
| `tools/train_relu_on_orin.sh` | the SiLU→ReLU fine-tune, driven end to end on a CUDA host |
| `tools/train_qat_on_orin.sh` | the plain full-COCO fine-tune and INT8 TFLite export |
| `tools/quantize_yolov8n.py` | ONNX Runtime static PTQ, COCO128 calibration |
| `datasets/coco8` | the 8-image Ultralytics sanity set, for pipeline smoke tests |
| `yolov8n_192_quant_pc_uf_od_coco-person-st.tflite` | ST's person-only model, the known-ATON-stable baseline |
| `generate.sh` + `user_neuralart.json` + `my_mpools/` | source model → `network_data.hex`, unchanged from the shipped build |

The two training scripts target 192×192 and predate the shipped model; the
committed network is 256×256. They are the pipeline, not the recipe.
