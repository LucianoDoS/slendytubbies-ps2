"""Check the native two-stage win flow and retain complete GS frames for review."""
import argparse
import json
import re
from pathlib import Path
import numpy as np
from PIL import Image

p=argparse.ArgumentParser()
p.add_argument('capture',type=Path)
p.add_argument('log',type=Path)
p.add_argument('snapshot',type=Path)
a=p.parse_args()
log=a.log.read_text(encoding='utf8',errors='replace')
states=[(int(s),int(f),float(t)) for s,f,t in re.findall(r'STPS WIN STATE state=(\d+) frame=(\d+) t=([\d.]+)',log)]
assert [s for s,_,_ in states]==[1,4,8,0], 'Wrong gameplay / notice / final scene / menu flow'
notice=states[2][2]-states[1][2]
held=states[3][2]-states[2][2]
assert 4.95<=notice<=5.15 and held>=2.45, 'Incorrect win timing'
assert 'STPS VICTORY WORLD valid=1' in log and 'STPS SMOKE PASS victory' in log
vram=int(re.search(r'STPS TEXTURES scene=victory bank=1 vram=(\d+)',log)[1])
cache=re.search(r'STPS CACHE key=100 bytes=(\d+) capacity=(\d+)',log)
assert int(cache[1])<int(cache[2]) and vram<=4*1024*1024, 'GS memory overflow'
geometry=[int(n) for n in re.search(r'STPS VICTORY GEOMETRY ground=(\d+) models=(\d+) trees=(\d+) grass=(\d+) total=(\d+)',log).groups()]
assert all(n>0 for n in geometry) and sum(geometry[:4])==geometry[4]<20000, 'Missing or truncated 3D scene'
shots={}
for file in sorted(a.capture.glob('*.png')):
    m=re.search(r'_f(\d+)_',file.name)
    if m:shots[int(m[1])]=file
images={}
for name,start,end in [('win-notice',states[1][1],states[2][1]),('win-scene',states[2][1],states[3][1])]:
    frames=[f for f in shots if start+16<f<end-8]
    assert frames, f'Missing GS capture of {name}'
    image=Image.open(shots[frames[len(frames)//2]]).convert('RGB').resize((512,448))
    image.save(a.capture/(name+'.png'))
    images[name]=np.asarray(image).astype(int)
final=images['win-scene'];first=images['win-notice']
text_white=int((final[20:125,140:370].min(axis=2)>210).sum())
model_gray=int(((final[132:270,220:290].max(axis=2)-final[132:270,220:290].min(axis=2)<35)&(final[132:270,220:290].mean(axis=2)<100)).sum())
green=int(((final[270:448,:,1]>final[270:448,:,0]+10)&(final[270:448,:,1]>final[270:448,:,2]+10)).sum())
assert text_white>300 and model_gray>400 and green>10000, 'Missing victory text, character, or landscape'
assert (np.abs(first-final).max(axis=2)>30).sum()>40000, 'Win notice did not change into the final scene'
report=dict(passed=True,notice_seconds=notice,final_held_seconds=held,vram_bytes=vram,
            cache_bytes=int(cache[1]),cache_capacity=int(cache[2]),geometry=geometry,
            white_title_pixels=text_white,gray_character_pixels=model_gray,green_landscape_pixels=green,
            elf_sha256=json.loads(a.snapshot.read_text(encoding='utf-8-sig'))['Hash'])
(a.capture/'victory-check.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
