"""Check real GS captures of the stationary walking legs and grass, plus roof traversal."""
import argparse,json,re
from pathlib import Path
import numpy as np
from PIL import Image
p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('log',type=Path);p.add_argument('snapshot',type=Path);a=p.parse_args()
log=a.log.read_text(encoding='utf8',errors='replace');assert 'STPS SMOKE PASS surfaces' in log
roof=[(int(r),int(c),float(h),float(d)) for r,c,h,d in re.findall(r'STPS ROOF route=(\d+) centre=(\d+) minimum=([\d.]+) exit=([\d.]+)',log)]
assert len(roof)==4 and all(c==1 and h>6.5 and d>12 for r,c,h,d in roof)
starts=[(name,int(frame)) for name,frame in re.findall(r'STPS SURFACE START scene=(\S+) frame=(\d+)',log)]
shots={}
for file in sorted(a.capture.glob('*.png')):
    m=re.search(r'_f(\d+)_',file.name)
    if m:shots[int(m[1])]=file
# GS counts the boot/setup presents too; native game frames start later.
# The final capture interval bounds the offset to eight frames.
native_end=int(re.search(r'STPS SMOKE PASS surfaces frames=(\d+)',log)[1])
offset=max(shots)-native_end+4
def select(name):
    i=next(i for i,(n,f) in enumerate(starts) if n==name);begin=starts[i][1]+offset;end=starts[i+1][1]+offset if i+1<len(starts) else max(shots)+1
    frames=[f for f in shots if begin+8<f<end-8];assert len(frames)>=3
    return frames
def read(frame,name):
    im=Image.open(shots[frame]).convert('RGB').resize((512,448));im.save(a.capture/(name+'.png'));return np.asarray(im).astype(int)
body=select('body-idle');first=read(body[0],'body-idle');later=read(body[2],'body-idle-later')
roi=first[330:448,125:365];other=later[330:448,125:365]
white=(roi.min(axis=2)>120)&(roi.max(axis=2)-roi.min(axis=2)<30)
changed=np.abs(roi-other).max(axis=2)>18
assert white.sum()>300 and (white&changed).sum()>120, 'Stationary legs are missing or not animating'
assert np.abs(first[100:260,30:480]-later[100:260,30:480]).mean()<1, 'Camera/background moved during idle animation test'
for name in ('grass-front','grass-side'):
    read(select(name)[len(select(name))//2],name)
read(select('roof')[len(select('roof'))//2],'roof-centre')
result=dict(passed=True,roof=roof,white_leg_pixels=int(white.sum()),moving_leg_pixels=int((white&changed).sum()),
            elf_sha256=json.loads(a.snapshot.read_text(encoding='utf-8-sig'))['Hash'])
(a.capture/'surfaces-check.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
