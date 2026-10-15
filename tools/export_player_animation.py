"""Sample the original automatically playing walking clip for first-person meshes."""
import json
import struct
from pathlib import Path
import numpy as np
from export_assets import Exporter
from export_player_trees import geometry

def sample(curve,time,channels):
    keys=curve['m_Curve']
    if time<=keys[0]['time']:return {k:keys[0]['value'][k] for k in channels}
    if time>=keys[-1]['time']:return {k:keys[-1]['value'][k] for k in channels}
    a,b=next((a,b) for a,b in zip(keys,keys[1:]) if a['time']<=time<=b['time'])
    dt=b['time']-a['time'];t=(time-a['time'])/dt
    return {k:(2*t**3-3*t*t+1)*a['value'][k]+(t**3-2*t*t+t)*dt*a['outSlope'][k]
              +(-2*t**3+3*t*t)*b['value'][k]+(t**3-t*t)*dt*b['inSlope'][k] for k in channels}

def export_walk(source='original',output='ps2/generated'):
    e=Exporter(source,output);pf=e.load('Assets/PlayerMultiplayer.prefab')
    objects={o.path_id:o.read_typetree() for o in pf[0].objects};types={o.path_id:o.type.name for o in pf[0].objects}
    transforms={d['m_GameObject']['m_PathID']:p for p,d in objects.items() if types[p]=='Transform'}
    component=next(d for p,d in objects.items() if types[p]=='Animation' and d['m_PlayAutomatically'])
    clip=e.resolve(pf,component['m_Animation']).read_typetree()
    root=transforms[component['m_GameObject']['m_PathID']]
    paths={}
    def path(p):
        if p==root:return ''
        parent=objects[p]['m_Father']['m_PathID']
        if not parent:return None
        prefix=path(parent)
        if prefix is None:return None
        name=objects[objects[p]['m_GameObject']['m_PathID']]['m_Name']
        return prefix+'/'+name if prefix else name
    for p in transforms.values():
        name=path(p)
        if name is not None:paths[name]=p
    duration=max(k['time'] for category in ('m_RotationCurves','m_PositionCurves','m_ScaleCurves') for c in clip[category] for k in c['curve']['m_Curve'])
    base=list(geometry(e,pf,'Camera'))
    lookup={}
    for index,(vertices,triangles,texture,color,name) in enumerate(base):
        for v,row in enumerate(vertices):lookup.setdefault((name,row.tobytes()),(index,v))
    report=json.loads((e.out/'export_report.json').read_text());blob=bytearray((e.out/'world.bin').read_bytes())
    header=struct.unpack_from('<20I',blob);mapped=[];offsets=[];parts=[];descriptors=[];total=0
    for i,name in enumerate(report['mesh_names']):
        m=struct.unpack_from('<7I9f',blob,header[8]+i*64)
        if not m[5]&64:continue
        vertices=np.frombuffer(blob,dtype='<f4',count=m[0]*8,offset=m[2]).reshape(-1,8)
        mapped.append([lookup[(name.split('/')[-1],v.tobytes())] for v in vertices])
        parts.append(1 if 'legmesh' in name else 2 if name.endswith('/default') else 0);descriptors.append((i,m[0]))
        struct.pack_into('<I',blob,header[8]+i*64+24,len(offsets));offsets.append(total);total+=m[0]
    frames=16;data=[]
    for frame in range(frames):
        overrides={};time=frame*duration/frames
        for category,field,channels in [('m_RotationCurves','m_LocalRotation','xyzw'),('m_PositionCurves','m_LocalPosition','xyz'),('m_ScaleCurves','m_LocalScale','xyz')]:
            for curve in clip[category]:
                p=paths.get(curve['path'])
                if p is None:continue
                d=overrides.setdefault(p,objects[p].copy());value=sample(curve['curve'],time,channels)
                if channels=='xyzw':
                    length=np.sqrt(sum(v*v for v in value.values()));value={k:v/length for k,v in value.items()}
                d[field]=value
        posed=list(geometry(e,pf,'Camera',local_transforms=overrides))
        data.append(np.asarray([list(posed[g][0][v,:3])+list(posed[g][0][v,5:8]) for mesh in mapped for g,v in mesh],dtype='<f4'))
    animation=np.asarray(data,dtype='<f4');(e.out/'player_walk.bin').write_bytes(animation.tobytes())
    for (i,count),offset in zip(descriptors,offsets):
        xyz=animation[:,offset:offset+count,:3]
        struct.pack_into('<6f',blob,header[8]+i*64+28,*xyz.min(axis=(0,1)),*xyz.max(axis=(0,1)))
    (e.out/'world.bin').write_bytes(blob)
    (e.out/'player_walk.h').write_text('/* Original walking clip sampled with skin weights and bind poses. */\n'
        +f'#define VIEW_WALK_FRAMES {frames}\n#define VIEW_WALK_VERTICES {total}\n'
        +f'static const float view_walk_duration={duration:.9f}f;\n'
        +'static const unsigned view_walk_offsets[]={'+','.join(map(str,offsets))+'};\n')
    with (e.out/'player_walk.h').open('a') as f:f.write('static const unsigned view_walk_parts[]={'+','.join(map(str,parts))+'};\n')
    result=dict(source='Assets/multiplayertubby/multiplayertubby.FBX',clip=clip['m_Name'],duration=duration,frames=frames,
                vertices=total,bytes=animation.nbytes,maximum_vertex_motion=float(np.max(np.linalg.norm(animation[:,:,:3]-animation[0,:,:3],axis=2))))
    (e.out/'player_walk.json').write_text(json.dumps(result,indent=2));print(json.dumps(result))
if __name__=='__main__':export_walk()
