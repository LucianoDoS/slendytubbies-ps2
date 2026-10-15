"""Convert the original Unity 3.x serialized assets into a bounded PS2 data blob.

The package's metaData files contain the actual Unity import cache, including
meshes, textures and skin bind poses. No Unity editor or FBX reimport is needed.
"""
import argparse
import audioop
import io
import json
import math
import struct
import wave
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw
import UnityPy
from UnityPy.helpers.MeshHelper import MeshHandler
from export_player_trees import export_models
from export_visuals import export_visuals
from optimize_meshes import planar_subdivisions
from font_atlas import debug_font

COLLIDE, ENEMY, DOOR, HOOVER, COLLECT = 1, 2, 4, 8, 16

def xyz(d):
    return np.array([d['x'], d['y'], d['z']], dtype=np.float64)

def trs(d):
    q = d['m_LocalRotation']; x, y, z, w = (q[k] for k in ('x','y','z','w'))
    m = np.eye(4)
    m[:3,:3] = np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                          [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                          [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]]) @ np.diag(xyz(d['m_LocalScale']))
    m[:3,3] = xyz(d['m_LocalPosition'])
    return m

class Exporter:
    def __init__(self, source, destination):
        self.root = Path(source)
        self.out = Path(destination); self.out.mkdir(parents=True, exist_ok=True)
        self.manifest = json.loads((self.root/'manifest.json').read_text(encoding='utf-8'))
        self.by_guid = {e['guid']:e['path'] for e in self.manifest}
        self.by_path = {e['path'].lower():e['path'] for e in self.manifest}
        self.cache = {}; self.textures = []; self.texture_ids = {}; self.meshes = []
        self.warnings = []; self.bounds = []; self.audio = []; self.audio_codes={};self.collision_meshes = []

    def load(self, path, imported=False):
        path = self.by_path.get(path.lower(), path)
        p = self.root/path
        if imported: p = Path(str(p)+'.meta')
        key = str(p)
        if key not in self.cache:
            env = UnityPy.load(str(p))
            self.cache[key] = (env, list(env.assets)[0], {o.path_id:o for o in env.objects})
        return self.cache[key]

    def resolve(self, file, ptr):
        if not ptr or not ptr['m_PathID']: return None
        if ptr['m_FileID'] == 0: return file[2].get(ptr['m_PathID'])
        external = file[1].externals[ptr['m_FileID']-1]
        path = external.path.replace('\\','/')
        # Unity 3.x GUID-only references store the two nibbles of each byte
        # in the reverse order to the .unitypackage directory names.
        if not path:
            guid=''.join(f'{b&15:x}{b>>4:x}' for b in external.guid)
            path=self.by_guid.get(guid)
            if not path:
                self.warnings.append(f'Missing GUID {guid}'); return None
            return self.load(path,external.type==3)[2].get(ptr['m_PathID'])
        imported = path.lower().startswith('library/')
        if imported:
            guid = path.rsplit('/',1)[-1]
            if guid not in self.by_guid:
                self.warnings.append(f'Missing external {path}'); return None
            path = self.by_guid[guid]
        try:
            target = self.load(path, imported)
            return target[2].get(ptr['m_PathID'])
        except (FileNotFoundError, IndexError, ValueError) as error:
            self.warnings.append(f'Cannot resolve {path}: {error}'); return None

    def file_of(self, obj):
        return next(f for f in self.cache.values() if f[1] is obj.assets_file)

    def image_texture(self, key, im, size=64, height=None):
        if key in self.texture_ids: return self.texture_ids[key]
        height=height or size
        im = im.convert('RGBA').resize((size,height),Image.Resampling.LANCZOS)
        pix = np.asarray(im).copy(); pix[:,:,3] = (pix[:,:,3].astype(np.uint16)*128//255).astype(np.uint8)
        i = len(self.textures); self.texture_ids[key] = i
        self.textures.append((size,height,pix.tobytes(),key))
        return i

    def texture(self, obj, size=64, opaque=False):
        if obj is None: return 0
        source=next(k for k,f in self.cache.items() if f[1] is obj.assets_file)
        foliage='branches' in source.lower() or 'terrain grass' in source.lower()
        if foliage and size==64:size=256 if 'scotspinebranches' in source.lower() else 128
        key = f'{source}:{obj.path_id}:{size}'+(':opaque' if opaque else '')
        if key in self.texture_ids: return self.texture_ids[key]
        try:
            image=obj.read().image.convert('RGBA')
            # Skybox shaders ignore the imported alpha channel. Set opacity
            # before resampling so zero-alpha RGB is not discarded by Pillow.
            if opaque:image=image.convert('RGB').convert('RGBA')
            if foliage:
                # Keep the rectangular pine atlas and the source cutout coverage.
                height=max(32,int(size*image.height/image.width))
                reduced=image.resize((size,height),Image.Resampling.LANCZOS)
                pixels=np.asarray(reduced).copy()
                coverage=np.mean(np.asarray(image)[:,:,3]>=128)
                low,high=.25,4.0
                for _ in range(16):
                    gain=(low+high)*.5
                    if np.mean(pixels[:,:,3]*gain>=128)<coverage:low=gain
                    else:high=gain
                pixels[:,:,3]=np.minimum(255,pixels[:,:,3].astype(float)*high).astype('uint8')
                return self.image_texture(key,Image.fromarray(pixels),size,height)
            return self.image_texture(key,image,size)
        except Exception as error:
            self.warnings.append(f'Texture decode {key}: {error}'); return 0

    def material(self, obj):
        if obj is None: return 0, (1,1), (0,0), (1,1,1)
        f = self.file_of(obj)
        try:
            d = obj.read_typetree()
        except ValueError as error:
            key=next(k for k,v in self.cache.items() if v is f)
            try:
                cached=self.load(str(Path(key).relative_to(self.root)).replace('\\','/'),True)
                obj=cached[2][obj.path_id]; f=cached; d=obj.read_typetree()
            except Exception:
                self.warnings.append(f'Material properties unsupported; using original diffuse texture: {key}')
                for i in range(len(f[1].externals)):
                    candidate=self.resolve(f,{'m_FileID':i+1,'m_PathID':2800000})
                    if candidate is not None and candidate.type.name=='Texture2D':
                        return self.texture(candidate),(1,1),(0,0),(1,1,1)
                return 0,(1,1),(0,0),(1,1,1)
        texture = 0; scale=(1,1); offset=(0,0); color=(1,1,1)
        for k,v in d.get('m_SavedProperties',{}).get('m_TexEnvs',[]):
            if k['name'] == '_MainTex':
                texture=self.texture(self.resolve(f,v['m_Texture']))
                scale=(v['m_Scale']['x'],v['m_Scale']['y']); offset=(v['m_Offset']['x'],v['m_Offset']['y'])
        for k,v in d.get('m_SavedProperties',{}).get('m_Colors',[]):
            if k['name']=='_Color': color=(v['r'],v['g'],v['b'])
        return texture,scale,offset,color

    def export(self):
        self.image_texture('white',Image.new('RGBA',(64,64),'white'))
        f = self.load('Assets/SingleplayerMorning.unity')
        objects = {o.path_id:o.read_typetree() for o in f[0].objects}
        types = {o.path_id:o.type.name for o in f[0].objects}
        transforms = {d['m_GameObject']['m_PathID']:pid for pid,d in objects.items() if types[pid]=='Transform'}
        matrices = {}
        def matrix(pid):
            if not pid: return np.eye(4)
            if pid not in matrices:
                d = objects[pid]; matrices[pid] = matrix(d['m_Father']['m_PathID']) @ trs(d)
            return matrices[pid]
        def parents(go):
            chain=[]; pid=transforms[go]
            while pid:
                chain.append(objects[objects[pid]['m_GameObject']['m_PathID']])
                pid=objects[pid]['m_Father']['m_PathID']
            return chain
        def position(name):
            go=next(pid for pid,d in objects.items() if types[pid]=='GameObject' and d['m_Name']==name)
            return matrix(transforms[go])[:3,3]
        papers=[matrix(transforms[pid])[:3,3] for pid,d in objects.items() if types[pid]=='GameObject' and d.get('m_TagString')=='Paper']
        paper_go={pid:i for i,pid in enumerate(pid for pid,d in objects.items() if types[pid]=='GameObject' and d.get('m_TagString')=='Paper')}
        assert len(papers)==10, f'Expected ten custards, got {len(papers)}'
        enemy=position('Tinky 1'); spawn=position('Spawn'); door=position('DoorOpen'); hoover=position('vacum')
        waypoint=position('way')
        from export_noonoo import export_noonoo
        export_noonoo(self,f,objects,types,transforms,matrix)
        self.config=list(spawn)+list(enemy)+list(door)+list(hoover)+list(waypoint)+[v for p in papers for v in p]
        ai = next(d for pid,d in objects.items() if types[pid]=='MonoBehaviour' and 'sprintspeed' in d)
        player=self.load('Assets/PlayerMultiplayer.prefab')
        scripts=[o.read_typetree() for o in player[0].objects if o.type.name=='MonoBehaviour']
        move=next(d for d in scripts if 'walkSpeed' in d)
        self.config += [move['walkSpeed'],move['crchSpeed'],move['runSpeed'],ai['speed'],ai['runspeed'],ai['sprintspeed'],ai['followDistance'],ai['maxVisibleDistance'],ai['damage'],ai['offscreenDotRange'],ai['visibleDotRange']]
        for scene in ('SingleplayerMorning','SingleplayerDusk','SingleplayerNight'):
            sf=self.load(f'Assets/{scene}.unity')
            rs=next(o.read_typetree() for o in sf[0].objects if o.type.name=='RenderSettings')
            self.config += [rs['m_FogDensity']]+[rs['m_FogColor'][k] for k in ('r','g','b')]+[rs['m_AmbientLight'][k] for k in ('r','g','b')]
        # Serialized renderer meshes include the original FBX import result.
        filters={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='MeshFilter'}
        colliders={d['m_GameObject']['m_PathID']:d for pid,d in objects.items() if types[pid]=='MeshCollider' and d.get('m_Enabled',True) and not d.get('m_IsTrigger')}
        for pid,rd in objects.items():
            if types[pid] not in ('MeshRenderer','SkinnedMeshRenderer') or not rd.get('m_Enabled',True): continue
            go=rd['m_GameObject']['m_PathID']; chain=parents(go); names=[d['m_Name'] for d in chain]
            if any(not d.get('m_IsActive',1) for d in chain): continue
            if any(n in ('collect gui','popup cube','LookAt') for n in names): continue
            ref=rd.get('m_Mesh') or filters.get(go,{}).get('m_Mesh')
            mesh=self.resolve(f,ref)
            if mesh is None: continue
            handler=MeshHandler(mesh.read()); handler.process()
            vertices=np.asarray(handler.m_Vertices,dtype=np.float64)[:,:3]
            homogeneous=np.column_stack((vertices,np.ones(len(vertices))))
            world=(matrix(transforms[go]) @ homogeneous.T).T[:,:3]
            if types[pid]=='SkinnedMeshRenderer':
                md=mesh.read_typetree(); skins=md.get('m_Skin',[]); bind=md.get('m_BindPose',[])
                if skins and bind:
                    bones=[matrix(b['m_PathID']) for b in rd['m_Bones']]
                    bindmat=[np.array([[b[f'e{r}{c}'] for c in range(4)] for r in range(4)]) for b in bind]
                    world=np.zeros_like(vertices)
                    for j,s in enumerate(skins):
                        for k in range(4):
                            weight=s[f'weight[{k}]']; bi=s[f'boneIndex[{k}]']
                            if weight: world[j] += (bones[bi]@bindmat[bi]@homogeneous[j])[:3]*weight
            normals=np.asarray(handler.m_Normals if handler.m_Normals else [(0,1,0)]*len(vertices),dtype=np.float64)[:,:3]
            normal_matrix=np.linalg.inv(matrix(transforms[go])[:3,:3]).T
            normals=(normal_matrix@normals.T).T
            normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-8)
            uvs=np.asarray(handler.m_UV0 if handler.m_UV0 else [(0,0)]*len(vertices),dtype=np.float64)[:,:2]
            flags=COLLIDE if go in colliders else 0; identifier=0
            # These 15 twisted trees exist in Morning/Dusk, but not Night.
            if names[0]=='treemain1': flags|=512
            if 'Tinky 1' in names: flags=ENEMY
            if 'vacum' in names: flags|=HOOVER
            if go in paper_go: flags=COLLECT; identifier=paper_go[go]
            if 'Teletubby house' in names and names[0] in ('Component_','Componen01'):
                flags|=DOOR;identifier=0 if names[0]=='Component_' else 1
            for sub,tris in enumerate(handler.get_triangles()):
                if not tris: continue
                matref=rd['m_Materials'][min(sub,len(rd['m_Materials'])-1)] if rd['m_Materials'] else None
                tex,scale,offset,color=self.material(self.resolve(f,matref))
                uv=uvs*np.array(scale)+np.array(offset); uv[:,1]=1-uv[:,1]
                verts=np.column_stack((world,uv,normals)).astype('<f4')
                self.meshes.append((verts,np.asarray(tris,dtype='<u2'),tex,flags,identifier,color,'/'.join(reversed(names))))
        # Invisible collision meshes are retained separately; visible ones already carry COLLIDE.
        for go,cd in colliders.items():
            if go in filters: continue
            mesh=self.resolve(f,cd['m_Mesh'])
            if mesh is None: continue
            h=MeshHandler(mesh.read()); h.process(); v=np.asarray(h.m_Vertices)[:,:3]
            world=(matrix(transforms[go])@np.column_stack((v,np.ones(len(v)))).T).T[:,:3]
            tris=[t for s in h.get_triangles() for t in s]
            self.collision_meshes.append((world,np.asarray(tris,dtype='<u2')))
        for pid,cd in objects.items():
            if types[pid]!='BoxCollider' or not cd.get('m_Enabled',True) or cd.get('m_IsTrigger'): continue
            go=cd['m_GameObject']['m_PathID']
            if go in paper_go or objects[go]['m_Name'] in ('collect gui','popup cube','LookAt','DoorOpen','way'): continue
            center=xyz(cd['m_Center']); half=xyz(cd['m_Size'])*.5
            corners=np.array([center+half*np.array([x,y,z]) for x in (-1,1) for y in (-1,1) for z in (-1,1)])
            points=(matrix(transforms[go])@np.column_stack((corners,np.ones(8))).T).T[:,:3]
            self.bounds.append(list(points.min(axis=0))+list(points.max(axis=0)))
        td=next(d for pid,d in objects.items() if types[pid]=='MonoBehaviour' and 'm_TerrainData' in d)
        terrain_obj=self.resolve(f,td['m_TerrainData']); tf=self.file_of(terrain_obj); terrain=terrain_obj.read_typetree()
        height=terrain['m_Heightmap']
        self.width=height['m_Width']; self.height=height['m_Height']
        # Unity 3.x serializes height samples in x-major order.
        self.heights=np.asarray(height['m_Heights'],dtype='<u2').reshape(self.width,self.height).T.copy()
        scale=height['m_Scale']; self.config += [scale['x'],scale['y']/32767,scale['z']]
        splats=terrain['m_SplatDatabase']['m_Splats']
        # Ground fills the foreground on real PS2 video. Four 128px layers fit
        # the resident bank while retaining four times the former 64px detail.
        ground=[self.texture(self.resolve(tf,s['texture']),128) for s in splats]
        # Retain all original weights; the renderer blends texture layers.
        alpha=self.resolve(tf,terrain['m_SplatDatabase']['m_AlphaTextures'][0])
        alpha_im=np.asarray(alpha.read().image.resize((self.width-1,self.height-1),Image.Resampling.NEAREST))
        self.splats=np.flipud(alpha_im[:,:,:4]).astype('uint8').copy()
        self.config += ground
        self.trees=[]
        for tr in terrain['m_DetailDatabase']['m_TreeInstances']:
            p=xyz(tr['position'])*np.array([scale['x']*(self.width-1),scale['y'],scale['z']*(self.height-1)])
            self.trees.append(list(p)+[tr['widthScale'],tr['heightScale'],float(tr['index'])])
        model_report=export_models(self,tf,terrain)
        (self.out/'player_trees.json').write_text(json.dumps(model_report,indent=2))
        self.config += [0] # Reserved former billboard texture index.
        # The active menu keeps its own title texture. Bound this unused flat
        # fallback so the additional pine detail costs no total ELF memory.
        for key,path,size in [('title','Assets/slendytubbiestitle.png',128),('map','Assets/map/map.png',256),
                              ('scare','Assets/popups/tinky.png',128),('won','Assets/playerhaswon.png',128),
                              ('dead','Assets/playerhasdied.png',128)]:
            im=Image.open(self.root/path)
            if key=='title': im=im.crop(im.getbbox())
            if key=='won':self.config.append(self.image_texture(key,im.crop(im.getbbox()),256,64))
            else:self.config.append(self.image_texture(key,im,256,128) if key=='dead' else self.image_texture(key,im,size))
        for name in ('dipsydead','laladead','podead','yournext'):
            self.config.append(self.image_texture(name,Image.open(self.root/f'Assets/popups/{name}.png'),128))
        font=Image.new('RGBA',(128,128),(0,0,0,0)); draw=ImageDraw.Draw(font)
        ft=debug_font()
        for c in range(32,128): draw.text(((c%16)*8,(c//16-2)*16),chr(c),font=ft,fill='white')
        self.config.append(self.image_texture('font',font,128))
        self.config += [0]*6 # Preserve the door transform offsets in the blob.
        house=self.load('Assets/teletubby house/Teletubby house.FBX',True)
        clip=next(o.read_typetree() for o in house[0].objects if o.type.name=='AnimationClip' and o.read_typetree()['m_Name']=='Take 001')
        for name in ('Component_','Componen01'):
            go=next(pid for pid,d in objects.items() if types[pid]=='GameObject' and d['m_Name']==name)
            pid=transforms[go]; transform=dict(objects[pid])
            transform['m_LocalPosition']=next(c['curve']['m_Curve'][-1]['value'] for c in clip['m_PositionCurves'] if c['path']==name)
            transform['m_LocalRotation']=next(c['curve']['m_Curve'][-1]['value'] for c in clip['m_RotationCurves'] if c['path']==name)
            delta=matrix(objects[pid]['m_Father']['m_PathID'])@trs(transform)@np.linalg.inv(matrix(pid))
            self.config.extend(delta[:3,:].flatten().tolist())
        for name in ('laladead2','tinky'):
            self.config.append(self.image_texture(name,Image.open(self.root/f'Assets/popups/{name}.png'),128))
        from export_audio import export_audio
        export_audio(self,f,objects,types,matrix,transforms,player,ai)
        export_visuals(self,terrain)
        from export_fidelity import export_fidelity
        export_fidelity(self,tf,terrain)
        self.write_blob()
        from export_player_animation import export_walk
        export_walk(self.root,self.out)

    def write_blob(self):
        # Unity stores all submeshes against a shared vertex buffer. Retain only
        # referenced vertices per draw, giving each submesh accurate bounds too.
        compact=[]
        for vertices,triangles,texture,flags,identifier,color,name in self.meshes:
            if 'branches' in self.textures[texture][3].lower():flags|=1024
            if not getattr(self,'baked_colors',False):
                pixels=np.frombuffer(self.textures[texture][2],dtype='uint8').reshape(-1,4)
                if np.all(pixels[:,3]==128):
                    flags|=256
                    points=vertices[triangles,:3]
                    normal=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0])
                    if np.sum(normal*vertices[triangles,5:8].mean(axis=1))<0:triangles=triangles[:,[0,2,1]].copy()
                    if len(triangles)>256 and not flags&(ENEMY|HOOVER|32|64):
                        triangles=planar_subdivisions(vertices,triangles)
            def partition(indices):
                centers=vertices[indices,:3].mean(axis=1)
                extent=np.ptp(centers,axis=0)
                # Small spatial clusters let the PS2 reject off-screen leaves and
                # building faces before transforming their individual vertices.
                if len(indices)<=64 or flags&(ENEMY|DOOR|HOOVER|64) or getattr(self,'baked_colors',False):
                    yield indices;return
                axis=extent.argmax();order=np.argsort(centers[:,axis]);middle=len(order)//2
                yield from partition(indices[order[:middle]])
                yield from partition(indices[order[middle:]])
            for part in partition(triangles):
                used,indices=np.unique(part,return_inverse=True)
                compact.append((vertices[used].copy(),indices.reshape(-1,3).astype('<u2'),texture,flags,identifier,color,name))
        self.meshes=compact
        data=bytearray(80)
        def add(raw,alignment=16):
            data.extend(b'\0'*((-len(data))%alignment)); offset=len(data); data.extend(raw); return offset
        meshes=[]; vertex_offsets={}
        for verts,tris,tex,flags,identifier,color,name in self.meshes:
            raw=verts.tobytes()
            if raw not in vertex_offsets: vertex_offsets[raw]=add(raw)
            vo=vertex_offsets[raw]; io=add(tris.tobytes())
            bbox=list(verts[:,:3].min(axis=0))+list(verts[:,:3].max(axis=0))
            meshes.append(struct.pack('<7I9f',len(verts),len(tris),vo,io,tex,flags,identifier,*bbox,*color))
        mo=add(b''.join(meshes)); to_desc=[]
        for width,height,pixels,key in self.textures:
            offset=add(pixels,128); to_desc.append(struct.pack('<4I',width,height,offset,len(pixels)))
        to=add(b''.join(to_desc)); bo=add(np.asarray(self.bounds,dtype='<f4').tobytes())
        tro=add(np.asarray(self.trees,dtype='<f4').tobytes()); ho=add(self.heights.tobytes()); so=add(self.splats.tobytes())
        if hasattr(self,'grass_patches'):
            self.config += [add(self.grass_index.tobytes()),add(self.grass_patches),self.grass_texture]
            if hasattr(self,'grass_grid_size'):self.config += [self.grass_grid_size,self.grass_distance]
        co=add(np.asarray(self.config,dtype='<f4').tobytes()); ao_desc=[];audio_offsets={}
        for name,pcm in self.audio:
            if pcm not in audio_offsets:audio_offsets[pcm]=add(pcm,64)
            count=len(pcm)|0x80000000 if self.audio_codes.get(name)=='ulaw' else len(pcm)//2
            offset=audio_offsets[pcm]; ao_desc.append(struct.pack('<2I',offset,count))
        ao=add(b''.join(ao_desc))
        cm=[]
        for verts,tris in self.collision_meshes:
            vo=add(verts.astype('<f4').tobytes()); io=add(tris.tobytes())
            bbox=list(verts.min(axis=0))+list(verts.max(axis=0))
            cm.append(struct.pack('<4I6f',len(verts),len(tris),vo,io,*bbox))
        cmo=add(b''.join(cm))
        version=3 if hasattr(self,'grass_grid_size') else 2 if hasattr(self,'grass_patches') else 1
        struct.pack_into('<20I',data,0,0x53505453,version,len(meshes),len(self.textures),len(self.bounds),len(self.trees),self.width,self.height,mo,to,bo,tro,ho,so,co,len(self.audio),ao,len(cm),cmo,len(data))
        (self.out/'world.bin').write_bytes(data)
        report={'blob_bytes':len(data),'meshes':len(meshes),'vertices':sum(len(m[0]) for m in self.meshes),'triangles':sum(len(m[1]) for m in self.meshes),'textures':len(self.textures),'texture_bytes':sum(len(t[2]) for t in self.textures),'trees':len(self.trees),'bounds':len(self.bounds),'collision_meshes':len(cm),'warnings':sorted(set(self.warnings)),'mesh_names':[m[-1] for m in self.meshes],'config':self.config,'texture_names':[t[-1] for t in self.textures]}
        (self.out/'export_report.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        print(json.dumps({k:report[k] for k in ('blob_bytes','meshes','vertices','triangles','textures','texture_bytes','trees','bounds','collision_meshes','warnings')},indent=2))

if __name__=='__main__':
    parser=argparse.ArgumentParser(); parser.add_argument('--source',default='original'); parser.add_argument('--output',default='ps2/generated')
    args=parser.parse_args(); Exporter(args.source,args.output).export()
