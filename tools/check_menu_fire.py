"""Check visible flame movement in actual PCSX2 GS frame dumps."""
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(sys.argv[1])
frames = {}
for path in sorted(root.glob("*.png")):
    match = re.search(r"_f(\d+)_fr", path.name)
    if match:
        # The last dump for a frame includes its last submitted GS draw.
        frames[int(match[1])] = path
assert len(frames) >= 3, "Not enough captured frames to check movement"
ordered = sorted(frames.items())
images = [(number, path, np.asarray(Image.open(path).convert("RGB")))
          for number, path in ordered]
width, height = images[0][2].shape[1], images[0][2].shape[0]
box = (int(width * 195 / 512), int(height * 340 / 448),
       int(width * 255 / 512), int(height * 420 / 448))
title_box = (int(width * 170 / 512), int(height * 8 / 448),
             int(width * 345 / 512), int(height * 82 / 448))
smoke_box = (int(width * 455 / 512), int(height * 300 / 448),
             width, int(height * 420 / 448))


def crop(array, bounds):
    x0, y0, x1, y1 = bounds
    return array[y0:y1, x0:x1]


comparisons = []
for left, right in zip(images, images[1:]):
    a, b = crop(left[2], box), crop(right[2], box)
    delta = np.abs(a.astype(np.int16) - b.astype(np.int16))
    changed = int(np.count_nonzero(delta.max(axis=2) > 8))
    lit = int(np.count_nonzero((b[:, :, 0] > 70) &
                              (b[:, :, 0] > b[:, :, 2] * 1.5)))
    title_change = int(np.count_nonzero(np.abs(
        crop(left[2], title_box).astype(np.int16) -
        crop(right[2], title_box).astype(np.int16)).max(axis=2) > 8))
    title_pixels = int(np.count_nonzero(crop(right[2], title_box).max(axis=2) > 70))
    smoke_change = int(np.count_nonzero(np.abs(
        crop(left[2], smoke_box).astype(np.int16) -
        crop(right[2], smoke_box).astype(np.int16)).max(axis=2) > 8))
    comparisons.append(dict(frames=[left[0], right[0]], changed=changed,
                            flame_pixels=lit, title_changed=title_change,
                            title_pixels=title_pixels, smoke_changed=smoke_change))
valid = [item for item in comparisons
         if item["changed"] > 25 and item["flame_pixels"] > 15
         and item["title_changed"] == 0 and item["title_pixels"] > 500
         and item["smoke_changed"] > 25]
assert len(valid) >= 2, f"Flames frozen or scene unstable: {comparisons}"
report = dict(result="PASS", frames=len(images), region=box,
              comparisons=comparisons)
(root / "menu-fire-check.json").write_text(json.dumps(report, indent=2))
print(f"PASS: animated menu flames in {len(valid)} PCSX2 frame comparisons")
