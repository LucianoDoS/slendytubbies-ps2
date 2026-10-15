"""Compare actual GS frames from a complete or game-only native benchmark."""
import argparse,json,re
from pathlib import Path
import numpy as np
from PIL import Image

p=argparse.ArgumentParser();p.add_argument('capture',type=Path);p.add_argument('log',type=Path);a=p.parse_args()
log=a.log.read_text(encoding='utf8',errors='replace');assert 'STPS SMOKE PASS benchmark' in log
starts=[(name,int(frame)) for name,frame in re.findall(r'STPS BENCH START scene=(\S+) frame=(\d+)',log)]
shots={}
for file in sorted(a.capture.glob('*.png')):
    match=re.search(r'_f(\d+)_',file.name)
    if match:shots[int(match[1])]=file
def frame(name):
    i=next(i for i,(n,_) in enumerate(starts) if n==name);begin=starts[i][1]
    end=starts[i+1][1] if i+1<len(starts) else max(shots)
    candidates=[n for n in shots if begin+15<n<end-5]
    assert candidates,f'Missing capture: {name}'
    source=shots[candidates[len(candidates)//2]]
    image=Image.open(source).convert('RGB').resize((512,448))
    image.save(a.capture/(name+'.png'))
    return np.asarray(image).astype(float)
tree=frame('custard-tree');pickup=frame('pickup-notice');dead=frame('death-scene');grass=frame('grass-close');bare=frame('grass-off')
fog_white=float(tree[120:230,:100].mean());assert fog_white>185
trunk_dark=int((tree[30:275,270:400].mean(axis=2)<190).sum());assert trunk_dark>2000
notice_changed=int((np.abs(tree[395:438,155:360]-pickup[395:438,155:360]).max(axis=2)>12).sum());assert notice_changed>200
red=int(((dead[:,:,0]>dead[:,:,1]+35)&(dead[:,:,0]>dead[:,:,2]+35)).sum());assert red>600
background=float(np.abs(dead[300:365]-tree[300:365]).mean());assert background<3
grass_changed=int((np.abs(grass[220:448]-bare[220:448]).max(axis=2)>12).sum());assert grass_changed>1000
report=dict(fog_region_mean=fog_white,trunk_dark_pixels=trunk_dark,pickup_changed_pixels=notice_changed,
            death_red_pixels=red,death_background_mean_difference=background,grass_changed_pixels=grass_changed,passed=True,
            elf_sha256=json.loads((a.capture/'performance.json').read_text())['elf_sha256'])
if {'enemy-visible','enemy-close'}<=set(name for name,_ in starts):
    far=frame('enemy-visible');close=frame('enemy-close')
    far_corner=float(far[2:8,2:18].mean());close_corner=float(close[2:8,2:18].mean())
    assert far_corner>80 and close_corner<far_corner*.4,'Damage border must appear only in the close encounter'
    report.update(distant_corner_mean=far_corner,attacking_corner_mean=close_corner)
(a.capture/'fidelity-check.json').write_text(json.dumps(report,indent=2))
print(json.dumps(report))
