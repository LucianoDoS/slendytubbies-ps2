"""Check GS captures for dark gaps inside the flashlight's floor projection."""
import json
import re
import sys
from pathlib import Path

import numpy as np
from PIL import Image

root = Path(sys.argv[1])
frames = {}
for path in sorted(root.glob('*.png')):
    match = re.search(r'_f(\d+)_fr', path.name)
    if match:
        frames[int(match[1])] = path
assert 60 in frames and 180 in frames, 'Missing controlled light-on/off frames'
on = np.asarray(Image.open(frames[60]).convert('RGB')).astype(np.int16)
off = np.asarray(Image.open(frames[180]).convert('RGB')).astype(np.int16)
height, width = on.shape[:2]
box = [int(width * 235 / 512), int(height * 145 / 448),
       int(width * 275 / 512), int(height * 180 / 448)]
x0, y0, x1, y1 = box
delta = on[y0:y1, x0:x1, 0] - off[y0:y1, x0:x1, 0]
coverage = float(np.mean(delta > 8))
change=on[y0:y1,x0:x1]-off[y0:y1,x0:x1]
neutral_ratio=float(change[:,:,2].mean()/max(change[:,:,0].mean(),1))
report = dict(result='PASS' if coverage >= .98 else 'FAIL',
              lit_fraction=coverage, region=box,
              blue_to_red_light_delta=neutral_ratio,
              on_frame=frames[60].name, off_frame=frames[180].name)
(root / 'flashlight-check.json').write_text(json.dumps(report, indent=2))
assert coverage >= .98, f'Gaps in floor light: {coverage:.3%} illuminated'
assert neutral_ratio>.45, f'Flashlight still has a strong warm tint: blue/red {neutral_ratio:.3f}'
print(f'PASS: flashlight floor projection covers {coverage:.2%} of its test region')
