"""Verify spatial partitioning preserves every triangle and its attributes."""
import hashlib
import json
import struct
import sys
from collections import Counter
from pathlib import Path

import numpy as np


def geometry(path):
    data = Path(path).read_bytes()
    header = struct.unpack_from('<20I', data)
    result = Counter()
    for i in range(header[2]):
        m = struct.unpack_from('<7I9f', data, header[8] + i * 64)
        vertices = np.frombuffer(data, '<f4', m[0] * 8, m[2]).reshape(-1, 8)
        indices = np.frombuffer(data, '<u2', m[1] * 3, m[3]).reshape(-1, 3)
        material = struct.pack('<3I3f', m[4], m[5], m[6], *m[-3:])
        for triangle in vertices[indices]:
            result[hashlib.sha256(material + triangle.tobytes()).digest()] += 1
    return result, header[2], hashlib.sha256(data).hexdigest()


before, old_meshes, old_hash = geometry(sys.argv[1])
after, new_meshes, new_hash = geometry(sys.argv[2])
assert before == after, 'Spatial partition changed triangle geometry or attributes'
report = dict(result='PASS', triangles=sum(after.values()),
              meshes_before=old_meshes, meshes_after=new_meshes,
              before_sha256=old_hash, after_sha256=new_hash)
Path('ps2/qa/clusters-check.json').write_text(json.dumps(report, indent=2))
print(f'PASS: all {sum(after.values())} triangles and attributes preserved')
