"""Preserve legacy menu emitter positions, textures and colour ramps."""
import numpy as np


def export_particles(exporter,file,objects,types,pose,origin):
    renderers={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='ParticleRenderer'}
    animators={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='ParticleAnimator'}
    rows=[]
    def vec(d):return np.array([d[k] for k in ('x','y','z')])
    def array(values):return '{'+','.join(f'{float(v):.9f}f' for v in values)+'}'
    for pid,emitter in objects.items():
        if types[pid]!='EllipsoidParticleEmitter' or not emitter['m_Enabled'] or not emitter['m_Emit']:continue
        go=emitter['m_GameObject']['m_PathID'];renderer=renderers[go];animator=animators[go]
        material=exporter.resolve(file,renderer['m_Materials'][0]);md=material.read_typetree()
        texture,*_=exporter.material(material)
        tint=next((v for k,v in md['m_SavedProperties']['m_Colors'] if k['name']=='_TintColor'),dict(r=1,g=1,b=1,a=1))
        transform=pose(go);scale=np.linalg.norm(transform[:3,:3],axis=0)
        velocity=vec(emitter['worldVelocity'])+transform[:3,:3]@vec(emitter['localVelocity'])
        colours=[]
        for i in range(5):
            rgba=animator[f'colorAnimation[{i}]']['rgba']
            colours.append([((rgba>>(k*8))&255)/255*tint[key] for k,key in enumerate(('r','g','b','a'))])
        rows.append('{'+','.join([array(transform[:3,3]-origin),array(vec(emitter['m_Ellipsoid'])*scale),
                    array(velocity),array(vec(animator['force'])),array([emitter['minSize']*scale.mean(),emitter['maxSize']*scale.mean()]),
                    f"{(emitter['minEnergy']+emitter['maxEnergy'])/2:.9f}f",f"{animator['sizeGrow']:.9f}f",
                    '{'+','.join(array(c) for c in colours)+'}',str(texture),str(int(md['m_Name'].startswith('Flame')))])+'}')
    return '\n'.join(['typedef struct {float position[3],spread[3],velocity[3],force[3],size[2],life,grow,color[5][4];unsigned texture,additive;} MenuEmitter;',
                       'static const MenuEmitter menu_emitters[]={'+',\n'.join(rows)+'};'])
