#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Jolla Mobile Ltd
#
# SPDX-License-Identifier: BSD-3-Clause
"""
Sanity-check a lens_shading_camera<ID>.json calibration by re-applying it
(in Python, independently of the C++ DNG writer) to a RAW DNG and reporting
the residual vignetting and R/G, B/G color shift by radius.

Use this after generating or changing a calibration to confirm it actually
improves things, and again after any change to
tools/calibration/generate_lens_shading.py or src/dnglensshading.cpp, to
make sure the two stay in agreement (this script's gain-map interpretation
must match src/dnglensshading.cpp's).

Usage:
    python3 verify_gain_map.py --calibration ../../src/calibration/lens_shading_camera0.json check.dng
    python3 verify_gain_map.py --calibration ../../src/calibration/lens_shading_camera0.json check1.dng check2.dng ...

Requirements:
    pip install rawpy numpy scipy
"""
import argparse
import json
import sys

import numpy as np
import rawpy
from scipy.ndimage import zoom

PHASE_NAMES = {
    "RGGB": ["R", "Gr", "Gb", "B"],
    "GRBG": ["Gr", "R", "B", "Gb"],
    "GBRG": ["Gb", "B", "R", "Gr"],
    "BGGR": ["B", "Gb", "Gr", "R"],
}


def radial_profile(plane, dy, dx, cy, cx, rmax, n_bins):
    ph, pw = plane.shape
    yy, xx = np.mgrid[0:ph, 0:pw]
    r = np.sqrt((yy * 2 + dy - cy) ** 2 + (xx * 2 + dx - cx) ** 2)
    bins = (r / rmax * n_bins).astype(int).clip(0, n_bins - 1)
    return np.array([plane[bins == i].mean() for i in range(n_bins)])


def check_one_file(dng_file, calibration, names, grid_rows, grid_cols, n_bins):
    with rawpy.imread(dng_file) as raw:
        if (raw.sizes.raw_width, raw.sizes.raw_height) != (
                calibration["image_width"], calibration["image_height"]):
            print(f"SKIPPED {dng_file}: resolution does not match calibration; "
                  "results would be meaningless")
            return None

        img = raw.raw_image.astype(np.float64)
        black = raw.black_level_per_channel
        pattern = raw.raw_pattern
        h, w = img.shape
        cy, cx = h / 2, w / 2
        rmax = np.sqrt(cy ** 2 + cx ** 2)

        before, after = {}, {}
        for dy in range(2):
            for dx in range(2):
                name = names[dy * 2 + dx]
                black_level = float(black[pattern[dy, dx]]) if black else 0.0
                plane = img[dy::2, dx::2] - black_level

                grid = np.array(calibration["planes"][name]).reshape(grid_rows, grid_cols)
                zy, zx = plane.shape[0] / grid_rows, plane.shape[1] / grid_cols
                gain = zoom(grid, (zy, zx), order=1)[:plane.shape[0], :plane.shape[1]]

                before[name] = radial_profile(plane, dy, dx, cy, cx, rmax, n_bins)
                after[name] = radial_profile(plane * gain, dy, dx, cy, cx, rmax, n_bins)

    def report(label, profiles):
        print(f"\n{label} (percent of centre value, by radius bin 0=centre..{n_bins - 1}=corner):")
        for name in ("R", "Gr", "Gb", "B"):
            p = profiles[name]
            print(f"  {name:>2}: {np.round(100 * p / p[0], 1)}")
        g = (profiles["Gr"] + profiles["Gb"]) / 2
        print(f"  R/G ratio: {np.round(profiles['R'] / g, 3)}")
        print(f"  B/G ratio: {np.round(profiles['B'] / g, 3)}")

    report("BEFORE correction", before)
    report("AFTER correction", after)

    worst_before = min(min(100 * p / p[0]) for p in before.values())
    worst_after = min(min(100 * p / p[0]) for p in after.values())
    print(f"\nWorst-case corner brightness: {worst_before:.1f}% -> {worst_after:.1f}% of centre "
          f"(closer to 100% is better)")
    improved = worst_after > worst_before
    if improved:
        print("Calibration improves vignetting on this file.")
    else:
        print("WARNING: calibration does not improve vignetting on this file -- "
              "check the DNG matches the camera/lens the calibration was generated for.")
    return worst_before, worst_after, improved


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                      formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("dng_files", nargs="+", help="One or more DNG files to check")
    parser.add_argument("--calibration", required=True)
    parser.add_argument("--bins", type=int, default=10)
    args = parser.parse_args()

    calibration = json.load(open(args.calibration))
    names = PHASE_NAMES.get(calibration["cfa_pattern"])
    if names is None:
        sys.exit(f"Unsupported CFA pattern: {calibration['cfa_pattern']}")
    grid_rows, grid_cols = calibration["grid_rows"], calibration["grid_cols"]

    results = {}
    for i, dng_file in enumerate(args.dng_files):
        if len(args.dng_files) > 1:
            print(f"\n{'=' * 70}\n{dng_file}\n{'=' * 70}")
        results[dng_file] = check_one_file(dng_file, calibration, names, grid_rows, grid_cols, args.bins)

    if len(args.dng_files) > 1:
        print(f"\n{'=' * 70}\nSummary ({len(args.dng_files)} files)\n{'=' * 70}")
        for dng_file, result in results.items():
            if result is None:
                print(f"  {dng_file}: skipped")
                continue
            worst_before, worst_after, improved = result
            status = "OK" if improved else "NOT IMPROVED"
            print(f"  {dng_file}: {worst_before:.1f}% -> {worst_after:.1f}% [{status}]")
        if any(r is not None and not r[2] for r in results.values()):
            sys.exit(1)


if __name__ == "__main__":
    main()

