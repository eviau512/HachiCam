"""Deterministic exploratory alignment cases. No proprietary fixtures or code.

Run with Python + NumPy + OpenCV, optionally --opencv-path .tools/vision.
Synthetic frames independently integrate a high-resolution scene at known phases.
This is an optimizer/patch rejection probe, not end-to-end SR or device acceptance.
"""
import argparse
import json
from pathlib import Path
import platform
import sys
import time

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--opencv-path", type=Path)
parser.add_argument("--output", type=Path, default=Path(".tools/document-alignment/results.json"))
parser.add_argument("--suite", choices=("exploratory", "heldout"), default="exploratory")
args = parser.parse_args()
if args.opencv_path:
    sys.path.insert(0, str(args.opencv_path.resolve()))
import cv2
import numpy as np
from alignment import Config, align_patch, ecc_fit, guarded_ecc_fit


SIZE, SCALE = 128, 8


def scene(seed):
    rng = np.random.default_rng(seed)
    h = np.full((SIZE*SCALE, SIZE*SCALE), 220., np.float32)
    # Generated glyph-like strokes, unique marks, punctuation and fine lines.
    for _ in range(160):
        x, y = rng.integers(12*SCALE, (SIZE-12)*SCALE, 2)
        length = int(rng.integers(2*SCALE, 9*SCALE))
        endpoint = (int(x+length), int(y)) if rng.random() < .5 else (int(x), int(y+length))
        cv2.line(h, (int(x), int(y)), endpoint, float(rng.uniform(35, 140)),
                 int(rng.integers(3, 10)))
    cv2.circle(h, (31*SCALE, 27*SCALE), 9*SCALE, 80., 5)
    # Optical blur occurs before sampling, independently of registration smoothing.
    return cv2.GaussianBlur(h, (0, 0), .35*SCALE)


def observe(h, dx=0., dy=0., seed=0, noise=0., gain=1., bias=0.):
    y, x = np.indices(h.shape, dtype=np.float32)
    shifted = cv2.remap(h, (x-dx*SCALE).astype(np.float32), (y-dy*SCALE).astype(np.float32), cv2.INTER_LINEAR,
                        borderMode=cv2.BORDER_REFLECT_101)
    integrated = shifted.reshape(SIZE, SCALE, SIZE, SCALE).mean(axis=(1, 3))
    rng = np.random.default_rng(seed)
    return np.clip(gain*integrated+bias+rng.normal(0, noise, integrated.shape), 0, 255).round().astype(np.uint8)


def main():
    cv2.setNumThreads(1)
    cfg = Config()
    cases = []
    seeds = range(8) if args.suite == "exploratory" else range(100, 120)
    for seed in seeds:
        h = scene(seed)
        if args.suite == "exploratory":
            dx, dy = [(0.35, -.4), (3.2, -2.75), (-1.65, 2.3), (.7, .2)][seed % 4]
        else:
            dx, dy = np.random.default_rng(seed+800).uniform(-5.5, 5.5, 2)
        for noise in ((0., 6.) if args.suite == "exploratory" else (0., 3., 10.)):
            cases.append((f"strokes_{seed}_noise{noise:g}", "unique", observe(h, seed=seed, noise=noise),
                          observe(h, dx, dy, seed=seed+100, noise=noise), (dx, dy), True))
    h = scene(40 if args.suite == "exploratory" else 140)
    cases.append(("affine_brightness", "photometric", observe(h), observe(h, .6, -.35, gain=.7, bias=18), (.6, -.35), True))
    reference = observe(h)
    occluded = observe(h, .6, -.35)
    occluded[25:103, 25:103] = 60
    cases.append(("occlusion", "negative", reference, occluded, None, False))
    source = observe(h, .6, -.35)
    source[:, SIZE//2:] = observe(h, -3.1, 2.4)[:, SIZE//2:]
    cases.append(("mixed_motion", "negative", reference, source, None, False))
    cases.append(("all_clipped", "negative", np.full((SIZE, SIZE), 255, np.uint8),
                  np.full((SIZE, SIZE), 255, np.uint8), None, False))
    cases.append(("flat", "negative", np.full((SIZE, SIZE), 200, np.uint8),
                  np.full((SIZE, SIZE), 200, np.uint8), None, False))
    y, x = np.indices((SIZE*SCALE,)*2)
    edge = np.where(x < SIZE*SCALE/2, 60., 220.).astype(np.float32)
    cases.append(("one_edge", "negative", observe(edge), observe(edge, .6, -.35), None, False))
    period = ((x//(4*SCALE)+y//(4*SCALE)) % 2)*140.+60.
    cases.append(("periodic_grid", "negative", observe(period.astype(np.float32)),
                  observe(period.astype(np.float32), .6, -.35), None, False))
    records = []
    for name, group, reference, source, truth, should_accept in cases:
        entry = dict(name=name, group=group, truth=truth, should_accept=should_accept)
        for method, fit in (("block_lk", align_patch), ("ecc_fit_only", ecc_fit), ("guarded_ecc", guarded_ecc_fit)):
            start = time.perf_counter()
            result = fit(reference, source, cfg)
            record = result.record()
            record["milliseconds"] = 1000*(time.perf_counter()-start)
            record["endpoint_error"] = (float(np.hypot(result.dx-truth[0], result.dy-truth[1])) if truth else None)
            entry[method] = record
        records.append(entry)
    summary = {}
    for method in ("block_lk", "ecc_fit_only", "guarded_ecc"):
        positives = [r for r in records if r["should_accept"]]
        accepted = [r[method]["endpoint_error"] for r in positives if r[method]["accepted"]]
        timings = [r[method]["milliseconds"] for r in records]
        summary[method] = dict(positive_accepted=len(accepted), positive_total=len(positives),
             accepted_error_median=float(np.median(accepted)) if accepted else None,
             accepted_error_p95=float(np.percentile(accepted, 95)) if accepted else None,
             negative_accepted=sum(r[method]["accepted"] for r in records if not r["should_accept"]),
             negative_total=sum(not r["should_accept"] for r in records),
             patch_time_median_ms=float(np.median(timings)), patch_time_p95_ms=float(np.percentile(timings, 95)))
    payload = dict(scope="optimizer_and_patch_rejection_only", suite=args.suite, fixture_revision=1,
                   runtime=dict(python=platform.python_version(), opencv=cv2.__version__, numpy=np.__version__),
                   config=cfg.__dict__, patch_size=SIZE, integration_scale=SCALE,
                   summary=summary, records=records)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(payload, indent=2, allow_nan=False)+"\n", encoding="utf-8")
    print(json.dumps(summary, indent=2))
    # These are meaningful ambiguity/known-phase probes, not product acceptance.
    assert summary["block_lk"]["negative_accepted"] == 0, "Unsupported patch accepted"
    assert summary["block_lk"]["positive_accepted"] >= .8*summary["block_lk"]["positive_total"], "Excessive loss of useful patches"
    assert summary["block_lk"]["accepted_error_p95"] <= .15, "Subpixel exploratory target missed"
    assert summary["guarded_ecc"]["negative_accepted"] == 0, "Guarded ECC accepted unsupported patch"


if __name__ == "__main__":
    main()
