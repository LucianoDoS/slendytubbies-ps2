"""Check native steering logs and visible vacuum motion in fixed-camera GS frames."""
import json
import re
import sys
from pathlib import Path
import numpy as np
from PIL import Image

capture, log_path, elf_path = map(Path, sys.argv[1:])
log = log_path.read_text(encoding='utf-8', errors='replace')
assert 'STPS SMOKE PASS noonoo' in log
turns, x_span, z_span = map(float, re.search(
    r'STPS NOONOO SUMMARY turns=([\d.]+) x_span=([\d.]+) z_span=([\d.]+)', log).groups())
assert turns > 1 and x_span > 1 and z_span > 1
frames = []
for file in sorted(capture.glob('*.png')):
    match = re.search(r'_f(\d+)_', file.name)
    if match and int(match[1]) > 30:
        pixels = np.asarray(Image.open(file).convert('RGB').resize((512, 448))).astype(float)
        frames.append((file, pixels[160:365, 80:440]))
assert len(frames) >= 8, 'Missing vacuum movement captures'
changes = [int((np.abs(pixels - frames[0][1]).max(axis=2) > 16).sum()) for _, pixels in frames[1:]]
blue = [int(((pixels[:, :, 2] > pixels[:, :, 0] + 25) &
             (pixels[:, :, 1] > pixels[:, :, 0] + 15)).sum()) for _, pixels in frames]
assert max(changes) > 150 and max(blue) > 80, 'Vacuum must move and remain visible'
report = dict(passed=True, turns=turns, x_span_m=x_span, z_span_m=z_span,
              maximum_changed_pixels=max(changes), maximum_vacuum_blue_pixels=max(blue),
              elf_sha256=json.loads(elf_path.read_text(encoding='utf-8-sig'))['Hash'])
(capture / 'noonoo-check.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
(capture / 'full-log.txt').write_text(log, encoding='utf-8')
print(json.dumps(report))
