"""Inspect original house colliders, flashlight settings and terrain grass modes."""
import json
import struct
import numpy as np
from export_assets import Exporter,trs
from UnityPy.helpers.MeshHelper import MeshHandler

e=Exporter('original','ps2/qa')
f=e.load('Assets/SingleplayerMorning.unity')
objects={o.path_id:o.read_typetree() for o in f[0].objects}
types={o.path_id:o.type.name for o in f[0].objects}
transforms={d['m_GameObject']['m_PathID']:p for p,d in objects.items() if types[p]=='Transform'}
matrices={}
def matrix(p):
    if not p:return np.eye(4)
    if p not in matrices:matrices[p]=matrix(objects[p]['m_Father']['m_PathID'])@trs(objects[p])
    return matrices[p]
def hierarchy(go):
    p=transforms[go];names=[];active=True
    while p:
        d=objects[objects[p]['m_GameObject']['m_PathID']]
        names.append(d['m_Name']);active=active and d.get('m_IsActive',True)
        p=objects[p]['m_Father']['m_PathID']
    return '/'.join(reversed(names)),active
rows=[]
filters={d['m_GameObject']['m_PathID']:d for p,d in objects.items() if types[p]=='MeshFilter'}
floor_faces=[]
for p,d in objects.items():
    if not types[p].endswith('Collider'):continue
    go=d['m_GameObject']['m_PathID'];name,active=hierarchy(go)
    if 'house' not in name.lower():continue
    row=dict(type=types[p],name=name,active=active,data=d,matrix=matrix(transforms[go]).tolist())
    if types[p]=='MeshCollider':
        mesh=e.resolve(f,d['m_Mesh'])
        if mesh:
            h=MeshHandler(mesh.read());h.process();v=np.asarray(h.m_Vertices)[:,:3]
            v=(matrix(transforms[go])@np.column_stack((v,np.ones(len(v)))).T).T[:,:3]
            row['bounds']=[v.min(axis=0).tolist(),v.max(axis=0).tolist()]
            row['triangles']=sum(len(t) for t in h.get_triangles())
            row['filter_mesh']=filters.get(go,{}).get('m_Mesh')
            for triangles in h.get_triangles():floor_faces.extend(v[np.asarray(triangles,dtype=int)].tolist())
        elif d['m_Mesh']['m_FileID']:row['external']=f[1].externals[d['m_Mesh']['m_FileID']-1].path
    rows.append(row)
td=next(d for d in objects.values() if 'm_TerrainData' in d)
terrain=e.resolve(f,td['m_TerrainData']).read_typetree()
grass=terrain['m_DetailDatabase']['m_DetailPrototypes']
pf=e.load('Assets/PlayerMultiplayer.prefab')
lights=[o.read_typetree() for o in pf[0].objects if o.type.name=='Light']
(e.out/'house-source-audit.json').write_text(json.dumps(dict(colliders=rows,grass=grass,lights=lights),indent=2))
for row in rows:print(row['type'],row['name'],'enabled=',row['data']['m_Enabled'],'active=',row['active'],row.get('bounds',row.get('external','')))
print('Grass',grass)
print('Lights',[(d['m_Color'],d['m_Intensity'],d['m_Range'],d['m_Type']) for d in lights])
faces=np.asarray(floor_faces)
a,b,c=faces[:,0],faces[:,1],faces[:,2]
den=(b[:,2]-c[:,2])*(a[:,0]-c[:,0])+(c[:,0]-b[:,0])*(a[:,2]-c[:,2])
normal=np.cross(b-a,c-a);horizontal=np.abs(normal[:,1])>=np.linalg.norm(normal,axis=1)*.7
for x,z in [(502.25,759.4),(501.7,744),(502.25,755),(502.25,760),(505,759.4)]:
    safe=np.abs(den)>1e-5
    u=np.zeros(len(a));v=u.copy()
    u[safe]=((b[safe,2]-c[safe,2])*(x-c[safe,0])+(c[safe,0]-b[safe,0])*(z-c[safe,2]))/den[safe]
    v[safe]=((c[safe,2]-a[safe,2])*(x-c[safe,0])+(a[safe,0]-c[safe,0])*(z-c[safe,2]))/den[safe]
    y=a[:,1]*u+b[:,1]*v+c[:,1]*(1-u-v)
    print('Vertical hits',x,z,sorted(set(np.round(y[safe&(u>=-1e-4)&(v>=-1e-4)&(u+v<=1.0001)],4))))

def floor_hits(faces,points,maximum=2.4,slope=.7):
    a,b,c=faces[:,0],faces[:,1],faces[:,2]
    normal=np.cross(b-a,c-a)
    valid=(np.abs(normal[:,1])>=np.linalg.norm(normal,axis=1)*slope)&(faces[:,:,1].min(axis=1)<maximum)
    a,b,c=a[valid],b[valid],c[valid]
    den=(b[:,2]-c[:,2])*(a[:,0]-c[:,0])+(c[:,0]-b[:,0])*(a[:,2]-c[:,2])
    results=[]
    for x,z in points:
        safe=np.abs(den)>1e-5;u=np.zeros(len(a));v=u.copy()
        u[safe]=((b[safe,2]-c[safe,2])*(x-c[safe,0])+(c[safe,0]-b[safe,0])*(z-c[safe,2]))/den[safe]
        v[safe]=((c[safe,2]-a[safe,2])*(x-c[safe,0])+(a[safe,0]-c[safe,0])*(z-c[safe,2]))/den[safe]
        y=a[:,1]*u+b[:,1]*v+c[:,1]*(1-u-v)
        mask=safe&(u>=0)&(v>=0)&(u+v<=1)&(y<maximum)
        results.append(y[mask].max() if mask.any() else -1000)
    return np.asarray(results)

blob=(e.out.parent/'generated/world.bin').read_bytes();header=struct.unpack_from('<20I',blob)
native=[]
for i in range(header[2]):
    m=struct.unpack_from('<7I9f',blob,header[8]+i*64)
    if not m[5]&1 or m[5]&14:continue
    if m[7]>540 or m[10]<470 or m[9]>795 or m[12]<725:continue
    vertices=np.frombuffer(blob,dtype='<f4',count=m[0]*8,offset=m[2]).reshape(-1,8)
    triangles=np.frombuffer(blob,dtype='<u2',count=m[1]*3,offset=m[3]).reshape(-1,3)
    native.extend(vertices[triangles,:3].tolist())
points=[(x,z) for z in np.arange(734.25,786,.5) for x in np.arange(475.25,530,.5)]
raw=floor_hits(faces,points);port=floor_hits(np.asarray(native),points)
holes=(raw>0)&(port<raw-.05)
report=dict(points=len(points),missing_floor_samples=int(holes.sum()),
            failures=[dict(x=x,z=z,original=float(r),port=float(p)) for (x,z),r,p in zip(points,raw,port) if r>0 and p<r-.05][:100])
(e.out/'house-floor-audit.json').write_text(json.dumps(report,indent=2))
print('Floor comparison',report['points'],'points, gaps=',report['missing_floor_samples'],report['failures'][:10])
roof_points=[(502.25+r,759.4) for r in (0,1,2,3,4,5,6,7,8,10,12,14,16,18,20)]
source_roof=floor_hits(faces,roof_points,14,.001)
walkable=floor_hits(np.asarray(native),roof_points,14,.7)
print('Roof support',[(p,float(r),float(w)) for p,r,w in zip(roof_points,source_roof,walkable)])
