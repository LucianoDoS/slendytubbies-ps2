"""Source pickup notices, tree capsules and sparse terrain detail densities."""
import numpy as np
from PIL import Image

def export_fidelity(e,tf,terrain):
    db=terrain['m_DetailDatabase'];capsules=[]
    for p in db['m_TreePrototypes']:
        pf=e.file_of(e.resolve(tf,p['prefab']))
        c=next(o.read_typetree() for o in pf[0].objects if o.type.name=='CapsuleCollider')
        capsules.append([c['m_Center'][k] for k in ('x','y','z')]+[c['m_Radius'],c['m_Height']])
    pickups=[]
    for n in range(1,10):
        im=Image.open(e.root/f'Assets/collectedinfo/{n}-10.png').convert('RGBA');box=im.getbbox()
        texture=e.image_texture(f'pickup-{n}',im.crop(box),256,16)
        pickups.append([texture,box[0]/im.width,box[1]/im.height,(box[2]-box[0])/im.width,(box[3]-box[1])/im.height])
    pf=e.load('Assets/IffSeeObject.prefab')
    d=next(o.read_typetree() for o in pf[0].objects if o.type.name=='MonoBehaviour' and 'yourtexture' in o.read_typetree())
    border=e.texture(e.resolve(pf,d['yourtexture']),64)
    e.grass_texture=e.texture(e.resolve(tf,db['m_DetailPrototypes'][0]['prototypeTexture']),64)
    e.grass_index=np.zeros((128,128),dtype='<u2');raw=bytearray(64) # index zero means empty
    for i,p in enumerate(db['m_Patches']):
        if not p['layerIndices']:continue
        values=np.asarray(p['numberOfObjects'],dtype=np.uint8).reshape(-1,8,8)
        packed=np.zeros((8,8),dtype=np.uint8)
        for layer,v in zip(p['layerIndices'],values):
            if layer<2:packed|=np.minimum(v,15)<<int(layer*4)
        if not packed.any():continue
        # Detail patches use row-major order, unlike serialized height samples.
        e.grass_index[i//128,i%128]=len(raw)//64
        raw.extend(packed.tobytes())
    e.grass_patches=bytes(raw)
    from export_assets import xyz,trs
    scene=e.load('Assets/SingleplayerMorning.unity');objects={o.path_id:o.read_typetree() for o in scene[0].objects};types={o.path_id:o.type.name for o in scene[0].objects}
    enemy=next(p for p,d in objects.items() if types[p]=='GameObject' and d['m_Name']=='Tinky 1')
    collider=next(d for p,d in objects.items() if types[p]=='CapsuleCollider' and d['m_GameObject']['m_PathID']==enemy)
    transform=next(d for p,d in objects.items() if types[p]=='Transform' and d['m_GameObject']['m_PathID']==enemy)
    scale=xyz(transform['m_LocalScale']);enemy_capsule=list(xyz(collider['m_Center'])*scale)+[collider['m_Radius']*max(abs(scale[0]),abs(scale[2])),collider['m_Height']*abs(scale[1])]
    roof=[m[0] for m in e.meshes if m[-1]=='Teletubby house/Mesh01'];roof_y=float(np.concatenate(roof)[:,1].max())
    cross=np.concatenate([m[0] for m in e.meshes if m[-1]=='Teletubby house/ceiling_02'])
    centre_x=float(cross[:,0].mean());centre_z=float(np.concatenate([m[0] for m in e.meshes if m[-1]=='Teletubby house/Teletubby house'])[:,2].mean())
    roof_support=[centre_x,centre_z,float(np.abs(cross[:,2]-centre_z).max()),roof_y]
    text=['/* Source capsules, pickup GUI bounds, and sparse grass. */',
          'static const float source_tree_capsules[3][5]={'+','.join('{'+','.join(f'{v:.9f}f' for v in row)+'}' for row in capsules)+'};',
          'static const float source_pickups[9][5]={'+','.join('{'+','.join(f'{v:.9f}f' for v in row)+'}' for row in pickups)+'};',
          'static const float source_grass[2][10]={'+','.join('{'+','.join(f'{v:.9f}f' for v in ([p[k] for k in ('minWidth','maxWidth','minHeight','maxHeight')]+[p[color][k] for color in ('healthyColor','dryColor') for k in ('r','g','b')]))+'}' for p in db['m_DetailPrototypes'])+'};',
          'static const unsigned source_grass_modes[2]={'+','.join(str(p['renderMode']) for p in db['m_DetailPrototypes'])+'};',
          'static const float source_enemy_capsule[5]={'+','.join(f'{v:.9f}f' for v in enemy_capsule)+'};',
          'static const float source_roof_support[4]={'+','.join(f'{v:.9f}f' for v in roof_support)+'};',
          f'static const unsigned source_damage_border={border};']
    (e.out/'fidelity.h').write_text('\n'.join(text))
    print(f'Sparse grass: {len(raw)+e.grass_index.nbytes} bytes')
