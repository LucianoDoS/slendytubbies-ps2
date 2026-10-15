import json,sys
from pathlib import Path
import numpy as np
sys.path.insert(0,str(Path(__file__).parent))
from export_assets import Exporter,trs
e=Exporter('original','ps2/qa')
report={}
for scene in ('SingleplayerMorning','SingleplayerNight'):
 f=e.load('Assets/'+scene+'.unity');o={x.path_id:x.read_typetree() for x in f[0].objects};t={x.path_id:x.type.name for x in f[0].objects}
 tr={d['m_GameObject']['m_PathID']:p for p,d in o.items() if t[p]=='Transform'}
 def matrix(p):
  if not p:return np.eye(4)
  return matrix(o[p]['m_Father']['m_PathID'])@trs(o[p])
 td=next(d for p,d in o.items() if t[p]=='MonoBehaviour' and 'm_TerrainData' in d)
 terrain=e.resolve(f,td['m_TerrainData']);tf=e.file_of(terrain);data=terrain.read_typetree();db=data['m_DetailDatabase']
 entries=[]
 for p,d in o.items():
  if t[p]=='GameObject' and (d.get('m_TagString')=='Paper' or any(n in d['m_Name'].lower() for n in ('tree','dead'))):entries.append({'name':d['m_Name'],'position':matrix(tr[p])[:3,3].tolist(),'components':[t[x[1]['m_PathID']] for x in d['m_Component']]})
 report[scene]={'terrain_file':str(terrain.assets_file.name),'fog':next(d for p,d in o.items() if t[p]=='RenderSettings'),'tree_count':len(db['m_TreeInstances']),'prototypes':db['m_TreePrototypes'],'splats':data['m_SplatDatabase']['m_Splats'],'details':db['m_DetailPrototypes'],'patch_count':db['m_PatchCount'],'patch_samples':db['m_PatchSamples'],'patch_first':next((p for p in db['m_Patches'] if p.get('layerIndices')),None),'objects':entries,'ai':next(d for p,d in o.items() if t[p]=='MonoBehaviour' and 'sprintspeed' in d)}
for path in ('Assets/collectedinfo/1-10.prefab','Assets/PlayerMultiplayer.prefab'):
 f=e.load(path);report[path]=[{'type':x.type.name,'data':x.read_typetree()} for x in f[0].objects if x.type.name in ('GUITexture','MonoBehaviour')]
for prototype in db['m_TreePrototypes']:
 obj=e.resolve(tf,prototype['prefab']);pf=e.file_of(obj);report[str(obj.assets_file.name)]=[{'type':x.type.name,'data':x.read_typetree()} for x in pf[0].objects if 'Collider' in x.type.name]
Path('ps2/qa/fidelity-audit.json').write_text(json.dumps(report,indent=2,default=str))
print('Saved source audit')

