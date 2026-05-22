Subject: N6Cam — multi-class (person + vehicles) model for STM32N6 / ATON NPU?

Hi Sylvain,

Hope you're well.

We've made good progress on the Scopus PoC firmware for the N6Cam — all the SoW UART commands are in place, photo→SD works, the test harness reports clean numbers, and CDC-based self-update lets us iterate both the Application and the model weights without the boot switch. The person-detection pipeline runs at **~17 ms / frame** on the ATON NPU using the `yolov8n_192_quant_pc_uf_od_coco-person-st.tflite` model that ships with the BSP — excellent for a single-class detector.

Our SoW also calls for **vehicle detection** in addition to people (W6 in our WBS — bicycles / cars / motorcycles / buses / trucks at minimum, i.e. COCO classes 0, 1, 2, 3, 5, 7). We've already wired the firmware side end-to-end for this:

- `AI_OD_YOLOV8_PP_NB_CLASSES` parameterised
- class-mask filter in `nn_task` (bit 0 = people, bit 1 = vehicles)
- `detect profile <det_msk> <act_msk>` shell command persists the mask
- per-class colour palette ready in `SAI_CLASSES[]`
- regression suite already parses `car=N`, `truck=N` etc. detections

What we don't have is a **production-grade multi-class quantised model** for the ATON NPU. We've gone through the pipeline ourselves:

1. Ultralytics `yolov8n.pt` (full 80-class COCO) → ONNX at 192×192 ✅
2. ONNX Runtime static INT8 quantisation with 128 COCO128 calibration images ✅
3. `stedgeai 3.0 generate --target stm32n6 --st-neural-art default@user_neuralart.json` ✅ (3.1 MB binary, fits in SLOT1_WEIGHTS, ~42 ms inference when most ops stay on ATON)
4. Flash via our CDC self-updater ✅

The pipeline is mechanically complete, but the **post-training quantisation collapses detection confidence** to ~0.05–0.10 on clear person photos (vs the 0.7+ that the vendor's coco-person-st.tflite delivers). It's consistent with what the public Ultralytics community reports about PTQ on YOLOv8 small backbones — sufficient for a single class, but the noise floor rises above conf threshold once you spread it over many classes.

Looking at the `stm32-hotspot/ultralytics` repo, all the published N6 deployments are person-only variants (`*coco-person*` and `*coco-person-st*`). The ST-tagged ones look QAT-derived (the `*_quant_pc_uf_od_*` naming hints at per-channel + Ultralytics-framework + object-detection, which matches what `app_postprocess_od_yolov8` expects).

**Two questions**, whichever you can help with:

1. **Do you have, or could ST produce, a multi-class `yolov8n_192_quant_*_coco-st.tflite` variant** — even just COCO's "person + vehicles" subset (classes 0, 1, 2, 3, 5, 7, plus optionally 6=train) — that's tuned the same way as the existing `coco-person-st.tflite`? Something we can drop into `Firmware/Model/network_data.hex` and have working at production accuracy + ~30–50 ms inference.

2. If not, **what's the recommended workflow for QAT** on this target? We have a Jetson Orin available locally. The Cube AI Developer Cloud at https://stedgeai-dc.st.com has a "quantise" mode — is that the path ST recommends, or is there a local Ultralytics + `stedgeai --quantize` flow that gives the same Q/DQ insertion strategy that the `coco-person-st.tflite` was built with?

The PoC delivery is sensitive to this — everything else is in place, but the SoW deliverable explicitly requires people-and-vehicles, and we'd rather not ship the PoC with vehicles stubbed out.

Happy to share our build / quantisation / regression-test scripts if that helps; the BSP is otherwise unchanged from your trunk (we vendor-ed it into our repo). I can also send the `tools/quantize_yolov8n.py` + generated artifacts so you can reproduce our PTQ result on your end.

Thanks for any guidance,
Ilan
ilan@kamacode.com
