<!--
SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
SPDX-License-Identifier: BSD-3-Clause
-->

# Lens shading calibration

Files here correct per-camera vignetting (corner darkening) and the
associated color shift (red/blue falloff faster or slower than green) when
saving RAW captures as DNG. The correction is written into the DNG itself as
a standard `OpcodeList2` / `GainMap` opcode (see DNG spec >= 1.3), so it is
applied automatically by any DNG-aware raw processor (Lightroom, darktable,
RawTherapee, etc.) -- RAWfish does not need to do any pixel processing
itself.

## How it works

* `lens_shading_camera<ID>.json` holds, for the Camera2 camera whose id is
  `<ID>`, a coarse per-CFA-channel (R, Gr, Gb, B) gain grid computed from
  photos of a flat, evenly lit, neutral grey/white target.
* `DngLensShading::buildOpcodeList2()` (`src/dnglensshading.cpp`) turns that
  grid into the DNG `GainMap` opcodes and `writeTiffDng()`
  (`src/declarativecameraextensions.cpp`) attaches them to every DNG saved
  for that camera, as long as the capture's resolution and CFA pattern still
  match the calibration.
* If no `lens_shading_camera<ID>.json` exists for a camera, or it does not
  match the capture (different resolution/CFA -- e.g. a binning mode
  change), no opcode is written and DNGs are produced exactly as before.

## Regenerating a calibration

1. Mount/point the camera at a flat, evenly lit, neutral grey or white
   card filling the entire frame (out of focus is fine and often better,
   since it hides texture in the card). Avoid mixed lighting and keep the
   card as flat as possible.
2. Capture one or more RAW DNGs of the card with RAWfish
   (Settings -> RAW capture format -> DNG, or RAW16 + JSON + DNG).
3. Run the generator:

   ```
   python3 tools/calibration/generate_lens_shading.py \
       --camera-id 0 \
       --output src/calibration/lens_shading_camera0.json \
       flat1.dng flat2.dng
   ```

   Replace `0` with the actual Camera2 id (see `Settings -> ... -> camera_id`
   in a capture's own `.json` sidecar, or `adb shell dumpsys media.camera`).
   Passing more than one capture reduces sensor noise in the calibration and
   the tool reports how much the independent estimates disagreed, as a
   sanity check.
4. Take a new DNG with that camera and confirm the `OpcodeList2` tag is now
   present (e.g. `exiftool -OpcodeList2 capture.dng`) and that a DNG-aware
   viewer shows flatter corners and less color drift than before.
5. Repeat for every physical camera (main, ultrawide, tele, front, ...)
   exposed by the device -- each has its own lens and needs its own
   `lens_shading_camera<ID>.json`.

## Limitations

* The calibration is tied to a specific RAW capture resolution and CFA
  pattern. If the device exposes several RAW sizes (e.g. full-res vs. a
  binned mode) per camera, generate and ship one calibration file per size
  actually used, or accept that binned captures fall back to uncorrected
  DNGs.
* This corrects optical vignetting and shading measured at one focus
  distance/aperture; it will be slightly less accurate at very different
  focus distances if the lens is not fully fixed-focus, which is a
  standard limitation of static lens shading maps.
* `--max-gain` (default 4.0) exists to avoid amplifying sensor noise in
  very dark corners; if the generator reports it is clamping heavily on
  your card/lighting, retake the flat with more even light rather than
  raising the limit.
