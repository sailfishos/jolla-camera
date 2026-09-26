#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
#
# SPDX-License-Identifier: BSD-3-Clause
"""
Generate a per-channel lens-shading (vignetting + color-shift) calibration
file from one or more RAW DNG captures of a flat, neutral-grey target.

The output JSON is consumed by the RAWfish camera plugin
(src/dnglensshading.cpp) to emit a DNG GainMap OpcodeList (OpcodeList2) when
saving RAW captures, so viewers/editors correct the falloff automatically.

Usage:
    python3 generate_lens_shading.py --camera-id 0 --output \\
        ../../src/calibration/lens_shading_camera0.json \\
        flat1.dng flat2.dng ...

Requirements:
    pip install rawpy numpy

How it works:
    1. Each input DNG is a RAW (mosaiced) capture of an evenly lit, neutral
       grey/white card filling the frame.
    2. The Bayer mosaic is split into its four CFA phases (R, Gr, Gb, B)
       using the sensor's actual CFA pattern (read from the DNG, or
       overridden with --cfa).
    3. Each phase plane is black-level corrected, then averaged into coarse
       tiles on a (--grid-rows x --grid-cols) grid.
    4. Per plane, the gain at each grid point is reference / tile_mean,
       where "reference" is the mean value in a small patch at the optical
       centre. Centre gain is therefore ~1.0 and gain increases towards the
       corners, independently for R/Gr/Gb/B -- this corrects vignetting
       (all four channels darken together) and the residual color shift
       (channels darken at different rates) in a single map.
    5. When multiple input files are given, each is processed independently
       and the resulting gain grids are averaged, which reduces sensor
       noise and gives a simple sanity check (--max-disagreement reports
       how much the individual estimates differed).
    6. --balance and --strength then re-dose the averaged gain grid before
       it is written out (see apply_balance_and_strength()): --balance
       trades brightening the corners for darkening the centre instead
       (both change exposure by the same shape, just around a different
       pivot), and --strength scales how much of the correction is baked
       in at all. The DNG reader (src/dnglensshading.cpp) writes whatever
       gain values are in the JSON as-is, so this is the only place that
       dosing is controlled.

The calibration is only valid for the specific camera_id, sensor resolution
and CFA pattern it was generated from. Regenerate it whenever the RAW
capture resolution changes (e.g. a different binning mode).
"""
import argparse
import datetime
import json
import sys

import numpy as np

try:
    import rawpy
except ImportError:
    sys.exit("This tool requires rawpy: pip install rawpy numpy")

# Canonical phase order used throughout this tool and in the JSON output.
PLANE_NAMES = ("R", "Gr", "Gb", "B")


def cfa_phase_names(cfa_pattern):
    """
    Map each of the four 2x2 CFA phases, in (row, col) = (0,0),(0,1),(1,0),(1,1)
    order, to one of "R", "Gr", "Gb", "B".

    "Gr" is the green phase sharing a row with red; "Gb" is the green phase
    sharing a row with blue. This must match cfaPatternBytes() in
    src/declarativecameraextensions.cpp.
    """
    layouts = {
        "RGGB": ["R", "Gr", "Gb", "B"],
        "GRBG": ["Gr", "R", "B", "Gb"],
        "GBRG": ["Gb", "B", "R", "Gr"],
        "BGGR": ["B", "Gb", "Gr", "R"],
    }
    if cfa_pattern not in layouts:
        raise ValueError(f"Unsupported CFA pattern: {cfa_pattern}")
    return layouts[cfa_pattern]


def detect_cfa_pattern(raw):
    """Derive the RGGB/GRBG/GBRG/BGGR string from rawpy's pattern + color_desc."""
    color_desc = raw.color_desc.decode("ascii")  # e.g. "RGBG"
    pattern = raw.raw_pattern  # 2x2 array of indices into color_desc
    letters = "".join(color_desc[pattern[y, x]] for y in range(2) for x in range(2))
    # letters is e.g. "BGGR", "RGGB", "GRBG", "GBRG" already in the right order
    if letters not in ("RGGB", "GRBG", "GBRG", "BGGR"):
        raise ValueError(f"Unrecognised CFA layout from DNG: {letters}")
    return letters


def extract_planes(raw):
    """Split the raw mosaic into the 4 CFA phase planes, black-level corrected."""
    img = raw.raw_image.astype(np.float64)
    black_levels = raw.black_level_per_channel
    pattern = raw.raw_pattern
    planes = {}
    for dy in range(2):
        for dx in range(2):
            color_index = pattern[dy, dx]
            black = float(black_levels[color_index]) if black_levels else 0.0
            planes[(dy, dx)] = img[dy::2, dx::2] - black
    return planes


def sample_grid(plane, grid_rows, grid_cols, window_fraction=0.5):
    """
    Sample `plane` on a (grid_rows x grid_cols) grid whose sample points are
    anchored exactly at the image edges: sample index 0 sits at row/col 0
    and the last sample sits at the last row/col. This matches what the
    DNG GainMap decoder assumes (both the Adobe DNG SDK's
    dng_gain_map_interpolator and darktable's rawprepare.c place grid
    point i at the normalized position i/(N-1) of the full image -- see
    src/dnglensshading.cpp for the corresponding encoder side).

    Each sample averages a small local window centred on that exact
    position (to reduce sensor noise), rather than the average of a whole
    non-overlapping tile as an earlier version of this function did. Tile
    averaging put the grid's edge samples at the *centre* of the outermost
    tile rather than at the image's actual edge, which systematically
    under-represents the true falloff right at the corners (the corner
    pixels are always darker than the average of the wider tile they sit
    in), so the previous encoding under-corrected the corners specifically
    even when the middle of the frame was already well corrected.
    """
    h, w = plane.shape
    row_centers = np.linspace(0, h - 1, grid_rows)
    col_centers = np.linspace(0, w - 1, grid_cols)
    row_half = max(1, int(window_fraction * h / grid_rows / 2))
    col_half = max(1, int(window_fraction * w / grid_cols / 2))
    grid = np.empty((grid_rows, grid_cols), dtype=np.float64)
    for r, rc in enumerate(row_centers):
        r0 = max(0, int(round(rc - row_half)))
        r1 = min(h, int(round(rc + row_half)) + 1)
        for c, cc in enumerate(col_centers):
            c0 = max(0, int(round(cc - col_half)))
            c1 = min(w, int(round(cc + col_half)) + 1)
            tile = plane[r0:r1, c0:c1]
            grid[r, c] = tile.mean() if tile.size else 0.0
    return grid


def centre_reference(plane, patch_fraction=0.06):
    """Mean value of a small patch at the geometric centre of `plane`."""
    h, w = plane.shape
    ph, pw = max(1, int(h * patch_fraction)), max(1, int(w * patch_fraction))
    y0, x0 = (h - ph) // 2, (w - pw) // 2
    patch = plane[y0:y0 + ph, x0:x0 + pw]
    return float(patch.mean())


def compute_gain_grid(plane, grid_rows, grid_cols, max_gain):
    tiles = sample_grid(plane, grid_rows, grid_cols)
    reference = centre_reference(plane)
    if reference <= 0:
        raise ValueError("Centre reference value is <= 0; is the capture too dark?")
    tiles = np.clip(tiles, reference / max_gain, None)  # avoid div-by-~0 in noise
    gain = reference / tiles
    return np.clip(gain, 1.0, max_gain)


def report_clamping(averaged, grid_rows, grid_cols, max_gain):
    """
    Warn when the shipped (post-averaging) gain grid is sitting at the
    --max-gain ceiling, since a clamped point is silently weaker than the
    correction that point actually needs -- the JSON stores 4.0 either way,
    so nothing else about the output would reveal this.

    Also breaks the count down by left vs. right edge column(s), because a
    lopsided result (e.g. the right edge clamping far more than the left)
    almost never means "raise --max-gain": it's the signature of an
    unevenly lit flat-field target (brighter on one side than the other),
    which the calibration model cannot distinguish from real vignetting.
    See calibration/README.md's guidance to retake the flat with more even
    light rather than raising the limit.
    """
    edge_cols = max(1, grid_cols // 4)
    threshold = max_gain - 1e-6
    any_clamped = False
    for name in PLANE_NAMES:
        clamped = averaged[name] >= threshold
        total = int(clamped.sum())
        if total == 0:
            continue
        any_clamped = True
        left = int(clamped[:, :edge_cols].sum())
        right = int(clamped[:, -edge_cols:].sum())
        pct = 100.0 * total / clamped.size
        print(f"  {name:>2}: {total}/{clamped.size} grid points ({pct:.1f}%) "
              f"clamped at max-gain={max_gain:g} "
              f"(left {edge_cols} col(s): {left}, right {edge_cols} col(s): {right})")
        if right > 2 * max(left, 1) or left > 2 * max(right, 1):
            heavier = "right" if right > left else "left"
            print(f"      -> heavily lopsided towards the {heavier} edge: this usually "
                  "means the flat-field target was lit unevenly, not that --max-gain is "
                  "too low. Retake the calibration capture with more even lighting before "
                  "considering --max-gain (see calibration/README.md).")
    if not any_clamped:
        print("  none of the four planes hit max-gain: the ceiling did not limit this "
              "calibration.")


def apply_balance_and_strength(averaged, balance, strength):
    """
    Re-dose the averaged, per-plane gain grids (each value >= 1.0, 1.0 at
    the optical centre, largest at the corners) between purely brightening
    the corners and purely darkening the rest of the frame -- the encoder
    side (src/dnglensshading.cpp) just writes back whatever ends up in the
    JSON, so this is the only place that decides which of the two the
    shipped calibration actually does.

    balance (0-100) picks the pivot gain value that ends up unchanged
    (mapped to 1.0): at 0 the pivot is 1.0, i.e. today's behaviour --
    every point is only ever brightened up towards the map's peak gain,
    the centre is left alone. At 100 the pivot is the single largest gain
    value found across all four planes, so that point (the point needing
    the *most* correction, usually a corner) is left alone and everything
    else -- including the centre -- is only ever darkened down towards
    it. Values in between blend the two. The same pivot is used for all
    four planes so balance only ever shifts overall brightness and never
    disturbs the existing R/Gr/Gb/B colour-shift correction between
    planes.

    The pivot is interpolated geometrically (pivot = global_max ** (balance
    / 100)) rather than linearly, because these values are gains and
    combine multiplicatively: a linear blend would move the pivot very
    little over most of the slider and then jump near the top end, while
    the geometric blend keeps --balance's visible effect roughly even
    across its whole 0-100 range.

    strength (0-100) then blends the re-pivoted grid back towards a flat
    1.0 (no correction at all) in gain space, so 0 writes a no-op
    calibration and 100 writes the full (re-pivoted) correction.

    Returns a new {plane_name: ndarray} dict; does not mutate `averaged`.
    """
    balance = max(0.0, min(100.0, balance)) / 100.0
    strength = max(0.0, min(100.0, strength)) / 100.0

    global_max_gain = max(1.0, max(float(averaged[name].max()) for name in PLANE_NAMES))
    pivot = global_max_gain ** balance

    dosed = {}
    for name in PLANE_NAMES:
        rebalanced = averaged[name] / pivot
        blended = 1.0 + strength * (rebalanced - 1.0)
        # Defensive floor: a GainMap value must stay strictly positive for
        # readers to make sense of it.
        dosed[name] = np.clip(blended, 1e-3, None)
    return dosed, pivot


def process_file(path, grid_rows, grid_cols, max_gain, cfa_override):
    with rawpy.imread(path) as raw:
        cfa = cfa_override or detect_cfa_pattern(raw)
        names = cfa_phase_names(cfa)
        planes = extract_planes(raw)
        width, height = raw.sizes.raw_width, raw.sizes.raw_height

        gains = {}
        phase_index = 0
        for dy in range(2):
            for dx in range(2):
                name = names[phase_index]
                gains[name] = compute_gain_grid(
                    planes[(dy, dx)], grid_rows, grid_cols, max_gain)
                phase_index += 1
        return cfa, width, height, gains


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dng_files", nargs="+", help="Flat-field grey card DNG captures")
    parser.add_argument("--camera-id", required=True,
                         help='Camera2 camera id this calibration applies to (e.g. "0")')
    parser.add_argument("--output", required=True, help="Output calibration JSON path")
    parser.add_argument("--grid-rows", type=int, default=12,
                         help="Gain map grid rows per plane (default: 12)")
    parser.add_argument("--grid-cols", type=int, default=16,
                         help="Gain map grid columns per plane (default: 16)")
    parser.add_argument("--max-gain", type=float, default=4.0,
                         help="Clamp on corner gain to avoid amplifying noise (default: 4.0)")
    parser.add_argument("--balance", type=float, default=0.0,
                         help="0-100: how much of the correction is applied by darkening "
                              "the centre instead of only brightening the corners. 0 "
                              "(default) matches the original, purely-additive behaviour; "
                              "100 leaves the point needing the most correction unchanged "
                              "and only ever darkens everything else.")
    parser.add_argument("--strength", type=float, default=100.0,
                         help="0-100: how much of the (--balance-dosed) correction to "
                              "actually bake into the output. 100 (default) is the full "
                              "correction; 0 writes a no-op calibration.")
    parser.add_argument("--cfa", choices=["RGGB", "GRBG", "GBRG", "BGGR"],
                         help="Override auto-detected CFA pattern")
    args = parser.parse_args()
    if not 0.0 <= args.balance <= 100.0:
        sys.exit("--balance must be between 0 and 100")
    if not 0.0 <= args.strength <= 100.0:
        sys.exit("--strength must be between 0 and 100")

    per_file_gains = []
    reference_cfa = reference_width = reference_height = None

    for path in args.dng_files:
        cfa, width, height, gains = process_file(
            path, args.grid_rows, args.grid_cols, args.max_gain, args.cfa)
        if reference_cfa is None:
            reference_cfa, reference_width, reference_height = cfa, width, height
        elif (cfa, width, height) != (reference_cfa, reference_width, reference_height):
            sys.exit(f"{path}: CFA/resolution ({cfa}, {width}x{height}) does not match "
                      f"the first file ({reference_cfa}, {reference_width}x{reference_height})")
        per_file_gains.append(gains)
        print(f"Processed {path}: CFA={cfa}, {width}x{height}")

    averaged = {}
    max_disagreement = 0.0
    for name in PLANE_NAMES:
        stacked = np.stack([g[name] for g in per_file_gains])
        averaged[name] = stacked.mean(axis=0)
        if stacked.shape[0] > 1:
            spread = (stacked.max(axis=0) - stacked.min(axis=0)).max()
            max_disagreement = max(max_disagreement, float(spread))

    if len(per_file_gains) > 1:
        print(f"Max disagreement between input files: {max_disagreement:.4f} gain units")

    print(f"\nClamping check (--max-gain={args.max_gain:g}):")
    report_clamping(averaged, args.grid_rows, args.grid_cols, args.max_gain)

    dosed, pivot = apply_balance_and_strength(averaged, args.balance, args.strength)
    print(f"\nDosing (--balance={args.balance:g}, --strength={args.strength:g}): "
          f"pivot gain={pivot:.4f} "
          f"({'no change' if pivot <= 1.0 + 1e-9 else 'centre will be darkened'})")

    calibration = {
        "version": 1,
        "camera_id": args.camera_id,
        "cfa_pattern": reference_cfa,
        "image_width": reference_width,
        "image_height": reference_height,
        "grid_rows": args.grid_rows,
        "grid_cols": args.grid_cols,
        "max_gain": args.max_gain,
        "balance": args.balance,
        "strength": args.strength,
        "planes": {name: np.round(dosed[name], 5).flatten().tolist()
                   for name in PLANE_NAMES},
        "source_files": [path.split("/")[-1] for path in args.dng_files],
        "generated_at": datetime.datetime.now(datetime.timezone.utc)
                         .strftime("%Y-%m-%dT%H:%M:%SZ"),
    }

    with open(args.output, "w") as f:
        json.dump(calibration, f, indent=2)
        f.write("\n")
    print(f"Wrote {args.output}")


if __name__ == "__main__":
    main()
