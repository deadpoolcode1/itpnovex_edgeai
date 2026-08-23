#!/bin/bash
#
# Regenerate the on-device network from a source model.
#
# The model is an argument on purpose. This script used to name one model
# inline, was never updated when the model changed, and so spent three months
# claiming the firmware ran the person-only tflite when it had been a
# person+vehicle net since 2026-05-26. See README.md; `stai_network.h` is the
# file that always tells the truth about what is committed.
set -euo pipefail

MODEL="${1:-}"
if [ -z "$MODEL" ]; then
  echo "usage: $0 <model.onnx|model.tflite>" >&2
  echo "  the committed network was built from gen_best.onnx (see README.md)" >&2
  exit 2
fi

stedgeai generate --model "$MODEL" --target stm32n6 \
                  --st-neural-art default@user_neuralart.json \
                  --input-data-type uint8 --inputs-ch-position chlast \
                  --output-data-type float32
cp st_ai_output/network_generate_report.txt .
cp st_ai_output/network.c .
cp st_ai_output/network_ecblobs.h .
cp st_ai_output/stai_network.c .
cp st_ai_output/stai_network.h .
cp st_ai_output/network_atonbuf.xSPI2.raw network_data.xSPI2.bin
arm-none-eabi-objcopy -I binary network_data.xSPI2.bin --change-addresses 0x70600000 -O ihex network_data.hex
