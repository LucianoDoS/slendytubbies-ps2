"""Distant foliage must retain original triangles, UVs and alpha-cutout cards."""
import json,struct
from pathlib import Path
import numpy as np

root=Path(__file__).resolve().parents[1]
blob=(root/'ps2/generated/world.bin').read_bytes()
header=struct.unpack_from('<20I',blob)
faces={}
for index in range(header[2]):
    vertices,triangles,vo,io,texture,flags,prototype,*_=struct.unpack_from('<7I9f',blob,header[8]+index*64)
    if flags&1056!=1056:continue
    points=np.frombuffer(blob,dtype='<f4',count=vertices*8,offset=vo).reshape(-1,8)
    indices=np.frombuffer(blob,dtype='<u2',count=triangles*3,offset=io).reshape(-1,3)
    # Exact positions and texture corners; orientation is retained too.
    output=faces.setdefault(prototype,[])
    for tri in points[indices,:5]:
        corners=[p.tobytes() for p in tri]
        output.append(min(tuple(corners[i:]+corners[:i]) for i in range(3)))
report=[]
for kind in (0,1):
    source=set(faces[kind])
    for level in (9,12):
        distant=faces[kind+level]
        assert len(distant)>=20,'Distant canopy lost too many original leaf triangles'
        assert all(face in source for face in distant),'Leaf card was welded or its UV corners changed'
        report.append(dict(tree=kind,prototype=kind+level,triangles=len(distant),original_triangles_retained=True))
result=dict(passed=True,levels=report)
(root/'ps2/qa/tree-lod-check.json').write_text(json.dumps(result,indent=2))
print(json.dumps(result))
