"""Inspect the imported player animation, camera and enemy physical components."""
import json
from export_assets import Exporter
e=Exporter('original','ps2/qa')
pf=e.load('Assets/PlayerMultiplayer.prefab')
objs={o.path_id:o.read_typetree() for o in pf[0].objects};types={o.path_id:o.type.name for o in pf[0].objects}
rows=[]
for p,d in objs.items():
    name=objs[d['m_GameObject']['m_PathID']]['m_Name'] if 'm_GameObject' in d else d.get('m_Name','')
    if types[p] in ('Animation','Camera') or (types[p]=='MonoBehaviour' and ('minimumY' in d or 'Player' in d)):
        rows.append(dict(type=types[p],name=name,data=d))
        print(types[p],name,d)
    if types[p]=='SkinnedMeshRenderer' and name in ('leftlegmesh','rightlegmesh','default'):
        mesh=e.resolve(pf,d['m_Mesh']).read_typetree()
        weights=mesh['m_Skin']
        print('SKIN',name,'bones=',[objs[b['m_PathID']]['m_GameObject'] for b in d['m_Bones']], 'indices=',sorted(set(w['boneIndex[0]'] for w in weights)),'weights=',sorted(set(w['weight[0]'] for w in weights)))
fbx=e.load('Assets/multiplayertubby/multiplayertubby.FBX',True)
for o in fbx[0].objects:
    if o.type.name=='AnimationClip':
        d=o.read_typetree();print('CLIP',o.path_id,d['m_Name'],d.keys(), 'rotation',[(c['path'],len(c['curve']['m_Curve'])) for c in d['m_RotationCurves']], 'position',[c['path'] for c in d['m_PositionCurves']])
        (e.out/('player-animation-'+str(o.path_id)+'.json')).write_text(json.dumps(d,indent=2))
f=e.load('Assets/SingleplayerMorning.unity');objs={o.path_id:o.read_typetree() for o in f[0].objects};types={o.path_id:o.type.name for o in f[0].objects}
transforms={d['m_GameObject']['m_PathID']:p for p,d in objs.items() if types[p]=='Transform'}
for p,d in objs.items():
    if not types[p].endswith('Collider'):continue
    go=d['m_GameObject']['m_PathID'];t=transforms.get(go);chain=[]
    while t:
        chain.append(objs[objs[t]['m_GameObject']['m_PathID']]['m_Name']);t=objs[t]['m_Father']['m_PathID']
    if 'Tinky 1' in chain:print('ENEMY',types[p],d,'name',chain)
(e.out/'player-source-audit.json').write_text(json.dumps(rows,indent=2))
